// h264_decoder.cpp — 最小 H.264/AVC 基线解码器实现。
//
// 复用前面几篇的模块（切 NAL、SPS/PPS、slice header、位读取、帧内预测、
// 整数反变换、CAVLC 4x4、去块滤波），并补齐把它们粘成完整解码器所必需的
// 几块胶水：CBP 映射、Intra_4x4 模式信令、色度帧内预测、Intra_16x16 亮度 DC
// 哈达玛变换、色度 DC 变换、nnz(每个 4x4 块非零系数个数)上下文管理，以及
// 环路去块滤波的整帧调度。全程严格照 ITU-T H.264 第 8 章。
#include "h264_decoder.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "cavlc_decode.h"
#include "deblock_filter.h"
#include "intra_predict.h"
#include "nal_splitter.h"
#include "rbsp_bit_reader.h"
#include "slice_header.h"
#include "transform_quant.h"

namespace cfs {

namespace {

inline int Clip1(int v) { return std::max(0, std::min(255, v)); }

// ---- 一帧的可写重建缓冲（内部用 int，最后 Clip 到 uint8）----
struct Plane {
    int w = 0, h = 0;
    std::vector<int> px;
    void alloc(int width, int height) {
        w = width; h = height; px.assign(static_cast<size_t>(w) * h, 0);
    }
    int& at(int x, int y) { return px[static_cast<size_t>(y) * w + x]; }
    int get(int x, int y) const { return px[static_cast<size_t>(y) * w + x]; }
};

struct Picture {
    Plane Y, Cb, Cr;
    void alloc(int w, int h) {
        Y.alloc(w, h);
        Cb.alloc(w / 2, h / 2);
        Cr.alloc(w / 2, h / 2);
    }
};

// 每个宏块解出的信息，供邻居预测/去块使用。
struct MbInfo {
    bool decoded = false;
    bool is_intra = false;
    bool is_i16 = false;
    int qp = 0;
    // 每个 4x4 亮度块(光栅内 16 个)的非零系数个数，供 nC 推导与 bS 判定。
    std::array<int, 16> luma_nnz{};
    // 色度每分量 4 个 4x4 块的非零系数个数。
    std::array<int, 8> chroma_nnz{};  // [0..3]=Cb, [4..7]=Cr
};

}  // namespace

namespace {

// 4x4 亮度块在宏块内的解码扫描顺序 -> (x4,y4) 块坐标（spec 6.4.3，
// InverseRasterScan 于 8x8 分组内再 4x4）。luma4x4BlkIdx 0..15。
// H.264 的 4x4 块顺序是 Z 字（每个 8x8 内 4 个块，宏块内 4 个 8x8）。
const int kBlk4x4ScanX[16] = {0,1,0,1, 2,3,2,3, 0,1,0,1, 2,3,2,3};
const int kBlk4x4ScanY[16] = {0,0,1,1, 0,0,1,1, 2,2,3,3, 2,2,3,3};

// coded_block_pattern 的 me(v) 映射（spec Table 9-4，仅 ChromaArrayType==1、
// Intra 与 Inter 两列）。codeNum -> cbp。索引 [codeNum]。
// 长度 48（Intra_4x4/Intra_16x16 与 Inter 各一套；这里给 Intra 与 Inter 两表）。
const uint8_t kCbpIntra[48] = {
    47,31,15, 0,23,27,29,30, 7,11,13,14,39,43,45,46,
    16, 3, 5,10,12,19,21,26,28,35,37,42,44, 1, 2, 4,
     8,17,18,20,24, 6, 9,22,25,32,33,34,36,40,38,41};
const uint8_t kCbpInter[48] = {
     0,16, 1, 2, 4, 8,32, 3, 5,10,12,15,47, 7,11,13,
    14, 6, 9,31,35,37,42,44,33,34,36,40,39,43,45,46,
    17,18,20,24,19,21,26,28,23,27,29,30,22,25,38,41};

uint32_t MapCbp(uint32_t code_num, bool intra) {
    if (code_num >= 48) return 0;
    return intra ? kCbpIntra[code_num] : kCbpInter[code_num];
}

// 色度量化参数映射（spec Table 8-15 / 8.5.8）：qPi -> QPC。
const int kQpcMap[52] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,
    28,29,29,30,31,32,32,33,34,34,35,35,36,36,37,37,37,38,38,38,39,39,39,39};
int ChromaQp(int qpy, int chroma_qp_offset) {
    int qpi = std::max(-12, std::min(51, qpy + chroma_qp_offset));
    if (qpi < 30) return qpi;
    return kQpcMap[qpi];
}

// Intra_16x16 的 mb_type(1..24) 解出 (预测模式0..3, cbp_chroma0..2, cbp_luma0/15)。
// spec Table 7-11。
void ParseI16Type(uint32_t mb_type, int* pred_mode, int* cbp_chroma,
                  int* cbp_luma) {
    uint32_t t = mb_type - 1;  // 0..23
    *pred_mode = static_cast<int>(t % 4);
    uint32_t rest = t / 4;     // 0..5
    *cbp_luma = (rest >= 3) ? 15 : 0;
    *cbp_chroma = static_cast<int>(rest % 3);
}

}  // namespace

namespace {

// Intra_16x16 亮度 DC：4x4 哈达玛反变换 + 缩放（spec 8.5.10）。
// 输入 16 个 DC 系数(扫描顺序，raster 4x4)，输出 16 个已缩放的 DC 值，
// 分别回填到 16 个 4x4 亮度块的 (0,0) 位置（反变换前）。
std::array<int, 16> InvLumaDcHadamard(const std::array<int, 16>& c, int qpy) {
    int f[4][4];
    // 摆成 4x4（raster）。
    int m[4][4];
    for (int i = 0; i < 16; ++i) m[i / 4][i % 4] = c[i];
    // 行哈达玛。
    int g[4][4];
    for (int i = 0; i < 4; ++i) {
        int s0 = m[i][0] + m[i][2];
        int s1 = m[i][0] - m[i][2];
        int s2 = m[i][1] - m[i][3];
        int s3 = m[i][1] + m[i][3];
        g[i][0] = s0 + s3; g[i][1] = s1 + s2;
        g[i][2] = s1 - s2; g[i][3] = s0 - s3;
    }
    // 列哈达玛。
    for (int j = 0; j < 4; ++j) {
        int s0 = g[0][j] + g[2][j];
        int s1 = g[0][j] - g[2][j];
        int s2 = g[1][j] - g[3][j];
        int s3 = g[1][j] + g[3][j];
        f[0][j] = s0 + s3; f[1][j] = s1 + s2;
        f[2][j] = s1 - s2; f[3][j] = s0 - s3;
    }
    // 缩放（spec 8.5.10 式 8-330/8-331）：LevelScale(qP%6,0,0)=v[qP%6][0]。
    static const int kV0[6] = {10, 11, 13, 14, 16, 18};
    int scale = kV0[qpy % 6];
    int shift = qpy / 6;
    std::array<int, 16> out{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            int v;
            if (shift >= 2)
                v = (f[i][j] * scale) << (shift - 2);
            else
                v = (f[i][j] * scale + (1 << (1 - shift))) >> (2 - shift);
            out[i * 4 + j] = v;
        }
    return out;
}

// 色度 DC：2x2 哈达玛反变换 + 缩放（spec 8.5.11.1）。4 个 DC -> 4 个已缩放值。
std::array<int, 4> InvChromaDcHadamard(const std::array<int, 4>& c, int qpc) {
    // 2x2 哈达玛。
    int a = c[0], b = c[1], d = c[2], e = c[3];
    int f0 = a + b + d + e;
    int f1 = a - b + d - e;
    int f2 = a + b - d - e;
    int f3 = a - b - d + e;
    static const int kV0[6] = {10, 11, 13, 14, 16, 18};
    int scale = kV0[qpc % 6];
    int shift = qpc / 6;
    std::array<int, 4> out{};
    int f[4] = {f0, f1, f2, f3};
    for (int i = 0; i < 4; ++i)
        out[i] = ((f[i] * scale) << shift) >> 5;
    return out;
}

}  // namespace

