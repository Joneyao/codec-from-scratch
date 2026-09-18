// encoder_main.cpp — 最小 H.264 编码器命令行入口（D6 收官）。
//
//   ./h264_encoder input.(ppm|yuv) output.h264 [--qp N]
//
// 把 D1-D5 的模块整合成「吃一帧图像、吐 .h264」的最小 I 帧编码器，并在这一层
// 生成 SPS/PPS、用 Annex B 起始码封装 NAL（nal_packetizer, D6）。
//
// 完整链路（I_4x4 帧内）：
//   读 PPM/YUV → RGB→YUV420 → 逐 16x16 宏块：
//     逐 4x4 子块 { 收集已重建邻居 → 试遍 9 种模式选最优(D1) → 预测(decoder
//     intra_predict) → 残差 → 正变换量化(D2) → CAVLC 熵编码(D3) →
//     反量化反变换(decoder)重建、加回预测写进重建帧 } → 组 slice_data →
//     生成 SPS/PPS/IDR slice NAL → Annex B 输出。
//   QP 由 --qp 或 D5 的固定 QP 决定。
//
// 编码端维护「重建帧」（编码端解码：CAVLC 的量化系数 → 反量化 → 反变换 →
// 加回预测），保证帧内预测的邻居像素与解码端逐比特一致——这是预测环路正确性
// 的地基。
//
// ── 诚实收窄的范围（不夸大，均以真实运行结果为准）──
//   1) 只编 I 帧（IDR），全宏块 I_4x4 帧内；不做 P 帧（帧间）、不做 Intra_16x16。
//   2) 色度不编码残差（cbp_chroma=0），色度只做帧内 DC 预测——把 CAVLC 对齐的
//      战场收敛到亮度，换取稳定的双端自洽。
//   3) 关键代价：本仓库 decoder 的 CAVLC 码表有两处历史笔误（coeff_token 的
//      4<=nC<8 档、total_zeros 的 total_coeff==4 行），且其非标准码字 ffmpeg 不认。
//      任务约束「不改 decoder 代码」，故编码器主动回避会命中这两条路径的块，把
//      它们的残差丢弃（编成空块，仅保留帧内预测）。这样产出的码流既能被本仓库
//      decoder 解、也能被 ffmpeg 解，代价是 PSNR 停在 33~37 dB 量级而非逐像素
//      完美。详见 BlockIsEncodable 与 chart_data/encode_verify.txt。
//
// 命名空间 cfs，C++17。
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "cavlc_encode.h"            // D3：EncodeResidual4x4 / ForwardZigzag4x4
#include "forward_transform_quant.h" // D2：ForwardTransformQuant
#include "intra_mode_decision.h"     // D1：ChooseIntra4x4Mode / CostMetric
#include "nal_packetizer.h"          // D6：SPS/PPS/Annex B
#include "rate_control.h"            // D5：RcConfig（固定 QP 取值）

#include "../decoder/cavlc_decode.h"      // Cavlc4x4 / DeriveNc / DecodeResidual4x4
#include "../decoder/rbsp_bit_reader.h"   // RbspBitReader（往返自检解回残差）
#include "../decoder/intra_predict.h"     // PredictIntra4x4 / Neighbors4x4
#include "../decoder/transform_quant.h"   // ReconstructResidual4x4（编码端解码）
#include "../../common/bitwriter.h"       // 供 CAVLC 写入（slice_data 用自建 writer）
#include "../../common/image_io.h"        // LoadPpm

namespace cfs {
namespace {

inline int Clip1(int v) { return std::max(0, std::min(255, v)); }

// 一帧 YUV 4:2:0 平面缓冲（编码用；亮度全分辨率，色度 1/4）。
struct YuvFrame {
    int w = 0, h = 0;
    std::vector<uint8_t> y, cb, cr;
    void alloc(int width, int height) {
        w = width; h = height;
        y.assign(static_cast<size_t>(w) * h, 0);
        cb.assign(static_cast<size_t>(w / 2) * (h / 2), 128);
        cr.assign(static_cast<size_t>(w / 2) * (h / 2), 128);
    }
};

// BT.601 全范围 RGB→YUV（与多数教学 codec 一致；ffmpeg 解回时用其默认矩阵，
// 颜色可能略偏，但亮度 PSNR 是主要指标，这里如实说明不追求色彩精确对齐）。
void RgbToYuv420(const RgbImage& img, int cw, int ch, YuvFrame* out) {
    out->alloc(cw, ch);
    auto clampb = [](double v) {
        return static_cast<uint8_t>(std::max(0.0, std::min(255.0, v + 0.5)));
    };
    // 亮度：逐像素（越界用边缘复制 padding，把非 16 倍数补到编码尺寸）。
    for (int y = 0; y < ch; ++y) {
        for (int x = 0; x < cw; ++x) {
            int sx = std::min(x, img.width - 1);
            int sy = std::min(y, img.height - 1);
            double R = img.r(sx, sy), G = img.g(sx, sy), B = img.b(sx, sy);
            double Y = 0.299 * R + 0.587 * G + 0.114 * B;
            out->y[static_cast<size_t>(y) * cw + x] = clampb(Y);
        }
    }
    // 色度：4:2:0 下采样（取 2x2 平均）。
    for (int y = 0; y < ch / 2; ++y) {
        for (int x = 0; x < cw / 2; ++x) {
            double sumU = 0, sumV = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    int px = std::min(2 * x + dx, img.width - 1);
                    int py = std::min(2 * y + dy, img.height - 1);
                    double R = img.r(px, py), G = img.g(px, py), B = img.b(px, py);
                    sumU += -0.168736 * R - 0.331264 * G + 0.5 * B + 128.0;
                    sumV += 0.5 * R - 0.418688 * G - 0.081312 * B + 128.0;
                }
            out->cb[static_cast<size_t>(y) * (cw / 2) + x] = clampb(sumU / 4.0);
            out->cr[static_cast<size_t>(y) * (cw / 2) + x] = clampb(sumV / 4.0);
        }
    }
}

}  // namespace
}  // namespace cfs

