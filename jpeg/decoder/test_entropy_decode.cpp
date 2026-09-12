// test_entropy_decode.cpp — 熵解码 + 反量化 + 反 Zig-Zag + IDCT 的单元测试。
//
// 最有力的一条是"端到端往返"：拿一个已知像素块，用编码器 A2-A4 正向压成比特，
// 再用解码器 A7 反向解回像素，比对还原块与原块。只要误差落在量化引入的范围内，
// 就证明整条解码链（霍夫曼解码→反量化→反 Zig-Zag→IDCT）和编码链严丝合缝。
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

#include "bitreader.h"
#include "bitwriter.h"
#include "block_reconstruct.h"
#include "dct8x8.h"
#include "entropy_decode.h"
#include "huffman_decode.h"
#include "huffman_encode.h"  // 编码端：标准表 / EncodeBlock / Category
#include "quantize.h"
#include "zigzag_rle.h"      // ZigZagScan / RunLengthEncodeAc

namespace {
int g_failed = 0;
void Check(bool c, const char* m) {
    std::printf(c ? "[ok]   %s\n" : "[FAIL] %s\n", m);
    if (!c) ++g_failed;
}

// 真实 JFIF 熵数据里 0xFF 后面要塞 0x00（byte stuffing，T.81 F.1.2.3），
// 否则 BitReader 会把它当 marker。往返测试必须模拟这一步。
std::vector<uint8_t> ByteStuff(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out;
    out.reserve(in.size());
    for (uint8_t b : in) {
        out.push_back(b);
        if (b == 0xFF) out.push_back(0x00);
    }
    return out;
}

// 造一个有结构的测试像素块：低频渐变 + 一点纹理，接近真实图像块。
cfs::Block8u MakeTestBlock() {
    cfs::Block8u b{};
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            int v = 120 + 8 * x + 5 * y + ((x + y) % 3) * 4;
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            b[y][x] = static_cast<uint8_t>(v);
        }
    return b;
}
}  // namespace