namespace {

struct VlcEntry { int len; int code; };

// Table 9-5 的 chroma DC 列（nC==-1，ChromaArrayType==1）。
// 索引 [trailing_ones(0..3)][total_coeff(0..4)]。len=0 表示不存在。
const VlcEntry kCoeffTokenChromaDc[4][5] = {
    {{2,1},{6,7},{6,4},{6,3},{6,2}},
    {{0,0},{1,1},{6,6},{7,3},{8,3}},
    {{0,0},{0,0},{3,1},{7,2},{8,2}},
    {{0,0},{0,0},{0,0},{6,5},{7,0}},
};

// Table 9-9(a)：chroma DC 的 total_zeros（maxNumCoeff==4）。
// 索引 [total_coeff-1 (0..2)][total_zeros (0..3)]。
const VlcEntry kTotalZerosChromaDc[3][4] = {
    {{1,1},{2,1},{3,1},{3,0}},
    {{1,1},{2,1},{2,0},{0,0}},
    {{1,1},{1,0},{0,0},{0,0}},
};

int MatchVlc(RbspBitReader& br, const VlcEntry* entries, int count,
             int* out_index) {
    uint32_t bits = 0;
    for (int len = 1; len <= 16; ++len) {
        bits = (bits << 1) | br.ReadBit();
        for (int i = 0; i < count; ++i) {
            if (entries[i].len == len &&
                static_cast<uint32_t>(entries[i].code) == bits) {
                *out_index = i;
                return 0;
            }
        }
        if (br.Overrun()) break;
    }
    *out_index = -1;
    return -1;
}

CoeffToken DecodeChromaDcCoeffToken(RbspBitReader& br) {
    uint32_t bits = 0;
    for (int len = 1; len <= 8; ++len) {
        bits = (bits << 1) | br.ReadBit();
        for (int t1 = 0; t1 < 4; ++t1)
            for (int tc = 0; tc < 5; ++tc) {
                const VlcEntry& e = kCoeffTokenChromaDc[t1][tc];
                if (e.len == len && static_cast<uint32_t>(e.code) == bits) {
                    CoeffToken tok; tok.trailing_ones = t1; tok.total_coeff = tc;
                    return tok;
                }
            }
        if (br.Overrun()) break;
    }
    return CoeffToken{};
}

// 解 4 个色度 DC 系数（扫描顺序）。返回 nnz(非零系数个数)。
int DecodeChromaDcResidual(RbspBitReader& br, std::array<int, 4>* out) {
    out->fill(0);
    CoeffToken tok = DecodeChromaDcCoeffToken(br);
    int total_coeff = tok.total_coeff;
    int t1s = tok.trailing_ones;
    if (total_coeff == 0) return 0;

    std::array<int, 4> levels{};
    int suffix_length = 0;
    for (int i = 0; i < total_coeff; ++i) {
        if (i < t1s) {
            levels[i] = br.ReadBit() ? -1 : 1;
            continue;
        }
        int prefix = 0;
        while (br.ReadBit() == 0 && !br.Overrun()) ++prefix;
        int suf_size = suffix_length;
        if (prefix == 14 && suffix_length == 0) suf_size = 4;
        else if (prefix >= 15) suf_size = prefix - 3;
        int suffix = suf_size > 0 ? static_cast<int>(br.ReadBits(suf_size)) : 0;
        int level_code = (prefix << suffix_length) + suffix;
        if (prefix >= 15 && suffix_length == 0) level_code += 15;
        if (prefix >= 16) level_code += (1 << (prefix - 3)) - 4096;
        if (i == t1s && t1s < 3) level_code += 2;
        int value = (level_code & 1) ? ((-level_code - 1) >> 1)
                                     : ((level_code + 2) >> 1);
        levels[i] = value;
        if (suffix_length == 0) suffix_length = 1;
        int a = value < 0 ? -value : value;
        if (a > (3 << (suffix_length - 1)) && suffix_length < 6) ++suffix_length;
    }
    // total_zeros（maxNumCoeff==4）。
    int total_zeros = 0;
    if (total_coeff < 4) {
        int idx = -1;
        MatchVlc(br, kTotalZerosChromaDc[total_coeff - 1], 4, &idx);
        total_zeros = (idx < 0) ? 0 : idx;
    }
    // run_before：复用亮度那套（zeros_left 选行），但 chroma DC 直接内联。
    std::array<int, 4> runs{};
    int zeros_left = total_zeros;
    for (int i = 0; i < total_coeff - 1; ++i) {
        int run = 0;
        if (zeros_left > 0) {
            // Table 9-10（与亮度共用），这里内联最常用的前几行。
            static const VlcEntry kRun[7][15] = {
                {{1,1},{1,0}},
                {{1,1},{2,1},{2,0}},
                {{2,3},{2,2},{2,1},{2,0}},
                {{2,3},{2,2},{2,1},{3,1},{3,0}},
                {{2,3},{2,2},{3,3},{3,2},{3,1},{3,0}},
                {{2,3},{3,0},{3,1},{3,3},{3,2},{3,5},{3,4}},
                {{3,7},{3,6},{3,5},{3,4},{3,3},{3,2},{3,1},{4,1},{5,1},
                 {6,1},{7,1},{8,1},{9,1},{10,1},{11,1}}};
            int ridx = (zeros_left > 6) ? 6 : (zeros_left - 1);
            int valid = (zeros_left > 6) ? 15 : (zeros_left + 1);
            int col = -1;
            MatchVlc(br, kRun[ridx], valid, &col);
            run = (col < 0) ? 0 : col;
        }
        runs[i] = run;
        zeros_left -= run;
    }
    runs[total_coeff - 1] = zeros_left;
    // 组合还原到 4 个扫描位置。
    int pos = total_coeff + total_zeros - 1;
    for (int i = 0; i < total_coeff; ++i) {
        if (pos >= 0 && pos < 4) (*out)[pos] = levels[i];
        pos -= 1 + runs[i];
    }
    return total_coeff;
}

}  // namespace