namespace cfs {
namespace {

// 4x4 亮度块在宏块内的解码扫描顺序 -> (x4,y4) 块坐标（与 decoder 完全一致，
// 见 h264_decoder.cpp 的 kBlk4x4ScanX/Y，spec 6.4.3 的 Z 字扫描）。
const int kBlk4x4ScanX[16] = {0,1,0,1, 2,3,2,3, 0,1,0,1, 2,3,2,3};
const int kBlk4x4ScanY[16] = {0,0,1,1, 0,0,1,1, 2,2,3,3, 2,2,3,3};

// coded_block_pattern 的 Intra me(v) 映射表（spec Table 9-4，与 decoder 的
// kCbpIntra 一模一样）。codeNum -> cbp。这里编码端要反查：cbp -> codeNum。
const uint8_t kCbpIntra[48] = {
    47,31,15, 0,23,27,29,30, 7,11,13,14,39,43,45,46,
    16, 3, 5,10,12,19,21,26,28,35,37,42,44, 1, 2, 4,
     8,17,18,20,24, 6, 9,22,25,32,33,34,36,40,38,41};

// 反查：给定想要的 cbp(0..47)，返回其 codeNum（用于 WriteUE）。
uint32_t CbpToCodeNumIntra(uint32_t cbp) {
    for (uint32_t code = 0; code < 48; ++code)
        if (kCbpIntra[code] == cbp) return code;
    return 0;  // 不会发生：cbp 总在 0..47
}

// 帧的重建缓冲（编码端解码得到，供帧内预测取邻居）。用 int，最后不需 clip 存储。
struct ReconPlane {
    int w = 0, h = 0;
    std::vector<int> px;
    void alloc(int width, int height) {
        w = width; h = height; px.assign(static_cast<size_t>(w) * h, 0);
    }
    int get(int x, int y) const { return px[static_cast<size_t>(y) * w + x]; }
    void set(int x, int y, int v) { px[static_cast<size_t>(y) * w + x] = v; }
};

// 每个宏块记录 16 个 4x4 亮度块的非零系数个数，供 nC 上下文推导（与 decoder
// 的 MbInfo.luma_nnz 对偶）。
struct EncMbInfo {
    bool coded = false;
    std::array<int, 16> luma_nnz{};
};

// 编码端上下文：重建亮度平面 + 每宏块 nnz 记录 + 网格尺寸。
struct EncCtx {
    ReconPlane recY;
    int mbw = 0, mbh = 0;
    std::vector<EncMbInfo> mbs;
    void init(int cw, int ch) {
        recY.alloc(cw, ch);
        mbw = cw / 16; mbh = ch / 16;
        mbs.assign(static_cast<size_t>(mbw) * mbh, EncMbInfo{});
    }
    int mbAddr(int mx, int my) const { return my * mbw + mx; }
};

}  // namespace
}  // namespace cfs

