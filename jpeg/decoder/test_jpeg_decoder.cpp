// test_jpeg_decoder.cpp — 完整解码器（A8）的整图端到端测试。
//
// 最有说服力的一条：造一张 RGB 图 → 用编码器 A1-A5 完整链压成内存里的 .jpg
// 字节流 → 用 A8 解码器一路解回 RGB → 比对整图 PSNR。跑通它，等于把 A1-A8
// 八篇的正反两条链在一次运行里全部验一遍。
//
// 另外单独验三件"真实世界脏活"：
//   - 非 16 倍数尺寸（解码要按 MCU 对齐、输出裁回真实宽高）
//   - 4:2:0 色度上采样（Cb/Cr 半分辨率放大回全分辨率）
//   - DC 差分按分量独立累加（多个 MCU 时 Y/Cb/Cr 各走各的链）
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "bitwriter.h"
#include "color_transform.h"
#include "dct8x8.h"
#include "huffman_encode.h"
#include "image_io.h"
#include "jfif_parser.h"
#include "jfif_writer.h"
#include "jpeg_decoder.h"
#include "quantize.h"
#include "zigzag_rle.h"

namespace {
int g_failed = 0;
void Check(bool c, const char* m) {
    std::printf(c ? "[ok]   %s\n" : "[FAIL] %s\n", m);
    if (!c) ++g_failed;
}

// 从平面取 8x8 块（越界复制边缘），与编码器 ExtractBlock 同规则。
cfs::Block8u ExtractBlock(const cfs::Plane& p, int bx, int by) {
    cfs::Block8u blk{};
    for (int dy = 0; dy < 8; ++dy) {
        int y = by + dy; if (y >= p.height) y = p.height - 1;
        for (int dx = 0; dx < 8; ++dx) {
            int x = bx + dx; if (x >= p.width) x = p.width - 1;
            blk[dy][dx] = p.at(x, y);
        }
    }
    return blk;
}
}  // namespace

namespace {

// 用编码器 A1-A5 完整链把一张 RGB 图压成内存里的 .jpg 字节流（4:2:0, 指定质量）。
// 逻辑与 encoder_main.cpp 一致，只是不落盘、直接返回字节 vector。
std::vector<uint8_t> EncodeToJpegBytes(const cfs::RgbImage& rgb, int quality) {
    cfs::YCbCrImage img = cfs::RgbToYCbCr(rgb, cfs::ChromaSubsampling::k420);
    cfs::QuantTable luma_q = cfs::ScaleTable(cfs::kBaseLuma, quality);
    cfs::QuantTable chroma_q = cfs::ScaleTable(cfs::kBaseChroma, quality);
    const cfs::HuffTable& luma_dc = cfs::StdLumaDcTable();
    const cfs::HuffTable& luma_ac = cfs::StdLumaAcTable();
    const cfs::HuffTable& chroma_dc = cfs::StdChromaDcTable();
    const cfs::HuffTable& chroma_ac = cfs::StdChromaAcTable();

    int dc_y = 0, dc_cb = 0, dc_cr = 0;
    cfs::BitWriter bw;
    int mcus_x = (rgb.width + 15) / 16;
    int mcus_y = (rgb.height + 15) / 16;
    auto enc_block = [&](const cfs::Block8u& px, const cfs::QuantTable& q,
                         const cfs::HuffTable& dct, const cfs::HuffTable& act,
                         int& prev) {
        cfs::Block8d c = cfs::ForwardDct(px);
        cfs::Block8d qq = cfs::Quantize(c, q);
        std::array<int, 64> zz = cfs::ZigZagScan(qq);
        int diff = zz[0] - prev; prev = zz[0];
        std::vector<cfs::RleSymbol> ac = cfs::RunLengthEncodeAc(zz);
        cfs::EncodeBlock(diff, ac, dct, act, bw);
    };
    for (int my = 0; my < mcus_y; ++my)
        for (int mx = 0; mx < mcus_x; ++mx) {
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i)
                    enc_block(ExtractBlock(img.y, mx * 16 + i * 8,
                                           my * 16 + j * 8),
                              luma_q, luma_dc, luma_ac, dc_y);
            enc_block(ExtractBlock(img.cb, mx * 8, my * 8), chroma_q,
                      chroma_dc, chroma_ac, dc_cb);
            enc_block(ExtractBlock(img.cr, mx * 8, my * 8), chroma_q,
                      chroma_dc, chroma_ac, dc_cr);
        }

    cfs::JfifParams params;
    params.width = rgb.width;
    params.height = rgb.height;
    params.luma_quant = luma_q;
    params.chroma_quant = chroma_q;
    params.scan_data = bw.TakeBytes();
    params.components = {{1, 2, 2, 0, 0, 0}, {2, 1, 1, 1, 1, 1},
                         {3, 1, 1, 1, 1, 1}};
    return cfs::AssembleJfif(params);
}

