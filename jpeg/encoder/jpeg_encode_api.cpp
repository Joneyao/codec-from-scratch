// jpeg_encode_api.cpp — EncodeFrameToJpeg 的实现（原 encoder_main 主体搬过来）。
#include "jpeg_encode_api.h"

#include <array>
#include <vector>

#include "color_transform.h"
#include "dct8x8.h"
#include "huffman_encode.h"
#include "quantize.h"
#include "jfif_writer.h"
#include "zigzag_rle.h"

namespace cfs {

namespace {

// 从一个平面里取 8x8 块，块起点 (bx,by) 以像素计。越界的行/列用边缘像素复制
// 补齐（edge replication），保证图像宽高不是 8/16 倍数时也能编码。
Block8u ExtractBlock(const Plane& p, int bx, int by) {
    Block8u blk{};
    for (int dy = 0; dy < 8; ++dy) {
        int y = by + dy;
        if (y >= p.height) y = p.height - 1;
        for (int dx = 0; dx < 8; ++dx) {
            int x = bx + dx;
            if (x >= p.width) x = p.width - 1;
            blk[dy][dx] = p.at(x, y);
        }
    }
    return blk;
}

// 编码一个 8x8 块的完整链条：DCT -> 量化 -> Zig-Zag -> DC 差分 -> AC 游程 -> 霍夫曼。
void EncodeOneBlock(const Block8u& pixels, const QuantTable& qtab,
                    const HuffTable& dc_tab, const HuffTable& ac_tab,
                    int& prev_dc, BitWriter& bw) {
    Block8d coeffs = ForwardDct(pixels);
    Block8d quant = Quantize(coeffs, qtab);
    std::array<int, 64> zz = ZigZagScan(quant);
    int dc = zz[0];
    int dc_diff = dc - prev_dc;
    prev_dc = dc;
    std::vector<RleSymbol> ac = RunLengthEncodeAc(zz);
    EncodeBlock(dc_diff, ac, dc_tab, ac_tab, bw);
}

}  // namespace

std::vector<uint8_t> EncodeFrameToJpeg(const RgbImage& rgb, int quality) {
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;

    // A1：RGB -> YCbCr，4:2:0 抽样（Cb/Cr 各缩到 Y 的一半分辨率）。
    YCbCrImage img = RgbToYCbCr(rgb, ChromaSubsampling::k420);

    // A3：按 quality 缩放亮度 / 色度量化表。
    QuantTable luma_q = ScaleTable(kBaseLuma, quality);
    QuantTable chroma_q = ScaleTable(kBaseChroma, quality);

    const HuffTable& luma_dc = StdLumaDcTable();
    const HuffTable& luma_ac = StdLumaAcTable();
    const HuffTable& chroma_dc = StdChromaDcTable();
    const HuffTable& chroma_ac = StdChromaAcTable();

    // 三条独立的 DC 差分链（每分量首块以 0 为前值）。
    int dc_y = 0, dc_cb = 0, dc_cr = 0;
    BitWriter bw;

    // 4:2:0 下一个 MCU = 16x16 亮度像素。以 MCU 为步长遍历，
    // 每个 MCU 依次写 4 个 Y 块、1 个 Cb 块、1 个 Cr 块（交错顺序）。
    int mcus_x = (rgb.width + 15) / 16;
    int mcus_y = (rgb.height + 15) / 16;
    for (int my = 0; my < mcus_y; ++my) {
        for (int mx = 0; mx < mcus_x; ++mx) {
            for (int j = 0; j < 2; ++j) {
                for (int i = 0; i < 2; ++i) {
                    Block8u yb =
                        ExtractBlock(img.y, mx * 16 + i * 8, my * 16 + j * 8);
                    EncodeOneBlock(yb, luma_q, luma_dc, luma_ac, dc_y, bw);
                }
            }
            Block8u cbb = ExtractBlock(img.cb, mx * 8, my * 8);
            EncodeOneBlock(cbb, chroma_q, chroma_dc, chroma_ac, dc_cb, bw);
            Block8u crb = ExtractBlock(img.cr, mx * 8, my * 8);
            EncodeOneBlock(crb, chroma_q, chroma_dc, chroma_ac, dc_cr, bw);
        }
    }
    std::vector<uint8_t> scan = bw.TakeBytes();

    // A5：填帧头参数并组装 JFIF。4:2:0 → Y 抽样因子 2x2，Cb/Cr 各 1x1。
    JfifParams params;
    params.width = rgb.width;
    params.height = rgb.height;
    params.luma_quant = luma_q;
    params.chroma_quant = chroma_q;
    params.scan_data = scan;
    params.components = {
        {1, 2, 2, 0, 0, 0},  // Y
        {2, 1, 1, 1, 1, 1},  // Cb
        {3, 1, 1, 1, 1, 1},  // Cr
    };
    return AssembleJfif(params);
}

}  // namespace cfs