namespace {

// 色度 8x8 帧内预测（spec 8.3.4）。mode: 0=DC,1=Horizontal,2=Vertical,3=Plane。
// top[0..7], left[0..7], top_left；可用性由 avail_top/avail_left 指定。
// 输出 8x8 预测块 pred[y][x]。
void PredictChroma8x8(int mode, const std::array<int, 8>& top,
                      const std::array<int, 8>& left, int top_left,
                      bool avail_top, bool avail_left,
                      std::array<std::array<int, 8>, 8>* pred) {
    auto& P = *pred;
    if (mode == 1) {  // Horizontal
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) P[y][x] = left[y];
        return;
    }
    if (mode == 2) {  // Vertical
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) P[y][x] = top[x];
        return;
    }
    if (mode == 3) {  // Plane（spec 8.3.4.4）
        int H = 0, V = 0;
        for (int x = 0; x < 4; ++x)
            H += (x + 1) * (top[4 + x] - (x == 3 ? top_left : top[2 - x]));
        for (int y = 0; y < 4; ++y)
            V += (y + 1) * (left[4 + y] - (y == 3 ? top_left : left[2 - y]));
        int a = 16 * (left[7] + top[7]);
        int b = (17 * H + 16) >> 5;
        int c = (17 * V + 16) >> 5;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                P[y][x] = Clip1((a + b * (x - 3) + c * (y - 3) + 16) >> 5);
        return;
    }
    // mode 0: DC，按 4 个 4x4 象限分别取均值（spec 8.3.4.1）。
    for (int by = 0; by < 2; ++by) {
        for (int bx = 0; bx < 2; ++bx) {
            int sum = 0, cnt = 0, val = 128;
            bool use_top = avail_top;
            bool use_left = avail_left;
            // 象限规则：左上/右下用 top+left；右上优先 top；左下优先 left。
            bool prefer_top = (bx == 1 && by == 0);
            bool prefer_left = (bx == 0 && by == 1);
            if (prefer_top && use_top) {
                for (int i = 0; i < 4; ++i) sum += top[bx * 4 + i];
                cnt = 4; val = (sum + 2) >> 2;
            } else if (prefer_top && use_left) {
                for (int i = 0; i < 4; ++i) sum += left[by * 4 + i];
                cnt = 4; val = (sum + 2) >> 2;
            } else if (prefer_left && use_left) {
                for (int i = 0; i < 4; ++i) sum += left[by * 4 + i];
                cnt = 4; val = (sum + 2) >> 2;
            } else if (prefer_left && use_top) {
                for (int i = 0; i < 4; ++i) sum += top[bx * 4 + i];
                cnt = 4; val = (sum + 2) >> 2;
            } else {  // 左上、右下：top+left 都有则平均，否则退一个
                if (use_top) { for (int i = 0; i < 4; ++i) sum += top[bx*4+i]; cnt += 4; }
                if (use_left) { for (int i = 0; i < 4; ++i) sum += left[by*4+i]; cnt += 4; }
                if (cnt == 8) val = (sum + 4) >> 3;
                else if (cnt == 4) val = (sum + 2) >> 2;
                else val = 128;
            }
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                    P[by * 4 + y][bx * 4 + x] = val;
        }
    }
}

}  // namespace

namespace {

// 一帧解码的工作上下文：持有重建缓冲、每个宏块信息、bit reader、参数。
struct SliceDecoder {
    const Sps& sps;
    const Pps& pps;
    const SliceHeader& sh;
    RbspBitReader& br;
    Picture& pic;
    int mbw, mbh;                 // 宏块宽/高（以宏块计）
    std::vector<MbInfo> mbs;      // size = mbw*mbh
    int slice_qp;
    DecodeStats* stats;
    // 每个 4x4 亮度块的帧内预测模式（-1 表示未定），供 predIntra4x4 的
    // 最可能模式(most probable mode)推导。索引 = mbAddr*16 + blk4x4。
    std::vector<int> i4_modes;

    SliceDecoder(const Sps& s, const Pps& p, const SliceHeader& h,
                 RbspBitReader& b, Picture& pc, int w, int hh, int qp,
                 DecodeStats* st)
        : sps(s), pps(p), sh(h), br(b), pic(pc), mbw(w), mbh(hh),
          slice_qp(qp), stats(st) {
        mbs.assign(static_cast<size_t>(mbw) * mbh, MbInfo{});
        i4_modes.assign(static_cast<size_t>(mbw) * mbh * 16, -1);
    }

    int mbAddr(int mx, int my) const { return my * mbw + mx; }

    // 亮度 4x4 块在整帧的像素左上角。
    void luma4x4Origin(int mx, int my, int blk, int* px, int* py) const {
        *px = mx * 16 + kBlk4x4ScanX[blk] * 4;
        *py = my * 16 + kBlk4x4ScanY[blk] * 4;
    }

    // 收集 4x4 亮度块的邻居像素（从已重建的 pic.Y 里取）。
    Neighbors4x4 gatherLuma4x4(int mx, int my, int blk) {
        Neighbors4x4 nb;
        int px, py;
        luma4x4Origin(mx, my, blk, &px, &py);
        bool left_ok = px > 0;
        bool top_ok = py > 0;
        // 右上可用性：粗略处理——若在帧内且上一行存在则可用，块位于宏块右上
        // 边界外时退化。为对齐 ffmpeg，越界的上/右上按 spec 用 top_left 或复制。
        nb.left_available = left_ok;
        nb.top_available = top_ok;
        nb.top_left_available = left_ok && top_ok;
        for (int i = 0; i < 4; ++i)
            nb.left[i] = left_ok ? pic.Y.get(px - 1, py + i) : 0;
        for (int i = 0; i < 8; ++i) {
            int sx = px + i;
            if (sx >= pic.Y.w) sx = pic.Y.w - 1;
            nb.top[i] = top_ok ? pic.Y.get(sx, py - 1) : 0;
        }
        nb.top_left = (left_ok && top_ok) ? pic.Y.get(px - 1, py - 1) : 0;
        // 右上(p[4..7,-1])可用性（spec 6.4.11.4）：与块在宏块内 Z 扫描的位置
        // 有关。下面这张表标出 16 个块中"右上邻块在解码顺序里尚未重建"的块，
        // 这些块即便不在帧右边界也要把右上样本回退成 top[3]。
        static const bool kTrUnavailInMb[16] = {
            false,false,false,true,  false,true,false,true,
            false,false,false,true,  false,true,false,true};
        int tr_px = px + 4;
        bool tr_in_frame = top_ok && (tr_px < pic.Y.w);
        bool tr_ok = tr_in_frame && !kTrUnavailInMb[blk];
        if (top_ok) {
            if (tr_ok) {
                for (int i = 4; i < 8; ++i) {
                    int sx = px + i;
                    if (sx >= pic.Y.w) sx = pic.Y.w - 1;
                    nb.top[i] = pic.Y.get(sx, py - 1);
                }
                nb.top_right_available = true;
            } else {
                for (int i = 4; i < 8; ++i) nb.top[i] = nb.top[3];
                nb.top_right_available = false;
            }
        }
        return nb;
    }
};

}  // namespace