// 两张同尺寸 RGB 图的 PSNR（分贝）。完全相同返回一个很大的值。
double Psnr(const cfs::RgbImage& a, const cfs::RgbImage& b) {
    if (a.width != b.width || a.height != b.height) return -1.0;
    double sse = 0.0;
    size_t n = a.data.size();
    for (size_t i = 0; i < n; ++i) {
        double d = static_cast<double>(a.data[i]) - b.data[i];
        sse += d * d;
    }
    if (sse == 0.0) return 999.0;
    double mse = sse / static_cast<double>(n);
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

// 造一张有结构的 RGB 图：渐变 + 两个色块（近似真实图，含高低频）。
cfs::RgbImage MakeImage(int w, int h) {
    cfs::RgbImage img;
    img.width = w; img.height = h;
    img.data.resize(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            size_t o = (static_cast<size_t>(y) * w + x) * 3;
            int r = 128 + static_cast<int>(100 * std::sin(x / 12.0));
            int g = 128 + static_cast<int>(90 * std::cos(y / 15.0));
            int b = 128 + static_cast<int>(80 * std::sin((x + y) / 20.0));
            if (x >= w / 4 && x < w / 2 && y >= h / 4 && y < h / 2) {
                r = 220; g = 40; b = 40;
            }
            auto cl = [](int v) { return static_cast<uint8_t>(
                v < 0 ? 0 : (v > 255 ? 255 : v)); };
            img.data[o] = cl(r); img.data[o + 1] = cl(g); img.data[o + 2] = cl(b);
        }
    return img;
}
}  // namespace

int main() {
    // === 1. 整图往返（关键证据）：RGB -> jpg 字节 -> RGB，比 PSNR ===
    // 256x256（16 的倍数，无边缘补齐干扰），quality=90。
    {
        cfs::RgbImage src = MakeImage(256, 256);
        std::vector<uint8_t> jpg = EncodeToJpegBytes(src, 90);
        cfs::ParseResult pr = cfs::ParseJpeg(jpg);
        Check(pr.ok, "round-trip: 编码出的 jpg 能被自己的解析器解析");
        cfs::DecodeResult dr = cfs::DecodeJpeg(pr.header, jpg);
        Check(dr.ok, "round-trip: 完整解码成功");
        Check(dr.image.width == 256 && dr.image.height == 256,
              "round-trip: 输出尺寸正确");
        double psnr = Psnr(src, dr.image);
        std::printf("      256x256 q90 往返 PSNR = %.2f dB\n", psnr);
        // 4:2:0 + q90 有损压缩，重建 PSNR 经验上 >30 dB 才算链路正确。
        Check(psnr > 30.0, "round-trip: PSNR > 30 dB (整条编解码链正确)");
    }

    // === 2. 非 16 倍数尺寸：解码要按 MCU 对齐、输出裁回真实宽高 ===
    {
        cfs::RgbImage src = MakeImage(300, 200);  // 都不是 16 的倍数
        std::vector<uint8_t> jpg = EncodeToJpegBytes(src, 85);
        cfs::ParseResult pr = cfs::ParseJpeg(jpg);
        cfs::DecodeResult dr = cfs::DecodeJpeg(pr.header, jpg);
        Check(dr.ok, "非16倍数: 解码成功");
        Check(dr.image.width == 300 && dr.image.height == 200,
              "非16倍数: 输出精确裁剪到 300x200");
        double psnr = Psnr(src, dr.image);
        std::printf("      300x200 q85 往返 PSNR = %.2f dB\n", psnr);
        Check(psnr > 30.0, "非16倍数: PSNR > 30 dB");
    }

    // === 3. 多 MCU 下 DC 差分按分量独立累加 ===
    // 若 DC 链串错分量（Y 的差分累加进了 Cb），颜色会整体漂移，PSNR 骤降。
    // 上面 300x200 已是多 MCU（约 19x13 个 MCU），PSNR 达标即证明 DC 链正确。
    {
        cfs::RgbImage src = MakeImage(160, 160);
        std::vector<uint8_t> jpg = EncodeToJpegBytes(src, 75);
        cfs::DecodeResult dr =
            cfs::DecodeJpeg(cfs::ParseJpeg(jpg).header, jpg);
        double psnr = Psnr(src, dr.image);
        std::printf("      160x160 q75 往返 PSNR = %.2f dB\n", psnr);
        Check(dr.ok && psnr > 28.0,
              "DC 差分按分量独立: 多 MCU 无颜色漂移 (PSNR 达标)");
    }

    std::printf(g_failed == 0 ? "ALL TESTS PASSED\n" : "%d TEST(S) FAILED\n",
                g_failed);
    return g_failed == 0 ? 0 : 1;
}