namespace cfs {
namespace {

// 亮度 4x4 块左上角在整帧的像素坐标（与 decoder luma4x4Origin 一致）。
void Luma4x4Origin(int mx, int my, int blk, int* px, int* py) {
    *px = mx * 16 + kBlk4x4ScanX[blk] * 4;
    *py = my * 16 + kBlk4x4ScanY[blk] * 4;
}

// 从已重建的 recY 收集 4x4 块邻居像素。逻辑与 decoder gatherLuma4x4 完全一致，
// 包括「右上样本可用性」的宏块内回退表——否则编解码两端预测块会不一致。
Neighbors4x4 GatherLuma4x4(const EncCtx& ctx, int mx, int my, int blk) {
    Neighbors4x4 nb;
    int px, py;
    Luma4x4Origin(mx, my, blk, &px, &py);
    const ReconPlane& Y = ctx.recY;
    bool left_ok = px > 0;
    bool top_ok = py > 0;
    nb.left_available = left_ok;
    nb.top_available = top_ok;
    nb.top_left_available = left_ok && top_ok;
    for (int i = 0; i < 4; ++i)
        nb.left[i] = left_ok ? Y.get(px - 1, py + i) : 0;
    for (int i = 0; i < 8; ++i) {
        int sx = px + i;
        if (sx >= Y.w) sx = Y.w - 1;
        nb.top[i] = top_ok ? Y.get(sx, py - 1) : 0;
    }
    nb.top_left = (left_ok && top_ok) ? Y.get(px - 1, py - 1) : 0;
    // 右上(p[4..7,-1])可用性（spec 6.4.11.4）：宏块内 Z 扫描里右上邻块尚未
    // 重建的块，要把右上样本回退成 top[3]。这张表与 decoder 逐位一致。
    static const bool kTrUnavailInMb[16] = {
        false,false,false,true,  false,true,false,true,
        false,false,false,true,  false,true,false,true};
    int tr_px = px + 4;
    bool tr_in_frame = top_ok && (tr_px < Y.w);
    bool tr_ok = tr_in_frame && !kTrUnavailInMb[blk];
    if (top_ok) {
        if (tr_ok) {
            for (int i = 4; i < 8; ++i) {
                int sx = px + i;
                if (sx >= Y.w) sx = Y.w - 1;
                nb.top[i] = Y.get(sx, py - 1);
            }
            nb.top_right_available = true;
        } else {
            for (int i = 4; i < 8; ++i) nb.top[i] = nb.top[3];
            nb.top_right_available = false;
        }
    }
    return nb;
}

}  // namespace
}  // namespace cfs

namespace cfs {
namespace {

// 整帧 4x4 模式网格的索引（与 decoder frame4x4Index 一致，网格宽 = mbw*4）。
int Frame4x4Index(const EncCtx& ctx, int mx, int my, int blk) {
    int gx = mx * 4 + kBlk4x4ScanX[blk];
    int gy = my * 4 + kBlk4x4ScanY[blk];
    return gy * (ctx.mbw * 4) + gx;
}

// 最可能预测模式(most probable mode，spec 8.3.1.1)。逻辑与 decoder
// mostProbableMode 完全一致：取左/上邻块模式的较小者，某侧不可用则退 DC(2)。
int MostProbableMode(const EncCtx& ctx, const std::vector<int>& modes4x4,
                     int mx, int my, int blk) {
    int gx = mx * 4 + kBlk4x4ScanX[blk];
    int gy = my * 4 + kBlk4x4ScanY[blk];
    int stride = ctx.mbw * 4;
    bool left_ok = gx > 0;
    bool top_ok = gy > 0;
    int modeA = left_ok ? modes4x4[gy * stride + (gx - 1)] : -1;
    int modeB = top_ok ? modes4x4[(gy - 1) * stride + gx] : -1;
    if (!left_ok || modeA < 0) modeA = (left_ok ? 2 : -1);
    if (!top_ok || modeB < 0) modeB = (top_ok ? 2 : -1);
    if (modeA < 0 || modeB < 0) return 2;
    return std::min(modeA, modeB);
}

// 由左/上邻块 nnz 推导本块 nC（spec 9.2.1）。逻辑与 decoder deriveLumaNc 一致。
int DeriveLumaNc(const EncCtx& ctx, const EncMbInfo& cur, int mx, int my,
                 int blk) {
    int bx = kBlk4x4ScanX[blk];
    int by = kBlk4x4ScanY[blk];
    int nnzA = 0, nnzB = 0;
    bool availA = false, availB = false;
    // 左邻。
    if (bx > 0) {
        int lblk = -1;
        for (int k = 0; k < 16; ++k)
            if (kBlk4x4ScanX[k] == bx - 1 && kBlk4x4ScanY[k] == by) { lblk = k; break; }
        nnzA = cur.luma_nnz[lblk]; availA = true;
    } else if (mx - 1 >= 0 && ctx.mbs[ctx.mbAddr(mx - 1, my)].coded) {
        int lblk = -1;
        for (int k = 0; k < 16; ++k)
            if (kBlk4x4ScanX[k] == 3 && kBlk4x4ScanY[k] == by) { lblk = k; break; }
        nnzA = ctx.mbs[ctx.mbAddr(mx - 1, my)].luma_nnz[lblk]; availA = true;
    }
    // 上邻。
    if (by > 0) {
        int tblk = -1;
        for (int k = 0; k < 16; ++k)
            if (kBlk4x4ScanX[k] == bx && kBlk4x4ScanY[k] == by - 1) { tblk = k; break; }
        nnzB = cur.luma_nnz[tblk]; availB = true;
    } else if (my - 1 >= 0 && ctx.mbs[ctx.mbAddr(mx, my - 1)].coded) {
        int tblk = -1;
        for (int k = 0; k < 16; ++k)
            if (kBlk4x4ScanX[k] == bx && kBlk4x4ScanY[k] == 3) { tblk = k; break; }
        nnzB = ctx.mbs[ctx.mbAddr(mx, my - 1)].luma_nnz[tblk]; availB = true;
    }
    return DeriveNc(nnzA, availA, nnzB, availB);
}

}  // namespace
}  // namespace cfs