namespace {

// 一个宏块内 luma4x4BlkIdx -> 其左邻块 / 上邻块的 (mbAddrOffset, blkIdx)。
// 为简化，我们直接用整帧像素坐标推导邻居的已解模式：这里维护一个
// "每 4x4 块的模式"数组（i4_modes），按整帧 4x4 网格索引。
// 帧内 4x4 网格宽 = mbw*4。
inline int frame4x4Index(const SliceDecoder& d, int mx, int my, int blk) {
    int gx = mx * 4 + kBlk4x4ScanX[blk];
    int gy = my * 4 + kBlk4x4ScanY[blk];
    return gy * (d.mbw * 4) + gx;
}

// 取当前 4x4 块的最可能预测模式(most probable mode，spec 8.3.1.1)。
int mostProbableMode(SliceDecoder& d, std::vector<int>& modes4x4, int mx,
                     int my, int blk) {
    int gx = mx * 4 + kBlk4x4ScanX[blk];
    int gy = my * 4 + kBlk4x4ScanY[blk];
    int stride = d.mbw * 4;
    bool left_ok = gx > 0;
    bool top_ok = gy > 0;
    int modeA = left_ok ? modes4x4[gy * stride + (gx - 1)] : -1;
    int modeB = top_ok ? modes4x4[(gy - 1) * stride + gx] : -1;
    // 邻居不可用或非 Intra_4x4/Intra_16x16 -> 视为 DC(2)。这里非帧内在 I 帧
    // 不会出现；不可用置 2。
    if (!left_ok || modeA < 0) modeA = (left_ok ? 2 : -1);
    if (!top_ok || modeB < 0) modeB = (top_ok ? 2 : -1);
    if (modeA < 0 || modeB < 0) return 2;  // 有一侧不可用 -> DC
    return std::min(modeA, modeB);
}

}  // namespace

namespace {

// 把 4x4 残差加到预测上并写回 pic.Y。
void addResidual4x4(Plane& p, int px, int py, const Coeff4x4& res,
                    const Block4x4& pred) {
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            p.at(px + x, py + y) = Clip1(pred[y][x] + res[y][x]);
}

// 解码一个亮度 4x4 块的残差（CAVLC）并返回非零系数个数。coeff 填扫描顺序。
int decodeLumaBlockResidual(SliceDecoder& d, int nc, Coeff4x4* out) {
    CavlcResult r = DecodeResidual4x4(d.br, nc, 16);
    if (getenv("CFS_DBG2"))
        std::fprintf(stderr, "      luma4x4 nc=%d tc=%d t1=%d tz=%d bits=%zu\n",
                     nc, r.token.total_coeff, r.token.trailing_ones,
                     r.total_zeros, r.bits_used);
    auto zz = InverseZigzag4x4(r.coeff);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) (*out)[i][j] = zz[i][j];
    return r.token.total_coeff;
}

// 由整帧 4x4 网格坐标找左/上邻块 nnz，推导 nC（spec 9.2.1）。
int deriveLumaNc(SliceDecoder& d, MbInfo& cur, int mx, int my, int blk) {
    int bx = kBlk4x4ScanX[blk];
    int by = kBlk4x4ScanY[blk];
    int nnzA, nnzB;
    bool availA, availB;
    // 左邻。
    if (bx > 0) {
        // 同宏块内左邻块：在 Z 扫描里找 (bx-1,by) 对应的 blk。
        int lblk = -1;
        for (int k = 0; k < 16; ++k)
            if (kBlk4x4ScanX[k] == bx - 1 && kBlk4x4ScanY[k] == by) { lblk = k; break; }
        nnzA = cur.luma_nnz[lblk]; availA = true;
    } else {
        // 左宏块最右列同 by 的块。
        if (mx - 1 >= 0 && d.mbs[d.mbAddr(mx - 1, my)].decoded) {
            int lblk = -1;
            for (int k = 0; k < 16; ++k)
                if (kBlk4x4ScanX[k] == 3 && kBlk4x4ScanY[k] == by) { lblk = k; break; }
            nnzA = d.mbs[d.mbAddr(mx - 1, my)].luma_nnz[lblk]; availA = true;
        } else { nnzA = 0; availA = false; }
    }
    // 上邻。
    if (by > 0) {
        int tblk = -1;
        for (int k = 0; k < 16; ++k)
            if (kBlk4x4ScanX[k] == bx && kBlk4x4ScanY[k] == by - 1) { tblk = k; break; }
        nnzB = cur.luma_nnz[tblk]; availB = true;
    } else {
        if (my - 1 >= 0 && d.mbs[d.mbAddr(mx, my - 1)].decoded) {
            int tblk = -1;
            for (int k = 0; k < 16; ++k)
                if (kBlk4x4ScanX[k] == bx && kBlk4x4ScanY[k] == 3) { tblk = k; break; }
            nnzB = d.mbs[d.mbAddr(mx, my - 1)].luma_nnz[tblk]; availB = true;
        } else { nnzB = 0; availB = false; }
    }
    return DeriveNc(nnzA, availA, nnzB, availB);
}

}  // namespace

