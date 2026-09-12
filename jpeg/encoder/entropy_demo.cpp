// entropy_demo.cpp — A4 配套：把量化后的系数走完熵编码全流程，导出真实数据供配图。
//
// 流程：读 PPM -> Y 平面 -> 对每个 8x8 块 DCT -> 量化(Q50) -> Zig-Zag -> 游程编码
// -> 霍夫曼编码。重点导出：
//   1) 一个"细节丰富"块的量化矩阵、zigzag 一维序列（看 0 排到队尾）。
//   2) 该块的 (run,value) 游程符号列表。
//   3) 该块各阶段的 bit 数对比：原始 64 系数 vs 熵编码后实际 bit。
//   4) 整幅 Y 平面所有块的总 bit 数、平均压缩比等汇总。
//
// 用法: ./entropy_demo input.ppm out_dir/
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "color_transform.h"
#include "dct8x8.h"
#include "huffman_encode.h"
#include "image_io.h"
#include "quantize.h"
#include "zigzag_rle.h"

namespace {

// 挑方差最大（细节最丰富）的块，熵编码的"节省"最直观。
void PickHighVarBlock(const cfs::Plane& p, int& bx, int& by) {
    long best = -1;
    bx = by = 0;
    for (int y = 0; y + 8 <= p.height; y += 8)
        for (int x = 0; x + 8 <= p.width; x += 8) {
            long s = 0, s2 = 0;
            for (int dy = 0; dy < 8; ++dy)
                for (int dx = 0; dx < 8; ++dx) {
                    int v = p.at(x + dx, y + dy);
                    s += v;
                    s2 += static_cast<long>(v) * v;
                }
            long var = s2 - s * s / 64;
            if (var > best) { best = var; bx = x; by = y; }
        }
}

int CountNonZero(const std::array<int, 64>& zz) {
    int n = 0;
    for (int v : zz)
        if (v != 0) ++n;
    return n;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s input.ppm out_dir/\n", argv[0]);
        return 1;
    }
    std::string out_dir = argv[2];
    if (!out_dir.empty() && out_dir.back() != '/') out_dir += '/';

    cfs::RgbImage rgb;
    if (!cfs::LoadPpm(argv[1], rgb)) {
        std::fprintf(stderr, "failed to load PPM\n");
        return 2;
    }
    cfs::YCbCrImage img = cfs::RgbToYCbCr(rgb, cfs::ChromaSubsampling::k444);
    cfs::QuantTable table = cfs::ScaleTable(cfs::kBaseLuma, 50);

    const cfs::HuffTable& dc_tab = cfs::StdLumaDcTable();
    const cfs::HuffTable& ac_tab = cfs::StdLumaAcTable();

    int bx = 0, by = 0;
    PickHighVarBlock(img.y, bx, by);

    // ---- 遍历整幅 Y 平面所有块，按光栅顺序做完整熵编码 ----
    cfs::BitWriter bw;
    int prev_dc = 0;
    long total_raw_bits = 0;      // 每块 64 系数 * 8 bit 的"朴素"基线
    long total_entropy_bits = 0;  // 熵编码后实际 bit
    int nblocks = 0;

    // 记录目标块的中间产物，单独导出。
    std::array<int, 64> target_zz{};
    std::vector<cfs::RleSymbol> target_ac;
    cfs::Block8d target_quant{};
    int target_dc_diff = 0;
    size_t target_bits = 0;

    for (int y = 0; y + 8 <= img.y.height; y += 8) {
        for (int x = 0; x + 8 <= img.y.width; x += 8) {
            cfs::Block8u pixels{};
            for (int dy = 0; dy < 8; ++dy)
                for (int dx = 0; dx < 8; ++dx)
                    pixels[dy][dx] = img.y.at(x + dx, y + dy);

            cfs::Block8d coeffs = cfs::ForwardDct(pixels);
            cfs::Block8d quant = cfs::Quantize(coeffs, table);
            std::array<int, 64> zz = cfs::ZigZagScan(quant);
            int dc = zz[0];
            int dc_diff = dc - prev_dc;
            prev_dc = dc;
            std::vector<cfs::RleSymbol> ac = cfs::RunLengthEncodeAc(zz);

            size_t bits = cfs::EncodeBlock(dc_diff, ac, dc_tab, ac_tab, bw);
            total_entropy_bits += static_cast<long>(bits);
            total_raw_bits += 64 * 8;  // 朴素：每系数 8 bit
            ++nblocks;

            if (x == bx && y == by) {
                target_zz = zz;
                target_ac = ac;
                target_quant = quant;
                target_dc_diff = dc_diff;
                target_bits = bits;
            }
        }
    }
    std::vector<uint8_t> stream = bw.TakeBytes();