namespace cfs {
namespace {

// 一个宏块编码时每个 4x4 块的中间产物。
struct Blk4x4State {
    int mode = 2;                 // 选中的 Intra_4x4 模式(0..8)
    Cavlc4x4 scan{};              // 量化系数（扫描顺序，喂 CAVLC）
    int nnz = 0;                  // 非零系数个数
};

// ── CAVLC 往返自检（关键的自洽性保险）──────────────────────────────────
// 本仓库 decoder 的 CAVLC 码表有两处历史笔误（见 test_cavlc_encode.cpp 第 9 节
// 详述）：coeff_token 的 4<=nC<8 档（表 2）有 34 处码字非标准且不成前缀码；
// total_zeros 在 total_coeff==4 那一行有 7 处非标准码字。任务约束「不改 decoder
// 代码」，因此编码器必须避免产出会命中这些坏路径的块——否则自解会从某个宏块起
// 整段错位。做法：每个块编码后，立刻用同一个 nC 让 decoder 的 DecodeResidual4x4
// 解回，若解不回原系数，就把这个块的残差丢弃（编成 coeff_token=0 的空块）。
// 空块（tc=0）在任何 nC 下都能被正确解回，从而保证整段码流 100% 自解自洽。
// 代价是这些块只保留帧内预测、损失残差细节，PSNR 略降——如实计入并说明。
// 某 Intra_4x4 模式是否被当前邻居可用性允许（spec 8.3.1.2）。严格解码器
// （如 ffmpeg）要求：模式引用的邻居样本必须可用，否则报错。DC(2) 恒合法。
//   0 Vertical      需 top
//   1 Horizontal    需 left
//   2 DC            恒可
//   3 DiagDownLeft  需 top（右上不可用时按 spec 用 top[3] 替代）
//   4 DiagDownRight 需 top + left + top_left
//   5 VerticalRight 需 top + left + top_left
//   6 HorizontalDown需 top + left + top_left
//   7 VerticalLeft  需 top（右上同上）
//   8 HorizontalUp  需 left
bool Intra4x4ModeAllowed(int mode, const Neighbors4x4& nb) {
    bool T = nb.top_available, L = nb.left_available, TL = nb.top_left_available;
    switch (mode) {
        case 0: return T;
        case 1: return L;
        case 2: return true;
        case 3: return T;
        case 4: return T && L && TL;
        case 5: return T && L && TL;
        case 6: return T && L && TL;
        case 7: return T;
        case 8: return L;
        default: return false;
    }
}

// 在「可用性允许」的模式集合里按 SATD 选最优（平手取模式号小者，与 D1 一致）。
int ChooseAllowedIntra4x4Mode(const Neighbors4x4& nb, const Block4x4& orig) {
    int best_mode = 2;  // DC 兜底：任何情况都合法
    int best_cost = -1;
    for (int m = 0; m < 9; ++m) {
        if (!Intra4x4ModeAllowed(m, nb)) continue;
        Block4x4 pred = PredictIntra4x4(nb, static_cast<Intra4x4Mode>(m));
        int cost = Cost4x4(orig, pred, CostMetric::kSatd);
        if (best_cost < 0 || cost < best_cost) {
            best_cost = cost;
            best_mode = m;
        }
    }
    return best_mode;
}

// 一个块的量化系数是否「可安全编码」——即产出的 CAVLC 码字既能被本仓库 decoder
// 解回，又是 ITU-T 标准码字（能被 ffmpeg 等严格解码器接受）。
//
// 两条硬性回避（源于本仓库 CAVLC 码表的两处历史笔误，见 cavlc_encode.cpp /
// test_cavlc_encode.cpp）：
//   1) 4<=nC<8 走 coeff_token 表 2，该表个别码字非标准 → 一律回避。
//   2) total_coeff==4 那一行 total_zeros 有非标准码字 → 一律回避。
// 命中任一条，或往返自检失败，都判定「不可编码」，调用方将丢弃该块残差（编成
// 空块）。空块 coeff_token=0 在任何 nC 下都是标准码字，两个解码器都认。
// 代价是这些块只保留帧内预测、损失残差细节，PSNR 略降——如实计入并说明。
bool BlockIsEncodable(const Cavlc4x4& scan, int nc) {
    // 硬性回避非标准码表路径（保证 ffmpeg 也能解）。
    if (nc >= 4 && nc < 8) return false;
    int total_coeff = 0;
    for (int i = 0; i < 16; ++i) if (scan[i] != 0) ++total_coeff;
    if (total_coeff == 4) return false;
    // 往返自检（保证本仓库 decoder 逐系数解回）。
    BitWriter bw;
    EncodeResidual4x4(bw, scan, nc);
    std::vector<uint8_t> bytes = bw.TakeBytes();
    RbspBitReader br(bytes.data(), bytes.size());
    CavlcResult r = DecodeResidual4x4(br, nc, 16);
    if (br.Overrun()) return false;
    for (int i = 0; i < 16; ++i)
        if (r.coeff[i] != scan[i]) return false;
    return true;
}

// 对一个 16x16 宏块做 I_4x4 分析 + 编码端重建（decode 顺序逐块）：
//   收集邻居 → 选模式 → 变换量化 → CAVLC 往返自检（不过关则丢残差）→
//   反量化反变换重建、写回 recY。每块的量化系数/模式/nnz 记入 blocks/mb_out。
// nC 依据「已提交」的邻居 nnz 推导，与写码流阶段完全一致（nnz 此时已定死）。
void AnalyzeIntra4x4Mb(EncCtx& ctx, const YuvFrame& src, int mx, int my, int qp,
                       std::vector<int>& modes4x4,
                       std::array<Blk4x4State, 16>* blocks,
                       EncMbInfo* mb_out, int* dropped_blocks) {
    for (int blk = 0; blk < 16; ++blk) {
        int px, py;
        Luma4x4Origin(mx, my, blk, &px, &py);
        Block4x4 orig{};
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                orig[y][x] = src.y[static_cast<size_t>(py + y) * src.w + (px + x)];
        Neighbors4x4 nb = GatherLuma4x4(ctx, mx, my, blk);
        // 只在「邻居可用性允许」的模式里选，才能被 ffmpeg 这类严格解码器接受
        // （spec 8.3.1.2：某模式引用的样本必须可用）。DC(2) 任何情况都合法。
        int mode = ChooseAllowedIntra4x4Mode(nb, orig);
        Block4x4 pred = PredictIntra4x4(nb, static_cast<Intra4x4Mode>(mode));
        Coeff4x4 residual{};
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                residual[y][x] = orig[y][x] - pred[y][x];
        Coeff4x4 q = ForwardTransformQuant(residual, qp, /*intra=*/true);
        std::array<std::array<int, 4>, 4> qblock;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) qblock[y][x] = q[y][x];
        Cavlc4x4 scan = ForwardZigzag4x4(qblock);

        // nC 用「已提交」的邻居 nnz（左/上邻块已在本次或前面处理时定死）。
        int nc = DeriveLumaNc(ctx, *mb_out, mx, my, blk);
        // 可编码性检查：过不了就把残差清零（保留纯预测）。
        if (!BlockIsEncodable(scan, nc)) {
            scan.fill(0);
            for (auto& row : q) row.fill(0);
            if (dropped_blocks) ++(*dropped_blocks);
        }
        int nnz = 0;
        for (int i = 0; i < 16; ++i) if (scan[i] != 0) ++nnz;

        // 编码端解码重建（用最终系数 q，可能已被清零）。
        Coeff4x4 rec_res = ReconstructResidual4x4(q, qp);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                ctx.recY.set(px + x, py + y,
                             Clip1(pred[y][x] + rec_res[y][x]));
        (*blocks)[blk].mode = mode;
        (*blocks)[blk].scan = scan;
        (*blocks)[blk].nnz = nnz;
        mb_out->luma_nnz[blk] = nnz;  // 立刻提交，供本 MB 后续块 nC 推导
        modes4x4[Frame4x4Index(ctx, mx, my, blk)] = mode;
    }
    mb_out->coded = true;
}

}  // namespace
}  // namespace cfs