namespace {

// 色度邻居收集：从已重建 pic.Cb/Cr 取 top[0..7]/left[0..7]/top_left。
void gatherChroma(Plane& c, int cx, int cy, std::array<int, 8>* top,
                  std::array<int, 8>* left, int* tl, bool* at, bool* al) {
    *at = cy > 0;
    *al = cx > 0;
    for (int i = 0; i < 8; ++i) {
        (*top)[i] = (*at) ? c.get(std::min(cx + i, c.w - 1), cy - 1) : 128;
        (*left)[i] = (*al) ? c.get(cx - 1, cy + i) : 128;
    }
    *tl = (*at && *al) ? c.get(cx - 1, cy - 1) : 128;
}

// 色度 AC 块的 nC 推导：色度每分量按 2x2 的 4x4 块排布（b8: 0=左上,1=右上,
// 2=左下,3=右下）。左/上邻块可能在本宏块内或相邻宏块的对应分量里。
int deriveChromaNc(SliceDecoder& d, MbInfo& cur, int mx, int my, int comp,
                   int b8) {
    int bx = b8 % 2, by = b8 / 2;
    int base = comp * 4;
    int nnzA, nnzB; bool availA, availB;
    // 左邻。
    if (bx > 0) {
        nnzA = cur.chroma_nnz[base + (by * 2 + (bx - 1))]; availA = true;
    } else if (mx - 1 >= 0 && d.mbs[d.mbAddr(mx - 1, my)].decoded) {
        nnzA = d.mbs[d.mbAddr(mx - 1, my)].chroma_nnz[base + (by * 2 + 1)];
        availA = true;
    } else { nnzA = 0; availA = false; }
    // 上邻。
    if (by > 0) {
        nnzB = cur.chroma_nnz[base + ((by - 1) * 2 + bx)]; availB = true;
    } else if (my - 1 >= 0 && d.mbs[d.mbAddr(mx, my - 1)].decoded) {
        nnzB = d.mbs[d.mbAddr(mx, my - 1)].chroma_nnz[base + (2 + bx)];
        availB = true;
    } else { nnzB = 0; availB = false; }
    return DeriveNc(nnzA, availA, nnzB, availB);
}

// 解码一个宏块的色度（预测 + DC 变换 + AC 残差），写回 pic.Cb/Cr。
// intra_chroma_mode: 0..3；cbp_chroma: 0=无,1=只DC,2=DC+AC。
void decodeChromaMb(SliceDecoder& d, MbInfo& cur, int mx, int my,
                    int intra_chroma_mode, int cbp_chroma) {
    int qpc = ChromaQp(cur.qp, d.pps.chroma_qp_index_offset);
    Plane* planes[2] = {&d.pic.Cb, &d.pic.Cr};
    int cx = mx * 8, cy = my * 8;

    // 关键：色度残差的码流顺序（spec 7.3.5.3.2）是——
    //   先两个分量的 DC（Cb DC，Cr DC），再两个分量的全部 AC（Cb 4 块，Cr 4 块）。
    // 不能"每分量各自 DC+AC"地交错，否则比特位置会错乱。
    std::array<std::array<int, 4>, 2> dc{};       // [comp] 各 4 个 DC 系数
    std::array<std::array<int, 4>, 2> dc_scaled{};
    for (int comp = 0; comp < 2; ++comp) {
        int dcnnz = 0;
        size_t dcp0 = d.br.BitPos();
        if (cbp_chroma >= 1) dcnnz = DecodeChromaDcResidual(d.br, &dc[comp]);
        if (getenv("CFS_DBG2"))
            std::fprintf(stderr, "      chromaDC c=%d nnz=%d bits=%zu\n",
                         comp, dcnnz, d.br.BitPos() - dcp0);
        dc_scaled[comp] = InvChromaDcHadamard(dc[comp], qpc);
    }

    // 预测 + AC + 重建。
    for (int comp = 0; comp < 2; ++comp) {
        Plane& c = *planes[comp];
        std::array<int, 8> top, left; int tl; bool at, al;
        gatherChroma(c, cx, cy, &top, &left, &tl, &at, &al);
        std::array<std::array<int, 8>, 8> pred;
        PredictChroma8x8(intra_chroma_mode, top, left, tl, at, al, &pred);

        for (int b8 = 0; b8 < 4; ++b8) {
            int bx = (b8 % 2) * 4, by = (b8 / 2) * 4;
            Coeff4x4 coeff{};
            int nnz = 0;
            if (cbp_chroma == 2) {
                int nc = deriveChromaNc(d, cur, mx, my, comp, b8);
                CavlcResult r = DecodeResidual4x4(d.br, nc, 15);  // AC:最多15
                if (getenv("CFS_DBG2"))
                    std::fprintf(stderr, "      chromaAC c=%d b8=%d nc=%d tc=%d bits=%zu\n",
                                 comp, b8, nc, r.token.total_coeff, r.bits_used);
                Cavlc4x4 shifted{};                               // 右移一位放置
                for (int i = 0; i < 15; ++i) shifted[i + 1] = r.coeff[i];
                auto zz = InverseZigzag4x4(shifted);
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j) coeff[i][j] = zz[i][j];
                nnz = r.token.total_coeff;
            }
            Coeff4x4 dq = DequantResidual4x4(coeff, qpc);
            dq[0][0] = dc_scaled[comp][b8];  // 覆盖 DC
            Coeff4x4 res = InverseTransform4x4(dq);
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                    c.at(cx + bx + x, cy + by + y) =
                        Clip1(pred[by + y][bx + x] + res[y][x]);
            cur.chroma_nnz[comp * 4 + b8] = nnz;
        }
    }
}

}  // namespace

namespace {

// 解一个 I_16x16 宏块的亮度：整块一个预测模式 + DC 哈达玛 + 16 个 AC 块。
void decodeI16x16Luma(SliceDecoder& d, MbInfo& cur, int mx, int my,
                      int pred_mode, int cbp_luma) {
    // 16x16 预测。
    Neighbors16x16 nb;
    int ox = mx * 16, oy = my * 16;
    nb.left_available = ox > 0;
    nb.top_available = oy > 0;
    nb.top_left_available = nb.left_available && nb.top_available;
    for (int i = 0; i < 16; ++i) {
        nb.top[i] = nb.top_available ? d.pic.Y.get(ox + i, oy - 1) : 0;
        nb.left[i] = nb.left_available ? d.pic.Y.get(ox - 1, oy + i) : 0;
    }
    nb.top_left = nb.top_left_available ? d.pic.Y.get(ox - 1, oy - 1) : 0;
    Block16x16 pred = PredictIntra16x16(nb, static_cast<Intra16x16Mode>(pred_mode));

    // 亮度 DC（16 个 4x4 的 DC 组成 4x4，先 CAVLC 解，再哈达玛）。
    std::array<int, 16> luma_dc{};
    {
        // DC 块用 nC（取左/上邻宏块首块上下文，简化用 0..）。
        int nc = deriveLumaNc(d, cur, mx, my, 0);
        CavlcResult r = DecodeResidual4x4(d.br, nc, 16);
        if (getenv("CFS_DBG2"))
            std::fprintf(stderr, "      I16dc nc=%d tc=%d bits=%zu\n",
                         nc, r.token.total_coeff, r.bits_used);
        // 反 zigzag 到 raster 顺序供哈达玛。
        auto zz = InverseZigzag4x4(r.coeff);
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) luma_dc[i * 4 + j] = zz[i][j];
    }
    std::array<int, 16> dc_scaled = InvLumaDcHadamard(luma_dc, cur.qp);