int main() {
    // === 1. 幅值解码：负数陷阱（T.81 F.2.2.1 EXTEND）===
    {
        // size=3：编码端把 -5 编成 3 位紧凑表示。正向：MagnitudeBits(-5,3)。
        uint32_t enc = cfs::MagnitudeBits(-5, 3);
        int dec = cfs::DecodeMagnitude(enc, 3);
        Check(dec == -5, "magnitude decode: size=3 bits round-trips to -5");
        // 直接给出教科书例子：size=3、bits='010'(=2) 最高位为 0 是负数，应为 -5。
        Check(cfs::DecodeMagnitude(0b010, 3) == -5,
              "magnitude decode: bits '010' (size=3) -> -5");
        // 正数：size=3、bits='101'(=5) 最高位为 1，应为 +5。
        Check(cfs::DecodeMagnitude(0b101, 3) == 5,
              "magnitude decode: bits '101' (size=3) -> +5");
        // size=0 恒为 0。
        Check(cfs::DecodeMagnitude(0, 0) == 0, "magnitude decode: size=0 -> 0");
        // 遍历一批值，MagnitudeBits/DecodeMagnitude 互为逆。
        bool all = true;
        for (int v = -1023; v <= 1023; ++v) {
            int size = cfs::Category(v);
            uint32_t bits = cfs::MagnitudeBits(v, size);
            if (cfs::DecodeMagnitude(bits, size) != v) { all = false; break; }
        }
        Check(all, "magnitude enc/dec inverse over [-1023,1023]");
    }

    // === 2. 霍夫曼解码：已知比特流解回正确符号 ===
    {
        // 用标准亮度 DC 表：'00'->0, '010'->1（和 A6 测试一致，换个入口再验一次）。
        cfs::HuffDecTable dec =
            cfs::BuildDecTable(cfs::StdLumaDcBits(), cfs::StdLumaDcVals());
        std::vector<uint8_t> buf = {0x3F};  // 00111111
        cfs::BitReader br(buf.data(), buf.size());
        bool ok = false;
        uint8_t s = cfs::DecodeSymbol(br, dec, &ok);
        Check(ok && s == 0, "huffman decode: code '00' -> symbol 0");
    }

    // === 3. 反 Zig-Zag：一维系数摆回 8x8 是 A4 ZigZagScan 的逆 ===
    {
        // 造一个已知 8x8 整数块，正向 ZigZag 再反 ZigZag，应完全还原。
        cfs::Block8d src{};
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) src[y][x] = y * 8 + x;
        std::array<int, 64> zz = cfs::ZigZagScan(src);
        cfs::Block8d back = cfs::InverseZigZag(zz);
        bool same = true;
        for (int y = 0; y < 8 && same; ++y)
            for (int x = 0; x < 8; ++x)
                if (std::lround(back[y][x]) != std::lround(src[y][x])) {
                    same = false;
                    break;
                }
        Check(same, "inverse zigzag is exact inverse of ZigZagScan");
    }

    // === 4. 端到端往返（关键证据）===
    // 原像素块 -> DCT -> 量化 -> zigzag -> RLE -> 霍夫曼编码 -> 比特流
    //         -> 霍夫曼解码 -> 反量化 -> 反 zigzag -> IDCT -> 还原像素块。
    {
        cfs::Block8u original = MakeTestBlock();
        cfs::QuantTable qtable = cfs::ScaleTable(cfs::kBaseLuma, 50);

        // --- 编码侧 ---
        cfs::Block8d coeffs = cfs::ForwardDct(original);
        cfs::Block8d quant = cfs::Quantize(coeffs, qtable);
        std::array<int, 64> zz = cfs::ZigZagScan(quant);
        int dc = zz[0];
        std::vector<cfs::RleSymbol> ac = cfs::RunLengthEncodeAc(zz);

        const cfs::HuffTable& dc_enc = cfs::StdLumaDcTable();
        const cfs::HuffTable& ac_enc = cfs::StdLumaAcTable();
        cfs::BitWriter bw;
        // 第一个块：DC 差分 = DC - 0 = DC 本身。
        cfs::EncodeBlock(dc, ac, dc_enc, ac_enc, bw);
        std::vector<uint8_t> raw = bw.TakeBytes();
        std::vector<uint8_t> stream = ByteStuff(raw);

        // --- 解码侧 ---
        cfs::HuffDecTable dc_dec =
            cfs::BuildDecTable(cfs::StdLumaDcBits(), cfs::StdLumaDcVals());
        cfs::HuffDecTable ac_dec =
            cfs::BuildDecTable(cfs::StdLumaAcBits(), cfs::StdLumaAcVals());
        cfs::BitReader br(stream.data(), stream.size());
        int dc_pred = 0;
        std::array<int, 64> zz_dec{};
        bool ok = cfs::DecodeBlockZigZag(br, dc_dec, ac_dec, dc_pred, zz_dec);
        Check(ok, "round-trip: decode block succeeds");

        // 解出的量化系数应与编码前的量化系数完全一致（熵编码是无损的）。
        bool zz_same = true;
        for (int i = 0; i < 64; ++i)
            if (zz_dec[i] != zz[i]) { zz_same = false; break; }
        Check(zz_same, "round-trip: entropy coding is lossless (zigzag matches)");

        // 继续还原到像素，比对与原块的误差。
        cfs::Block8d rq = cfs::InverseZigZag(zz_dec);
        cfs::Block8d rdq = cfs::DequantizeBlock(rq, qtable);
        cfs::Block8u recon = cfs::ReconstructPixels(rdq);

        int max_err = 0;
        double sse = 0.0;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                int e = std::abs(static_cast<int>(recon[y][x]) -
                                 static_cast<int>(original[y][x]));
                if (e > max_err) max_err = e;
                sse += static_cast<double>(e) * e;
            }
        double rmse = std::sqrt(sse / 64.0);
        std::printf("      往返误差: 最大逐像素差=%d, RMSE=%.3f\n", max_err, rmse);

        // 导出往返三张图的数据（原块 / 还原块 / 逐像素差），供误差热力图配图。
        // 环境变量 CFS_CHART_DIR 指定输出目录；不设则跳过（不影响 CI）。
        if (const char* dir = std::getenv("CFS_CHART_DIR")) {
            std::string d(dir);
            auto dump = [&](const cfs::Block8u& b, const std::string& name) {
                std::ofstream f(d + "/" + name);
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                        f << static_cast<int>(b[y][x]) << (x == 7 ? '\n' : ' ');
            };
            dump(original, "roundtrip_original.txt");
            dump(recon, "roundtrip_recon.txt");
            std::ofstream fe(d + "/roundtrip_diff.txt");
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    fe << (static_cast<int>(recon[y][x]) -
                           static_cast<int>(original[y][x]))
                       << (x == 7 ? '\n' : ' ');
        }
        // 质量 50 的量化误差有界。经验上单块最大差应在 ~15 内，RMSE 更小。
        Check(max_err <= 20, "round-trip: max per-pixel error within quant bound");
        Check(rmse <= 8.0, "round-trip: RMSE small (within quant bound)");
    }

    std::printf(g_failed == 0 ? "ALL TESTS PASSED\n" : "%d TEST(S) FAILED\n",
                g_failed);
    return g_failed == 0 ? 0 : 1;
}