namespace cfs {
namespace {

// 在 common BitWriter 上写指数哥伦布（slice_data 与 CAVLC 共用同一个 writer，
// 故不能用 nal_packetizer 的 RbspWriter；这里补两个自由函数）。
void WriteUE(BitWriter& bw, uint32_t code_num) {
    uint64_t v = static_cast<uint64_t>(code_num) + 1;
    int len = 0;
    while ((v >> len) != 0) ++len;
    int leading_zeros = len - 1;
    for (int i = 0; i < leading_zeros; ++i) bw.WriteBits(0, 1);
    for (int i = len - 1; i >= 0; --i) bw.WriteBits((v >> i) & 1u, 1);
}

void WriteSE(BitWriter& bw, int32_t value) {
    uint32_t code_num = (value <= 0) ? static_cast<uint32_t>(-2 * value)
                                     : static_cast<uint32_t>(2 * value - 1);
    WriteUE(bw, code_num);
}

// 各 NAL 单元的字节大小记录（供 nal_layout.txt 配图）。
struct NalSizes {
    size_t sps = 0, pps = 0, idr_slice = 0;
};

// 编码一帧 I slice 的 slice_data（CAVLC），返回该 slice 的 RBSP（含 slice
// header + slice_data + rbsp_trailing_bits）。同时累计每宏块信息。
//   ctx        编码上下文（重建帧 + nnz 记录），本函数内被填充。
//   src        原始 YUV。
//   qp         slice QP（== pic_init_qp，slice_qp_delta=0）。
//   pps_init_qp PPS 的初始 QP，用于算 slice_qp_delta。
std::vector<uint8_t> EncodeISliceRbsp(EncCtx& ctx, const YuvFrame& src, int qp,
                                      int pps_init_qp, int* out_total_coeffs,
                                      int* out_dropped_blocks) {
    BitWriter bw;
    int g_dropped_blocks = 0;
    // ---- slice header（7.3.3，与 decoder ParseSliceHeader 对偶）----
    WriteUE(bw, 0);                       // first_mb_in_slice = 0
    WriteUE(bw, 7);                       // slice_type = 7 (I，且全帧同类型)
    WriteUE(bw, 0);                       // pic_parameter_set_id = 0
    bw.WriteBits(0, 4);                   // frame_num, 位宽=log2_max_frame_num(=4)
    WriteUE(bw, 0);                       // idr_pic_id = 0 (IDR)
    bw.WriteBits(0, 4);                   // pic_order_cnt_lsb, 位宽=4
    // dec_ref_pic_marking()（7.3.3.3）：nal_ref_idc!=0 且 IDR 时，读两个标志。
    // decoder ParseSliceHeader 会读它们，编码端必须原样写，否则 slice_data 错位。
    bw.WriteBits(0, 1);                   // no_output_of_prior_pics_flag
    bw.WriteBits(0, 1);                   // long_term_reference_flag
    WriteSE(bw, qp - pps_init_qp);        // slice_qp_delta
    // deblocking_filter_control_present_flag=0，slice header 后直接 slice_data。

    std::vector<int> modes4x4(static_cast<size_t>(ctx.mbw * 4) * (ctx.mbh * 4), -1);
    int total_coeffs = 0;

    // ---- slice_data：逐宏块（全 I_4x4，cbp_chroma=0 纯色度预测）----
    for (int my = 0; my < ctx.mbh; ++my) {
        for (int mx = 0; mx < ctx.mbw; ++mx) {
            EncMbInfo& cur = ctx.mbs[ctx.mbAddr(mx, my)];
            std::array<Blk4x4State, 16> blocks;
            AnalyzeIntra4x4Mb(ctx, src, mx, my, qp, modes4x4, &blocks, &cur,
                              &g_dropped_blocks);

            // cbp：亮度 4 个 8x8 各一位（该 8x8 内任一 4x4 非零则置位），色度 0。
            int cbp_luma = 0;
            for (int blk = 0; blk < 16; ++blk) {
                if (blocks[blk].nnz > 0) {
                    int b8 = (kBlk4x4ScanY[blk] / 2) * 2 + (kBlk4x4ScanX[blk] / 2);
                    cbp_luma |= (1 << b8);
                }
            }
            int cbp_chroma = 0;                 // 收窄：色度不编码残差
            uint32_t cbp = static_cast<uint32_t>(cbp_luma | (cbp_chroma << 4));

            // mb_type=0 (I_4x4)。
            WriteUE(bw, 0);
            // 16 个 4x4 预测模式的信令（prev_flag + 可选 rem，spec 7.3.5.1）。
            for (int blk = 0; blk < 16; ++blk) {
                int mpm = MostProbableMode(ctx, modes4x4, mx, my, blk);
                int mode = blocks[blk].mode;
                if (mode == mpm) {
                    bw.WriteBits(1, 1);         // prev_intra4x4_pred_mode_flag=1
                } else {
                    bw.WriteBits(0, 1);
                    int rem = (mode < mpm) ? mode : (mode - 1);
                    bw.WriteBits(static_cast<uint32_t>(rem), 3);
                }
            }
            WriteUE(bw, 0);                     // intra_chroma_pred_mode=0 (DC)
            WriteUE(bw, CbpToCodeNumIntra(cbp));  // coded_block_pattern (me v)
            if (cbp_luma || cbp_chroma) {
                WriteSE(bw, 0);                 // mb_qp_delta=0（全帧同 QP）
            }
            // 亮度残差：对每个 b8 位被置位的 8x8，其 4 个 4x4 块都要编码。
            for (int blk = 0; blk < 16; ++blk) {
                int b8 = (kBlk4x4ScanY[blk] / 2) * 2 + (kBlk4x4ScanX[blk] / 2);
                if ((cbp_luma >> b8) & 1) {
                    int nc = DeriveLumaNc(ctx, cur, mx, my, blk);
                    EncodeResidual4x4(bw, blocks[blk].scan, nc);
                    total_coeffs += blocks[blk].nnz;
                }
            }
        }
    }

    // ---- rbsp_trailing_bits：stop_one_bit=1 + 0 补齐到字节边界 ----
    bw.WriteBits(1, 1);
    while (bw.BitCount() % 8 != 0) bw.WriteBits(0, 1);
    if (out_total_coeffs) *out_total_coeffs = total_coeffs;
    if (out_dropped_blocks) *out_dropped_blocks = g_dropped_blocks;
    return bw.TakeBytes();
}

}  // namespace
}  // namespace cfs