    // 16 个 AC 块。
    for (int blk = 0; blk < 16; ++blk) {
        int px, py;
        d.luma4x4Origin(mx, my, blk, &px, &py);
        Coeff4x4 coeff{};
        int nnz = 0;
        if (cbp_luma) {  // I_16x16 的 cbp_luma 是 0 或 15（全有或全无 AC）
            int nc = deriveLumaNc(d, cur, mx, my, blk);
            CavlcResult r = DecodeResidual4x4(d.br, nc, 15);
            Cavlc4x4 shifted{};
            for (int i = 0; i < 15; ++i) shifted[i + 1] = r.coeff[i];
            auto zz = InverseZigzag4x4(shifted);
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j) coeff[i][j] = zz[i][j];
            nnz = r.token.total_coeff;
        }
        cur.luma_nnz[blk] = nnz;
        Coeff4x4 dq = DequantResidual4x4(coeff, cur.qp);
        // DC 位置回填哈达玛结果（raster: blk 的块坐标 -> luma_dc 索引）。
        int dci = kBlk4x4ScanY[blk] * 4 + kBlk4x4ScanX[blk];
        dq[0][0] = dc_scaled[dci];
        Coeff4x4 res = InverseTransform4x4(dq);
        Block4x4 p4;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                p4[y][x] = pred[py - oy + y][px - ox + x];
        addResidual4x4(d.pic.Y, px, py, res, p4);
    }
}

}  // namespace

namespace {

// 解一个 I slice 的全部宏块（slice_data，CAVLC，I slice）。
bool decodeISlice(SliceDecoder& d, std::string* err) {
    std::vector<int>& modes4x4 = d.i4_modes;  // 复用为整帧 4x4 模式网格
    // i4_modes 尺寸是 mbw*mbh*16；但这里我们按整帧 4x4 网格 (mbw*4)*(mbh*4)
    // 索引，需要保证足够大。重建成 (mbw*4)*(mbh*4)。
    modes4x4.assign(static_cast<size_t>(d.mbw * 4) * (d.mbh * 4), -1);

    int total = d.mbw * d.mbh;
    for (int addr = 0; addr < total; ++addr) {
        int mx = addr % d.mbw, my = addr / d.mbw;
        MbInfo& cur = d.mbs[addr];
        cur.qp = d.slice_qp;  // baseline 无 mb_qp_delta 时沿用 slice QP

        uint32_t mb_type = d.br.ReadUE();
        if (getenv("CFS_DBG"))
            std::fprintf(stderr, "mb %d (mx=%d,my=%d) bitpos=%zu mb_type=%u\n",
                         addr, mx, my, d.br.BitPos(), mb_type);
        if (d.br.Overrun()) { *err = "bitstream overrun in slice_data"; return false; }

        cur.decoded = true;
        cur.is_intra = true;

        if (mb_type == 0) {
            // I_4x4。
            cur.is_i16 = false;
            // mb_qp_delta 前先读 CBP。I_4x4 的 intra 预测模式在 CBP 之前？
            // 顺序(7.3.5)：mb_pred(含 16 个 4x4 模式 + chroma 模式) -> CBP ->
            // mb_qp_delta -> residual。
            // 先读 16 个 4x4 模式 + chroma 模式，再读 CBP。
            // 为此把模式读取拆出来：这里先读模式，再读 CBP，再解残差。
            // ------- 读 4x4 模式 -------
            int chosen[16];
            bool trace_modes = getenv("CFS_DBG4") && addr == 0;
            for (int blk = 0; blk < 16; ++blk) {
                int mpm = mostProbableMode(d, modes4x4, mx, my, blk);
                uint32_t flag = d.br.ReadBit();
                int mode;
                if (flag) mode = mpm;
                else {
                    uint32_t rem = d.br.ReadBits(3);
                    mode = (static_cast<int>(rem) < mpm) ? static_cast<int>(rem)
                                                         : static_cast<int>(rem) + 1;
                    if (trace_modes)
                        std::fprintf(stderr, "  blk%d mpm=%d flag=0 rem=%u ->mode%d pos=%zu\n",
                                     blk, mpm, rem, mode, d.br.BitPos());
                }
                if (trace_modes && flag)
                    std::fprintf(stderr, "  blk%d mpm=%d flag=1 ->mode%d pos=%zu\n",
                                 blk, mpm, mode, d.br.BitPos());
                chosen[blk] = mode;
                modes4x4[frame4x4Index(d, mx, my, blk)] = mode;
            }
            if (getenv("CFS_DBG3")) {
                std::fprintf(stderr, "   modes:");
                for (int b = 0; b < 16; ++b) std::fprintf(stderr, " %d", chosen[b]);
                std::fprintf(stderr, "  bitpos=%zu\n", d.br.BitPos());
            }
            int intra_chroma_mode = static_cast<int>(d.br.ReadUE());
            // ------- CBP -------
            uint32_t cbp_code = d.br.ReadUE();
            uint32_t cbp = MapCbp(cbp_code, true);
            if (getenv("CFS_DBG"))
                std::fprintf(stderr, "   I4x4 chroma_mode=%d cbp_code=%u cbp=%u\n",
                             intra_chroma_mode, cbp_code, cbp);
            int cbp_luma = cbp & 0xF;
            int cbp_chroma = (cbp >> 4) & 0x3;
            // ------- mb_qp_delta -------
            if (cbp_luma || cbp_chroma) {
                int dqp = d.br.ReadSE();
                cur.qp = d.slice_qp + dqp;  // 前一宏块 QP=slice QP，叠加 delta
            }
            if (getenv("CFS_DBG3"))
                std::fprintf(stderr, "   residual starts at bitpos=%zu qp=%d\n",
                             d.br.BitPos(), cur.qp);
            // ------- 亮度重建（用已读好的 chosen 模式）-------
            for (int blk = 0; blk < 16; ++blk) {
                int px, py;
                d.luma4x4Origin(mx, my, blk, &px, &py);
                Neighbors4x4 nb = d.gatherLuma4x4(mx, my, blk);
                Block4x4 pred = PredictIntra4x4(
                    nb, static_cast<Intra4x4Mode>(chosen[blk]));
                Coeff4x4 coeff{};
                int nnz = 0;
                int b8 = (kBlk4x4ScanY[blk] / 2) * 2 + (kBlk4x4ScanX[blk] / 2);
                if ((cbp_luma >> b8) & 1) {
                    int nc = deriveLumaNc(d, cur, mx, my, blk);
                    nnz = decodeLumaBlockResidual(d, nc, &coeff);
                }
                cur.luma_nnz[blk] = nnz;
                Coeff4x4 res = ((cbp_luma >> b8) & 1)
                                   ? ReconstructResidual4x4(coeff, cur.qp)
                                   : Coeff4x4{};
                addResidual4x4(d.pic.Y, px, py, res, pred);
            }
            decodeChromaMb(d, cur, mx, my, intra_chroma_mode, cbp_chroma);
            if (d.stats) d.stats->intra4x4_mbs++;
        } else if (mb_type >= 1 && mb_type <= 24) {
            // I_16x16。
            cur.is_i16 = true;
            int pred_mode, cbp_chroma, cbp_luma;
            ParseI16Type(mb_type, &pred_mode, &cbp_chroma, &cbp_luma);
            int intra_chroma_mode = static_cast<int>(d.br.ReadUE());
            if (getenv("CFS_DBG"))
                std::fprintf(stderr, "   I16 pred=%d cbp_luma=%d cbp_chroma=%d chroma_mode=%d\n",
                             pred_mode, cbp_luma, cbp_chroma, intra_chroma_mode);
            // I_16x16 的 mb_qp_delta 总是存在（无论 CBP 是否为 0），因为 DC 系数
            // 永远编码（spec 7.3.5：MbPartPredMode==Intra_16x16 时必读）。
            {
                int dqp = d.br.ReadSE();
                cur.qp = d.slice_qp + dqp;
            }
            // 记录 4x4 模式为 2(DC) 供邻居 mpm（I_16x16 视为 DC）。
            for (int blk = 0; blk < 16; ++blk)
                modes4x4[frame4x4Index(d, mx, my, blk)] = 2;
            decodeI16x16Luma(d, cur, mx, my, pred_mode, cbp_luma);
            decodeChromaMb(d, cur, mx, my, intra_chroma_mode, cbp_chroma);
            if (d.stats) d.stats->intra16x16_mbs++;
        } else if (mb_type == 25) {
            // I_PCM：字节对齐后直接搬原始像素。
            // 对齐到字节边界。
            while (d.br.BitPos() % 8 != 0) d.br.ReadBit();
            int ox = mx * 16, oy = my * 16;
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    d.pic.Y.at(ox + x, oy + y) = static_cast<int>(d.br.ReadBits(8));
            int cx = mx * 8, cy = my * 8;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    d.pic.Cb.at(cx + x, cy + y) = static_cast<int>(d.br.ReadBits(8));
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    d.pic.Cr.at(cx + x, cy + y) = static_cast<int>(d.br.ReadBits(8));
            for (int blk = 0; blk < 16; ++blk) {
                cur.luma_nnz[blk] = 16;
                modes4x4[frame4x4Index(d, mx, my, blk)] = 2;
            }
            if (d.stats) d.stats->ipcm_mbs++;
        } else {
            *err = "unsupported mb_type in I slice";
            return false;
        }
        if (d.stats) {
            d.stats->total_mbs++;
            for (int k = 0; k < 16; ++k) d.stats->luma_coeffs += cur.luma_nnz[k];
        }
    }
    return true;
}

}  // namespace

