// jpeg_decoder.cpp — 完整 baseline JPEG 解码器实现（A8）。
#include "jpeg_decoder.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "bitreader.h"
#include "block_reconstruct.h"
#include "entropy_decode.h"

namespace cfs {

namespace {

// 一个分量在解码时的全部工作状态。
struct CompState {
    int id = 0;              // 分量标识
    int h = 1, v = 1;        // 抽样因子 Hi/Vi
    int quant_id = 0;        // 量化表号
    int dc_table = 0;        // DC 霍夫曼表号
    int ac_table = 0;        // AC 霍夫曼表号
    int dc_pred = 0;         // DC 差分累加器（本分量独立一条链）
    int plane_w = 0;         // 本分量按 MCU 对齐后的像素宽（= mcu_cols*h*8）
    int plane_h = 0;         // 本分量按 MCU 对齐后的像素高（= mcu_rows*v*8）
    std::vector<uint8_t> plane;  // 解码出的该分量像素（plane_w*plane_h）
};

// 在头里按 class/id 找霍夫曼解码表。
const HuffDecTable* FindHuff(const JpegHeader& h, uint8_t cls, uint8_t id) {
    for (const auto& t : h.huffman_tables)
        if (t.table_class == cls && t.table_id == id) return &t.table;
    return nullptr;
}

// 在头里按 id 找量化表。
const QuantTable* FindQuant(const JpegHeader& h, uint8_t id) {
    for (const auto& q : h.quant_tables)
        if (q.table_id == id) return &q.table;
    return nullptr;
}

}  // namespace

namespace {

// 把一个还原好的 8x8 像素块写进分量平面里 (bx,by) 处（以该分量像素为坐标）。
// 平面已按 MCU 对齐补到整块，所以这里不会越界。
void WriteBlock(CompState& c, const Block8u& blk, int bx, int by) {
    for (int dy = 0; dy < 8; ++dy) {
        int y = by + dy;
        for (int dx = 0; dx < 8; ++dx) {
            int x = bx + dx;
            c.plane[static_cast<size_t>(y) * c.plane_w + x] = blk[dy][dx];
        }
    }
}

}  // namespace

DecodeResult DecodeJpeg(const JpegHeader& header,
                        const std::vector<uint8_t>& file_bytes) {
    DecodeResult res;
    if (header.frame_components.empty() || header.scan_components.empty()) {
        res.error = "缺少帧分量或扫描分量信息";
        return res;
    }
    if (header.width <= 0 || header.height <= 0) {
        res.error = "无效的图像尺寸";
        return res;
    }

    // 建立每个分量的工作状态，并把 SOS 里的 DC/AC 表号补进去。
    std::vector<CompState> comps;
    int max_h = 1, max_v = 1;
    for (const auto& fc : header.frame_components) {
        CompState c;
        c.id = fc.id;
        c.h = fc.h_sample;
        c.v = fc.v_sample;
        c.quant_id = fc.quant_id;
        for (const auto& sc : header.scan_components) {
            if (sc.id == fc.id) {
                c.dc_table = sc.dc_table;
                c.ac_table = sc.ac_table;
            }
        }
        max_h = std::max(max_h, c.h);
        max_v = std::max(max_v, c.v);
        comps.push_back(c);
    }

    // 一个 MCU 覆盖 (max_h*8) x (max_v*8) 像素。整张图按 MCU 数向上取整对齐。
    const int mcu_px_w = max_h * 8;
    const int mcu_px_h = max_v * 8;
    const int mcu_cols = (header.width + mcu_px_w - 1) / mcu_px_w;
    const int mcu_rows = (header.height + mcu_px_h - 1) / mcu_px_h;
    res.mcu_cols = mcu_cols;
    res.mcu_rows = mcu_rows;
    res.max_h = max_h;
    res.max_v = max_v;
    res.restart_interval = header.restart_interval;

    // 为每个分量分配它按 MCU 对齐后的平面。分量像素宽 = mcu_cols * Hi * 8，
    // 因为每个 MCU 里该分量有 Hi 个横向块、每块 8 像素。
    for (auto& c : comps) {
        c.plane_w = mcu_cols * c.h * 8;
        c.plane_h = mcu_rows * c.v * 8;
        c.plane.assign(static_cast<size_t>(c.plane_w) * c.plane_h, 0);
    }

    // 预取每个分量要用的三张表；缺表直接报错（真实文件也可能缺，要优雅退出）。
    struct CompTables { const HuffDecTable* dc; const HuffDecTable* ac;
                        const QuantTable* q; };
    std::vector<CompTables> tabs(comps.size());
    for (size_t i = 0; i < comps.size(); ++i) {
        tabs[i].dc = FindHuff(header, 0, comps[i].dc_table);
        tabs[i].ac = FindHuff(header, 1, comps[i].ac_table);
        tabs[i].q = FindQuant(header, comps[i].quant_id);
        if (!tabs[i].dc || !tabs[i].ac || !tabs[i].q) {
            res.error = "分量缺少霍夫曼或量化表";
            return res;
        }
    }

    // 定位熵编码数据。解析器只记了偏移，这里从传入的文件字节里取。
    if (header.scan_data_offset + header.scan_data_length > file_bytes.size()) {
        res.error = "扫描数据范围越界";
        return res;
    }
    const uint8_t* entropy = file_bytes.data() + header.scan_data_offset;
    const size_t entropy_len = header.scan_data_length;

    // 熵解码用一个可重建的 reader：碰到 RSTn 时要从 marker 之后重新开一个。
    size_t stream_off = 0;  // 相对 entropy 起点的当前读取偏移
    BitReader br(entropy, entropy_len);
    const int ri = header.restart_interval;
    int mcus_since_restart = 0;

    // 主循环：逐 MCU、MCU 内逐分量、分量内逐块（T.81 A.2.3 交错顺序）。
    for (int my = 0; my < mcu_rows; ++my) {
        for (int mx = 0; mx < mcu_cols; ++mx) {
            // 重启点：每 ri 个 MCU（ri>0 时）前，跨过一个 RSTn 并复位状态。
            // 关键：解完上个区间最后一个块后，reader 未必"正好停在" marker 上——
            // 当前字节里可能还剩几个填充位（编码器把区间末尾补 1 到字节边界，
            // T.81 F.1.2.3），marker 要到下一次取字节时才被发现。所以这里不依赖
            // AtMarker()，而是从 reader 当前字节位置起，在原始码流里扫下一个
            // RSTn（0xFF 后跟 0xD0..0xD7），跳过它、按字节对齐后重开 reader。
            if (ri > 0 && mcus_since_restart == ri) {
                size_t p = stream_off + br.BytePos();  // 当前消费到的字节
                while (p + 1 < entropy_len &&
                       !(entropy[p] == 0xFF && entropy[p + 1] >= 0xD0 &&
                         entropy[p + 1] <= 0xD7)) {
                    ++p;
                }
                if (p + 1 >= entropy_len) {
                    res.error = "重启点未找到 RSTn 标记";
                    return res;
                }
                stream_off = p + 2;  // 跳过 2 字节 RSTn，天然回到字节边界
                br = BitReader(entropy + stream_off, entropy_len - stream_off);
                for (auto& c : comps) c.dc_pred = 0;  // DC 预测清零
                mcus_since_restart = 0;
            }

            for (size_t ci = 0; ci < comps.size(); ++ci) {
                CompState& c = comps[ci];
                // 该分量在本 MCU 里有 v 行 * h 列个块。
                for (int by = 0; by < c.v; ++by) {
                    for (int bx = 0; bx < c.h; ++bx) {
                        std::array<int, 64> zz{};
                        if (!DecodeBlockZigZag(br, *tabs[ci].dc, *tabs[ci].ac,
                                               c.dc_pred, zz)) {
                            res.error = "熵解码中断（码流损坏或提前结束）";
                            return res;
                        }
                        Block8d q = InverseZigZag(zz);
                        Block8d dq = DequantizeBlock(q, *tabs[ci].q);
                        Block8u px = ReconstructPixels(dq);
                        int px0 = (mx * c.h + bx) * 8;
                        int py0 = (my * c.v + by) * 8;
                        WriteBlock(c, px, px0, py0);
                    }
                }
            }
            ++mcus_since_restart;
        }
    }

    // 找出 Y / Cb / Cr 三个分量（按分量 id：1=Y, 2=Cb, 3=Cr 是 JFIF 惯例）。
    // 更稳妥的做法是按帧里出现顺序：第一个=Y，第二、三个=Cb/Cr。
    const CompState* Yc = &comps[0];
    const CompState* Cbc = comps.size() > 1 ? &comps[1] : &comps[0];
    const CompState* Crc = comps.size() > 2 ? &comps[2] : &comps[0];

    // 色度上采样：把半分辨率的 Cb/Cr 放大回 Y 的全分辨率。
    // 分量自己的坐标 = 全分辨率坐标 * (c.h/max_h)。对 4:2:0 就是 x/2、y/2。
    // 这里用双线性（bilinear）：在四个最近的色度样点之间按小数位置插值。
    // 和最近邻（直接取整）相比，双线性在色度边缘更平滑，也更接近 libjpeg 默认
    // 的 "fancy upsampling"（三角形滤波），逐像素误差因此小得多。
    // T.871 只规定 YCbCr↔RGB 的算术，不规定上采样滤波，这一步是实现的自由度。
    auto plane_px = [](const CompState* c, int sx, int sy) -> double {
        if (sx < 0) sx = 0;
        if (sx >= c->plane_w) sx = c->plane_w - 1;
        if (sy < 0) sy = 0;
        if (sy >= c->plane_h) sy = c->plane_h - 1;
        return c->plane[static_cast<size_t>(sy) * c->plane_w + sx];
    };
    auto sample_at = [&](const CompState* c, int fx, int fy) -> double {
        if (c->h == max_h && c->v == max_v) {
            // 该分量本就是全分辨率（如 4:4:4 的 Cb/Cr 或所有分量的 Y），直接取。
            return plane_px(c, fx, fy);
        }
        // 映射到该分量的连续坐标，样点落在像素中心：src = (f + 0.5)*scale - 0.5。
        double gx = (fx + 0.5) * c->h / max_h - 0.5;
        double gy = (fy + 0.5) * c->v / max_v - 0.5;
        int x0 = static_cast<int>(std::floor(gx));
        int y0 = static_cast<int>(std::floor(gy));
        double tx = gx - x0, ty = gy - y0;
        double a = plane_px(c, x0, y0),     b = plane_px(c, x0 + 1, y0);
        double d = plane_px(c, x0, y0 + 1), e = plane_px(c, x0 + 1, y0 + 1);
        double top = a + (b - a) * tx;
        double bot = d + (e - d) * tx;
        return top + (bot - top) * ty;
    };

    // YCbCr -> RGB（JFIF / ITU-T T.871 full-range BT.601 的逆变换）：
    //   R = Y + 1.402   (Cr-128)
    //   G = Y - 0.344136(Cb-128) - 0.714136(Cr-128)
    //   B = Y + 1.772   (Cb-128)
    auto clamp8 = [](double v) -> uint8_t {
        int iv = static_cast<int>(std::lround(v));
        if (iv < 0) iv = 0;
        if (iv > 255) iv = 255;
        return static_cast<uint8_t>(iv);
    };

    res.image.width = header.width;
    res.image.height = header.height;
    res.image.data.resize(static_cast<size_t>(header.width) * header.height * 3);
    for (int y = 0; y < header.height; ++y) {
        for (int x = 0; x < header.width; ++x) {
            double Y = sample_at(Yc, x, y);
            double Cb = sample_at(Cbc, x, y) - 128.0;
            double Cr = sample_at(Crc, x, y) - 128.0;
            size_t o = (static_cast<size_t>(y) * header.width + x) * 3;
            res.image.data[o + 0] = clamp8(Y + 1.402 * Cr);
            res.image.data[o + 1] = clamp8(Y - 0.344136 * Cb - 0.714136 * Cr);
            res.image.data[o + 2] = clamp8(Y + 1.772 * Cb);
        }
    }

    res.ok = true;
    return res;
}

DecodeResult DecodeJpegFile(const std::string& path) {
    DecodeResult res;
    std::vector<uint8_t> bytes = ReadWholeFile(path);
    if (bytes.empty()) {
        res.error = "读文件失败或文件为空: " + path;
        return res;
    }
    ParseResult pr = ParseJpeg(bytes);
    if (!pr.ok) {
        res.error = "解析失败: " + pr.error;
        return res;
    }
    return DecodeJpeg(pr.header, bytes);
}

}  // namespace cfs