    // ---- 导出目标块的量化矩阵与 zigzag 序列 ----
    cfs::DumpBlock(target_quant, out_dir + "e_quant_block.txt", true);
    cfs::DumpZigZag(target_zz, out_dir + "e_zigzag.txt");
    cfs::DumpBlock(target_quant, out_dir + "e_zigzag_matrix.txt", true);

    // ---- 导出 zigzag 扫描顺序表（供画之字形路径）----
    {
        FILE* f = std::fopen((out_dir + "e_zigzag_order.txt").c_str(), "w");
        if (f) {
            for (int k = 0; k < 64; ++k)
                std::fprintf(f, "%d%c", cfs::kZigZagOrder[k],
                             k + 1 == 64 ? '\n' : ' ');
            std::fclose(f);
        }
    }

    // ---- 导出目标块 (run,value) 游程符号，以及每个符号的霍夫曼码长 + 幅值 bit ----
    {
        FILE* f = std::fopen((out_dir + "e_rle.txt").c_str(), "w");
        if (f) {
            std::fprintf(f, "# type run value huff_bits mag_bits\n");
            // DC 单独一行。
            int dc_size = cfs::Category(target_dc_diff);
            std::fprintf(f, "DC 0 %d %d %d\n", target_dc_diff,
                         dc_tab.len[dc_size], dc_size);
            for (const cfs::RleSymbol& s : target_ac) {
                if (s.run == 0 && s.value == 0) {
                    std::fprintf(f, "EOB 0 0 %d 0\n", ac_tab.len[0x00]);
                } else if (s.run == 15 && s.value == 0) {
                    std::fprintf(f, "ZRL 15 0 %d 0\n", ac_tab.len[0xF0]);
                } else {
                    int size = cfs::Category(s.value);
                    uint8_t sym = static_cast<uint8_t>((s.run << 4) | size);
                    std::fprintf(f, "AC %d %d %d %d\n", s.run, s.value,
                                 ac_tab.len[sym], size);
                }
            }
            std::fclose(f);
        }
    }

    // ---- 导出目标块的 bit 数对比 ----
    int target_nz = CountNonZero(target_zz);
    {
        FILE* f = std::fopen((out_dir + "e_bits.txt").c_str(), "w");
        if (f) {
            std::fprintf(f, "# stage bits\n");
            std::fprintf(f, "raw_64coeff %d\n", 64 * 8);   // 朴素 512 bit
            std::fprintf(f, "nonzero_only %d\n", target_nz * 8);
            std::fprintf(f, "entropy_coded %zu\n", target_bits);
            std::fclose(f);
        }
    }

    // ---- 导出整幅汇总 + 目标块摘要（正文引用的真实数字）----
    {
        FILE* f = std::fopen((out_dir + "e_stats.txt").c_str(), "w");
        if (f) {
            std::fprintf(f, "block_x %d\nblock_y %d\n", bx, by);
            std::fprintf(f, "block_nonzero %d\n", target_nz);
            std::fprintf(f, "block_dc %d\n", target_zz[0]);
            std::fprintf(f, "block_dc_diff %d\n", target_dc_diff);
            std::fprintf(f, "block_ac_symbols %zu\n", target_ac.size());
            std::fprintf(f, "block_raw_bits %d\n", 64 * 8);
            std::fprintf(f, "block_entropy_bits %zu\n", target_bits);
            std::fprintf(f, "num_blocks %d\n", nblocks);
            std::fprintf(f, "total_raw_bits %ld\n", total_raw_bits);
            std::fprintf(f, "total_entropy_bits %ld\n", total_entropy_bits);
            std::fprintf(f, "total_stream_bytes %zu\n", stream.size());
            double ratio = total_entropy_bits > 0
                               ? static_cast<double>(total_raw_bits) /
                                     static_cast<double>(total_entropy_bits)
                               : 0.0;
            std::fprintf(f, "compress_ratio %.3f\n", ratio);
            std::fclose(f);
        }
    }

    std::printf(
        "entropy demo done at block (%d,%d): nonzero=%d, raw=512bit, "
        "entropy=%zubit; whole Y: %d blocks, %ld->%ld bit (%.2fx)\n",
        bx, by, target_nz, target_bits, nblocks, total_raw_bits,
        total_entropy_bits,
        static_cast<double>(total_raw_bits) /
            static_cast<double>(total_entropy_bits));
    return 0;
}
