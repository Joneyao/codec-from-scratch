// encoder_main.cpp — A5 收尾：把 A1-A4 全部串起来，读一张 PPM，吐一个真能被
//                     系统查看器 / libjpeg / ffmpeg 打开的 .jpg 文件。
//
// 完整流水线（每一步对应前面一篇）：
//   读 PPM (A0/image_io)
//     -> RGB→YCbCr + 4:2:0 抽样 (A1/color_transform)
//     -> 按 MCU 顺序取 8x8 块 -> 正向 DCT (A2/dct8x8)
//     -> 量化 (A3/quantize)
//     -> Zig-Zag + DC 差分 + AC 游程 (A4/zigzag_rle)
//     -> 霍夫曼编码进 bit 流 (A4/huffman_encode)
//     -> 组装 JFIF 标记段 + byte stuffing + 写盘 (A5/jfif_writer)
//
// 4:2:0 的 MCU（最小编码单元）是 16x16 像素：4 个 Y 块(2x2) + 1 个 Cb + 1 个 Cr。
// 扫描数据里，块的顺序严格是：Y00 Y01 Y10 Y11 Cb Cr（T.81 A.2.3 交错顺序）。
//
// 用法: ./encoder_main input.ppm output.jpg [quality=90]
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "color_transform.h"
#include "dct8x8.h"
#include "huffman_encode.h"
#include "image_io.h"
#include "jfif_writer.h"
#include "quantize.h"
#include "zigzag_rle.h"

namespace {

// 从一个平面里取 8x8 块，块起点 (bx,by) 以像素计。越界的行/列用边缘像素复制
// 补齐（edge replication），保证图像宽高不是 8/16 倍数时也能编码。
cfs::Block8u ExtractBlock(const cfs::Plane& p, int bx, int by) {
    cfs::Block8u blk{};
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

}  // namespace

namespace {

// 编码一个 8x8 块的完整"熵编码前 + 熵编码"链条：
//   DCT -> 量化 -> Zig-Zag -> DC 差分（用 prev_dc 状态）-> AC 游程 -> 霍夫曼。
// prev_dc 按分量各自维护（Y / Cb / Cr 三条独立的 DC 差分链，T.81 F.1.2.1）。
void EncodeOneBlock(const cfs::Block8u& pixels, const cfs::QuantTable& qtab,
                    const cfs::HuffTable& dc_tab, const cfs::HuffTable& ac_tab,
                    int& prev_dc, cfs::BitWriter& bw) {
    cfs::Block8d coeffs = cfs::ForwardDct(pixels);
    cfs::Block8d quant = cfs::Quantize(coeffs, qtab);
    std::array<int, 64> zz = cfs::ZigZagScan(quant);
    int dc = zz[0];
    int dc_diff = dc - prev_dc;
    prev_dc = dc;
    std::vector<cfs::RleSymbol> ac = cfs::RunLengthEncodeAc(zz);
    cfs::EncodeBlock(dc_diff, ac, dc_tab, ac_tab, bw);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: %s input.ppm output.jpg [quality=90]\n", argv[0]);
        return 1;
    }
    int quality = (argc >= 4) ? std::atoi(argv[3]) : 90;
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;

    cfs::RgbImage rgb;
    if (!cfs::LoadPpm(argv[1], rgb)) {
        std::fprintf(stderr, "failed to load PPM: %s\n", argv[1]);
        return 2;
    }

    // A1：RGB -> YCbCr，4:2:0 抽样（Cb/Cr 各缩到 Y 的一半分辨率）。
    cfs::YCbCrImage img =
        cfs::RgbToYCbCr(rgb, cfs::ChromaSubsampling::k420);

    // A3：按 quality 缩放亮度 / 色度量化表。
    cfs::QuantTable luma_q = cfs::ScaleTable(cfs::kBaseLuma, quality);
    cfs::QuantTable chroma_q = cfs::ScaleTable(cfs::kBaseChroma, quality);

    const cfs::HuffTable& luma_dc = cfs::StdLumaDcTable();
    const cfs::HuffTable& luma_ac = cfs::StdLumaAcTable();
    const cfs::HuffTable& chroma_dc = cfs::StdChromaDcTable();
    const cfs::HuffTable& chroma_ac = cfs::StdChromaAcTable();

    // 三条独立的 DC 差分链（每分量首块以 0 为前值）。
    int dc_y = 0, dc_cb = 0, dc_cr = 0;
    cfs::BitWriter bw;

    // 4:2:0 下一个 MCU = 16x16 亮度像素。以 MCU 为步长遍历，
    // 每个 MCU 依次写 4 个 Y 块、1 个 Cb 块、1 个 Cr 块（交错顺序）。
    int mcus_x = (rgb.width + 15) / 16;
    int mcus_y = (rgb.height + 15) / 16;
    for (int my = 0; my < mcus_y; ++my) {
        for (int mx = 0; mx < mcus_x; ++mx) {
            // 4 个 Y 块：2x2 排布，块内偏移 (0,0)(8,0)(0,8)(8,8)。
            for (int j = 0; j < 2; ++j) {
                for (int i = 0; i < 2; ++i) {
                    cfs::Block8u yb = ExtractBlock(
                        img.y, mx * 16 + i * 8, my * 16 + j * 8);
                    EncodeOneBlock(yb, luma_q, luma_dc, luma_ac, dc_y, bw);
                }
            }
            // 抽样后的 Cb/Cr 平面里，这个 MCU 只对应一个 8x8 块，起点 (mx*8,my*8)。
            cfs::Block8u cbb = ExtractBlock(img.cb, mx * 8, my * 8);
            EncodeOneBlock(cbb, chroma_q, chroma_dc, chroma_ac, dc_cb, bw);
            cfs::Block8u crb = ExtractBlock(img.cr, mx * 8, my * 8);
            EncodeOneBlock(crb, chroma_q, chroma_dc, chroma_ac, dc_cr, bw);
        }
    }
    std::vector<uint8_t> scan = bw.TakeBytes();

    // A5：填帧头参数并组装 JFIF。4:2:0 → Y 抽样因子 2x2，Cb/Cr 各 1x1。
    cfs::JfifParams params;
    params.width = rgb.width;
    params.height = rgb.height;
    params.luma_quant = luma_q;
    params.chroma_quant = chroma_q;
    params.scan_data = scan;
    params.components = {
        // id, Hi, Vi, quant_id, dc_table, ac_table
        {1, 2, 2, 0, 0, 0},  // Y  用亮度量化表(0) + 亮度霍夫曼表(0)
        {2, 1, 1, 1, 1, 1},  // Cb 用色度量化表(1) + 色度霍夫曼表(1)
        {3, 1, 1, 1, 1, 1},  // Cr 同 Cb
    };

    std::vector<uint8_t> file = cfs::AssembleJfif(params);
    if (!cfs::WriteFile(argv[2], file)) {
        std::fprintf(stderr, "failed to write output: %s\n", argv[2]);
        return 3;
    }

    std::printf(
        "encoded %dx%d PPM -> %s : quality=%d, %d MCUs, scan=%zu bytes, "
        "file=%zu bytes\n",
        rgb.width, rgb.height, argv[2], quality, mcus_x * mcus_y, scan.size(),
        file.size());
    return 0;
}