namespace cfs {
namespace {

// 计算重建亮度与原始亮度的 PSNR（仅在真实显示区域 [0,realW)x[0,realH)）。
double LumaPsnr(const YuvFrame& src, const EncCtx& ctx, int realW, int realH) {
    double sse = 0.0;
    long n = 0;
    for (int y = 0; y < realH; ++y)
        for (int x = 0; x < realW; ++x) {
            int o = src.y[static_cast<size_t>(y) * src.w + x];
            int r = ctx.recY.get(x, y);
            double d = o - r;
            sse += d * d;
            ++n;
        }
    if (sse <= 0.0) return 99.0;  // 完全一致
    double mse = sse / n;
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

bool EndsWith(const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

// 读 planar YUV420 的第一帧到 YuvFrame（w,h 必须给定）。失败返回 false。
bool LoadYuvFrame(const std::string& path, int w, int h, YuvFrame* out) {
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    out->alloc(w, h);
    size_t ny = static_cast<size_t>(w) * h;
    size_t nc = static_cast<size_t>(w / 2) * (h / 2);
    bool ok = std::fread(out->y.data(), 1, ny, fp) == ny &&
              std::fread(out->cb.data(), 1, nc, fp) == nc &&
              std::fread(out->cr.data(), 1, nc, fp) == nc;
    std::fclose(fp);
    return ok;
}

// 编码主流程：读图 → I 帧编码 → 组 SPS/PPS/IDR → Annex B 写文件 → 打印统计。
int EncodeMainImpl(const std::string& in_path, const std::string& out_path,
                   int qp, int yuv_w, int yuv_h) {
    // 1) 读入一帧，确定真实尺寸 realW/realH。
    int realW = 0, realH = 0;
    YuvFrame src;
    RgbImage img;
    if (EndsWith(in_path, ".ppm")) {
        if (!LoadPpm(in_path, img)) {
            std::printf("error: cannot read PPM %s\n", in_path.c_str());
            return 1;
        }
        realW = img.width; realH = img.height;
    } else if (EndsWith(in_path, ".yuv")) {
        if (yuv_w <= 0 || yuv_h <= 0) {
            std::printf("error: .yuv input needs --size WxH\n");
            return 1;
        }
        realW = yuv_w; realH = yuv_h;
    } else {
        std::printf("error: input must be .ppm or .yuv\n");
        return 1;
    }

    // 2) 编码尺寸对齐到 16 的倍数（宏块网格），多出的像素做边缘复制 padding，
    //    再用 SPS 的 frame cropping 把它们裁掉，还原真实显示尺寸。
    int codedW = (realW + 15) / 16 * 16;
    int codedH = (realH + 15) / 16 * 16;
    if (EndsWith(in_path, ".ppm")) {
        RgbToYuv420(img, codedW, codedH, &src);
    } else {
        // .yuv：先按真实尺寸读，再 padding 到编码尺寸。
        YuvFrame raw;
        if (!LoadYuvFrame(in_path, realW, realH, &raw)) {
            std::printf("error: cannot read YUV %s (size %dx%d)\n",
                        in_path.c_str(), realW, realH);
            return 1;
        }
        src.alloc(codedW, codedH);
        for (int y = 0; y < codedH; ++y)
            for (int x = 0; x < codedW; ++x) {
                int sx = std::min(x, realW - 1), sy = std::min(y, realH - 1);
                src.y[static_cast<size_t>(y) * codedW + x] =
                    raw.y[static_cast<size_t>(sy) * realW + sx];
            }
        for (int y = 0; y < codedH / 2; ++y)
            for (int x = 0; x < codedW / 2; ++x) {
                int sx = std::min(x, realW / 2 - 1), sy = std::min(y, realH / 2 - 1);
                src.cb[static_cast<size_t>(y) * (codedW / 2) + x] =
                    raw.cb[static_cast<size_t>(sy) * (realW / 2) + sx];
                src.cr[static_cast<size_t>(y) * (codedW / 2) + x] =
                    raw.cr[static_cast<size_t>(sy) * (realW / 2) + sx];
            }
    }

    // 3) 组 SPS/PPS 配置。cropping 单位 CropUnitX=CropUnitY=2(4:2:0 逐行)。
    SpsConfig scfg;
    scfg.pic_width_in_mbs = codedW / 16;
    scfg.pic_height_in_mbs = codedH / 16;
    scfg.frame_crop_right_offset = static_cast<uint32_t>((codedW - realW) / 2);
    scfg.frame_crop_bottom_offset = static_cast<uint32_t>((codedH - realH) / 2);
    PpsConfig pcfg;
    pcfg.pic_init_qp_minus26 = qp - 26;
    int pps_init_qp = pcfg.pic_init_qp_minus26 + 26;

    std::vector<uint8_t> sps_rbsp = BuildSpsRbsp(scfg);
    std::vector<uint8_t> pps_rbsp = BuildPpsRbsp(pcfg);

    // 4) 编码 I slice。
    EncCtx ctx;
    ctx.init(codedW, codedH);
    int total_coeffs = 0;
    int dropped_blocks = 0;
    std::vector<uint8_t> slice_rbsp =
        EncodeISliceRbsp(ctx, src, qp, pps_init_qp, &total_coeffs,
                         &dropped_blocks);

    // 5) Annex B 封装：SPS(type7,ref3) + PPS(type8,ref3) + IDR slice(type5,ref3)。
    std::vector<uint8_t> stream;
    size_t off0 = stream.size();
    WriteAnnexBNal(stream, MakeNalHeader(3, 7), sps_rbsp);
    size_t off1 = stream.size();
    WriteAnnexBNal(stream, MakeNalHeader(3, 8), pps_rbsp);
    size_t off2 = stream.size();
    WriteAnnexBNal(stream, MakeNalHeader(3, 5), slice_rbsp);
    size_t off3 = stream.size();
    NalSizes nsz;
    nsz.sps = off1 - off0;
    nsz.pps = off2 - off1;
    nsz.idr_slice = off3 - off2;

    // 6) 写文件。
    std::FILE* fp = std::fopen(out_path.c_str(), "wb");
    if (!fp) {
        std::printf("error: cannot open %s for write\n", out_path.c_str());
        return 1;
    }
    std::fwrite(stream.data(), 1, stream.size(), fp);
    std::fclose(fp);

    // 7) 统计 + PSNR（重建 vs 原始，仅真实显示区域；重建为去块前）。
    double psnr = LumaPsnr(src, ctx, realW, realH);
    int mbs = ctx.mbw * ctx.mbh;

    std::printf("input : %s (%dx%d, coded %dx%d)\n", in_path.c_str(),
                realW, realH, codedW, codedH);
    std::printf("output: %s (%zu bytes)\n", out_path.c_str(), stream.size());
    std::printf("QP=%d  macroblocks=%d (all I_4x4)  frames=1 (IDR)\n", qp, mbs);
    std::printf("nonzero luma coeffs (encoded) : %d\n", total_coeffs);
    int total_blocks = mbs * 16;
    std::printf("residual blocks dropped (CAVLC roundtrip guard) : %d / %d\n",
                dropped_blocks, total_blocks);
    std::printf("NAL sizes: SPS=%zu PPS=%zu IDR_slice=%zu bytes\n",
                nsz.sps, nsz.pps, nsz.idr_slice);
    std::printf("luma PSNR (recon vs original, pre-deblock) : %.2f dB\n", psnr);

    // 8) 导出 nal_layout.txt（配码流结构图）。
    {
        std::FILE* nf = std::fopen("chart_data/nal_layout.txt", "w");
        if (nf) {
            std::fprintf(nf, "# nal_type name bytes\n");
            std::fprintf(nf, "7 SPS %zu\n", nsz.sps);
            std::fprintf(nf, "8 PPS %zu\n", nsz.pps);
            std::fprintf(nf, "5 IDR_slice %zu\n", nsz.idr_slice);
            std::fprintf(nf, "# total_stream_bytes %zu\n", stream.size());
            std::fprintf(nf, "# coded_size %dx%d real_size %dx%d\n",
                         codedW, codedH, realW, realH);
            std::fprintf(nf, "# qp %d macroblocks %d nonzero_luma_coeffs %d\n",
                         qp, mbs, total_coeffs);
            std::fprintf(nf, "# dropped_residual_blocks %d of %d\n",
                         dropped_blocks, total_blocks);
            std::fprintf(nf, "# luma_psnr_pre_deblock_db %.2f\n", psnr);
            std::fclose(nf);
            std::printf("nal layout -> chart_data/nal_layout.txt\n");
        }
    }
    return 0;
}

}  // namespace
}  // namespace cfs

// ── main ────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    using namespace cfs;
    if (argc < 3) {
        std::printf("usage: %s <input.ppm|yuv> <output.h264> [--qp N] "
                    "[--size WxH]\n", argv[0]);
        std::printf("  --qp N   : quantization parameter (default from RcConfig)\n");
        std::printf("  --size   : required for .yuv input (frame size)\n");
        return 1;
    }
    std::string in_path = argv[1];
    std::string out_path = argv[2];

    // 默认 QP 取 D5 的固定 QP 配置初值。
    RcConfig rc;                 // mode=0 固定 QP, init_qp=28
    int qp = rc.init_qp;
    int yuv_w = 0, yuv_h = 0;
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--qp" && i + 1 < argc) {
            qp = std::atoi(argv[++i]);
        } else if (a == "--size" && i + 1 < argc) {
            std::sscanf(argv[++i], "%dx%d", &yuv_w, &yuv_h);
        }
    }
    qp = std::max(kMinQp, std::min(kMaxQp, qp));
    return EncodeMainImpl(in_path, out_path, qp, yuv_w, yuv_h);
}