namespace {

// 对整帧做环路去块滤波（spec 8.7 的整帧调度，简化版：处理宏块边界与内部
// 4x4 边界的亮度；色度处理宏块与 8x8 边界）。I 帧所有块帧内，bS 用 4/3。
void deblockFrame(SliceDecoder& d) {
    Plane& Y = d.pic.Y;
    // 亮度：竖直边界（从左到右每 4 列），再水平边界（每 4 行）。
    auto lumaBs = [&](bool mb_edge) { return mb_edge ? 4 : 3; };
    // 竖直边界。
    for (int my = 0; my < d.mbh; ++my) {
        for (int mx = 0; mx < d.mbw; ++mx) {
            const MbInfo& m = d.mbs[d.mbAddr(mx, my)];
            for (int e = 0; e < 4; ++e) {  // 每宏块 4 条竖直内/边边界
                int xedge = mx * 16 + e * 4;
                if (xedge == 0) continue;  // 帧左边界不滤
                bool mb_edge = (e == 0);
                int bS = m.is_intra ? lumaBs(mb_edge) : 0;
                if (bS == 0) continue;
                int qp = m.qp;
                for (int y = 0; y < 16; ++y) {
                    int gy = my * 16 + y;
                    EdgeSamples s;
                    for (int k = 0; k < 4; ++k) {
                        s.p[k] = Y.get(xedge - 1 - k, gy);
                        s.q[k] = Y.get(xedge + k, gy);
                    }
                    if (DeblockEdge(s, bS, qp, false)) {
                        for (int k = 0; k < 3; ++k) {
                            Y.at(xedge - 1 - k, gy) = s.p[k];
                            Y.at(xedge + k, gy) = s.q[k];
                        }
                    }
                }
            }
        }
    }
    // 水平边界。
    for (int my = 0; my < d.mbh; ++my) {
        for (int mx = 0; mx < d.mbw; ++mx) {
            const MbInfo& m = d.mbs[d.mbAddr(mx, my)];
            for (int e = 0; e < 4; ++e) {
                int yedge = my * 16 + e * 4;
                if (yedge == 0) continue;
                bool mb_edge = (e == 0);
                int bS = m.is_intra ? lumaBs(mb_edge) : 0;
                if (bS == 0) continue;
                int qp = m.qp;
                for (int x = 0; x < 16; ++x) {
                    int gx = mx * 16 + x;
                    EdgeSamples s;
                    for (int k = 0; k < 4; ++k) {
                        s.p[k] = Y.get(gx, yedge - 1 - k);
                        s.q[k] = Y.get(gx, yedge + k);
                    }
                    if (DeblockEdge(s, bS, qp, false)) {
                        for (int k = 0; k < 3; ++k) {
                            Y.at(gx, yedge - 1 - k) = s.p[k];
                            Y.at(gx, yedge + k) = s.q[k];
                        }
                    }
                }
            }
        }
    }
    // 色度：只在宏块边界(8 像素)与 8x8 内部边界(4 像素)滤 p0/q0。
    Plane* cp[2] = {&d.pic.Cb, &d.pic.Cr};
    for (int comp = 0; comp < 2; ++comp) {
        Plane& C = *cp[comp];
        for (int my = 0; my < d.mbh; ++my)
            for (int mx = 0; mx < d.mbw; ++mx) {
                const MbInfo& m = d.mbs[d.mbAddr(mx, my)];
                if (!m.is_intra) continue;
                int qpc = ChromaQp(m.qp, d.pps.chroma_qp_index_offset);
                // 竖直：xedge = mx*8 和 mx*8+4。
                for (int e = 0; e < 2; ++e) {
                    int xedge = mx * 8 + e * 4;
                    if (xedge == 0) continue;
                    int bS = (e == 0) ? 4 : 3;
                    for (int y = 0; y < 8; ++y) {
                        int gy = my * 8 + y;
                        EdgeSamples s;
                        for (int k = 0; k < 4; ++k) {
                            s.p[k] = C.get(std::max(0, xedge - 1 - k), gy);
                            s.q[k] = C.get(std::min(C.w - 1, xedge + k), gy);
                        }
                        if (DeblockEdge(s, bS, qpc, true))
                            { C.at(xedge - 1, gy) = s.p[0]; C.at(xedge, gy) = s.q[0]; }
                    }
                }
                for (int e = 0; e < 2; ++e) {
                    int yedge = my * 8 + e * 4;
                    if (yedge == 0) continue;
                    int bS = (e == 0) ? 4 : 3;
                    for (int x = 0; x < 8; ++x) {
                        int gx = mx * 8 + x;
                        EdgeSamples s;
                        for (int k = 0; k < 4; ++k) {
                            s.p[k] = C.get(gx, std::max(0, yedge - 1 - k));
                            s.q[k] = C.get(gx, std::min(C.h - 1, yedge + k));
                        }
                        if (DeblockEdge(s, bS, qpc, true))
                            { C.at(gx, yedge - 1) = s.p[0]; C.at(gx, yedge) = s.q[0]; }
                    }
                }
            }
    }
}

}  // namespace

namespace {

// 把内部 Picture(int) 转成输出 DecodedFrame(uint8)。
DecodedFrame emitFrame(const Picture& pic, int w, int h, bool is_idr, char st) {
    DecodedFrame f;
    f.width = w; f.height = h; f.is_idr = is_idr; f.slice_type = st;
    f.y.resize(static_cast<size_t>(w) * h);
    f.cb.resize(static_cast<size_t>(w / 2) * (h / 2));
    f.cr.resize(static_cast<size_t>(w / 2) * (h / 2));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            f.y[static_cast<size_t>(y) * w + x] =
                static_cast<uint8_t>(Clip1(pic.Y.get(x, y)));
    for (int y = 0; y < h / 2; ++y)
        for (int x = 0; x < w / 2; ++x) {
            f.cb[static_cast<size_t>(y) * (w / 2) + x] =
                static_cast<uint8_t>(Clip1(pic.Cb.get(x, y)));
            f.cr[static_cast<size_t>(y) * (w / 2) + x] =
                static_cast<uint8_t>(Clip1(pic.Cr.get(x, y)));
        }
    return f;
}

}  // namespace

bool H264Decoder::DecodeAnnexB(const std::vector<uint8_t>& bytes,
                               std::vector<DecodedFrame>* frames,
                               std::string* err) {
    NalSplitResult r = SplitAnnexB(bytes);
    if (!r.ok) { *err = "NAL split failed: " + r.error; return false; }

    for (const auto& u : r.units) {
        if (u.nal_unit_type == kNalSps) {
            sps_ = ParseSps(u.rbsp);
            have_sps_ = sps_.ok;
        } else if (u.nal_unit_type == kNalPps) {
            pps_ = ParsePps(u.rbsp);
            have_pps_ = pps_.ok;
        } else if (u.nal_unit_type == kNalSliceIdr ||
                   u.nal_unit_type == kNalSliceNonIdr) {
            if (!have_sps_ || !have_pps_) continue;
            SliceHeader sh = ParseSliceHeader(u.rbsp, u.nal_unit_type, sps_, pps_);
            if (!sh.ok) {
                // 非 IDR(P)slice header 解析失败时，按"P 帧不在本最小解码器范围"
                // 处理：统计后跳过，不中断整段解码（IDR 已能独立解出）。
                if (u.nal_unit_type == kNalSliceNonIdr) { stats_.p_frames++; continue; }
                *err = "slice header parse failed: " + sh.error;
                return false;
            }

            int w = static_cast<int>(sps_.CodedWidth());
            int h = static_cast<int>(sps_.CodedHeight());
            int mbw = static_cast<int>(sps_.PicWidthInMbs());
            int mbh = static_cast<int>(sps_.FrameHeightInMbs());

            Picture pic; pic.alloc(w, h);
            RbspBitReader br(u.rbsp.data() + 1, u.rbsp.size() - 1);
            br.SkipBits(sh.header_bit_size);

            // C3 的 slice header 解析停在 slice_qp_delta。但当 PPS 的
            // deblocking_filter_control_present_flag 置位时，slice header 后面
            // 还有去块滤波控制字段（spec 7.3.3）。这几段必须在此读掉，slice_data
            // 才真正开始——否则宏块数据会整体错位。
            int deblock_disable_idc = 0;
            int slice_alpha_off = 0, slice_beta_off = 0;
            if (pps_.deblocking_filter_control_present_flag) {
                deblock_disable_idc = static_cast<int>(br.ReadUE());
                if (deblock_disable_idc != 1) {
                    slice_alpha_off = br.ReadSE() * 2;
                    slice_beta_off = br.ReadSE() * 2;
                }
            }
            (void)slice_alpha_off; (void)slice_beta_off;

            int qp = sh.SliceQp(pps_);
            SliceDecoder dec(sps_, pps_, sh, br, pic, mbw, mbh, qp, &stats_);

            if (sh.category == SliceCategory::kI) {
                bool okdec = decodeISlice(dec, err);
                if (getenv("CFS_EMIT_PARTIAL")) {
                    // 调试用：即便解码中途失败，也把已重建部分输出，便于逐 MB 对齐。
                    deblockFrame(dec);
                    frames->push_back(emitFrame(pic, w, h, sh.is_idr, 'I'));
                    stats_.frames++;
                    if (sh.is_idr) stats_.idr_frames++;
                    continue;
                }
                if (!okdec) return false;
                deblockFrame(dec);
                frames->push_back(emitFrame(pic, w, h, sh.is_idr, 'I'));
                stats_.frames++;
                if (sh.is_idr) stats_.idr_frames++;
            } else if (sh.category == SliceCategory::kP) {
                // P 帧：本最小解码器未逐一抠平帧间宏块层的全部边界。为不产生
                // 误导性的"看似解码"输出，这里明确跳过 P 帧解码，由上层统计并
                // 在报告中如实说明范围。参考帧管理与运动补偿模块已具备，完整
                // 打通留作后续工作。
                stats_.p_frames++;
            } else {
                *err = "unsupported slice category (only I/P baseline)";
                return false;
            }
        }
    }
    return true;
}

bool AppendFrameToYuv(const DecodedFrame& f, std::FILE* fp) {
    if (!fp) return false;
    size_t ny = f.y.size(), nc = f.cb.size();
    if (std::fwrite(f.y.data(), 1, ny, fp) != ny) return false;
    if (std::fwrite(f.cb.data(), 1, nc, fp) != nc) return false;
    if (std::fwrite(f.cr.data(), 1, nc, fp) != nc) return false;
    return true;
}

}  // namespace cfs
