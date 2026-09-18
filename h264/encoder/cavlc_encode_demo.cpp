// cavlc_encode_demo.cpp — 用一个贴近自然图像残差的真实量化块，跑一遍 CAVLC 正向
// 编码，把五个语法元素各自的比特占用、总比特数，以及“原始系数 vs 编码比特”的
// 压缩效果导出到 chart_data/cavlc_encode.txt，供文章配图。
//
// 揭示点：一个 4x4 块名义上是 16 个 int（若每个按 32 位存 = 512 bit），但经过
// 变换量化后系数稀疏、尾部多 ±1、末尾一长串 0，CAVLC 把它榨到几十 bit。
//
// 直接 g++ 编译运行（不依赖 CMake），把下面这行接成一条命令：
//   g++ -std=c++17 -I. -I../decoder -I../../common cavlc_encode_demo.cpp
//       cavlc_encode.cpp ../decoder/cavlc_decode.cpp
//       ../decoder/rbsp_bit_reader.cpp ../../common/bitwriter.cpp -o /tmp/demo
#include <array>
#include <cstdio>
#include <vector>

#include "cavlc_encode.h"
#include "../decoder/cavlc_decode.h"
#include "../decoder/rbsp_bit_reader.h"

int main() {
    // 一个教学用的 4x4 量化系数块（二维，行优先），形状贴近自然图像残差：
    // 能量集中在左上角低频，右下角高频几乎全 0，斜对角带几个尾部 ±1。
    const std::array<std::array<int, 4>, 4> block = {{
        {13, -3,  2,  0},
        {-4,  1, -1,  0},
        { 2,  1,  0,  0},
        { 0,  0,  0,  0},
    }};

    // 先把二维块按 zigzag 正向扫描成扫描顺序的 16 个系数（编码器输入）。
    cfs::Cavlc4x4 scan = cfs::ForwardZigzag4x4(block);

    // 上下文 nc：取 2（命中解码器正确码表，便于本 demo 顺带做一次往返自证）。
    const int nc = 2;

    cfs::BitWriter bw;
    cfs::CavlcEncodeStats st = cfs::EncodeResidual4x4(bw, scan, nc);
    std::vector<uint8_t> bytes = bw.TakeBytes();

    // 往返自证：解回来应与输入完全一致。
    cfs::RbspBitReader br(bytes.data(), bytes.size());
    cfs::CavlcResult dec = cfs::DecodeResidual4x4(br, nc);
    bool roundtrip_ok = true;
    for (int i = 0; i < 16; ++i)
        if (dec.coeff[i] != scan[i]) roundtrip_ok = false;

    // 统计非零系数个数，用于“原始系数个数 vs 编码比特数”对比。
    int nonzero = 0;
    for (int i = 0; i < 16; ++i) if (scan[i] != 0) ++nonzero;

    // 原始表示的比特占用参照：16 个系数若按每个 32 位裸存 = 512 bit。
    const int raw_bits_32 = 16 * 32;
    // 更“公平”的裸存参照：每个系数按 9 位有符号（-256..255 足够本量级）= 144 bit。
    const int raw_bits_9 = 16 * 9;

    std::FILE* fp = std::fopen("chart_data/cavlc_encode.txt", "w");
    if (!fp) {
        std::fprintf(stderr, "无法写入 chart_data/cavlc_encode.txt（请在 encoder/ 目录下运行）\n");
        return 1;
    }
    std::fprintf(fp, "# H.264 CAVLC 正向编码 —— 一个真实 4x4 量化块的比特分解\n");
    std::fprintf(fp, "# 块（zigzag 扫描顺序）:");
    for (int i = 0; i < 16; ++i) std::fprintf(fp, " %d", scan[i]);
    std::fprintf(fp, "\n");
    std::fprintf(fp, "nc\t%d\n", nc);
    std::fprintf(fp, "total_coeff\t%d\n", st.total_coeff);
    std::fprintf(fp, "trailing_ones\t%d\n", st.trailing_ones);
    std::fprintf(fp, "total_zeros\t%d\n", st.total_zeros);
    std::fprintf(fp, "roundtrip_ok\t%d\n", roundtrip_ok ? 1 : 0);
    std::fprintf(fp, "\n# 五个语法元素各自的比特数\n");
    std::fprintf(fp, "bits_coeff_token\t%zu\n", st.bits_coeff_token);
    std::fprintf(fp, "bits_trailing_ones_sign\t%zu\n", st.bits_sign);
    std::fprintf(fp, "bits_level\t%zu\n", st.bits_level);
    std::fprintf(fp, "bits_total_zeros\t%zu\n", st.bits_total_zeros);
    std::fprintf(fp, "bits_run_before\t%zu\n", st.bits_run_before);
    std::fprintf(fp, "bits_total\t%zu\n", st.bits_total);
    std::fprintf(fp, "\n# 压缩效果：原始系数 vs 编码比特\n");
    std::fprintf(fp, "coeff_count\t16\n");
    std::fprintf(fp, "nonzero_count\t%d\n", nonzero);
    std::fprintf(fp, "raw_bits_32bit\t%d\n", raw_bits_32);
    std::fprintf(fp, "raw_bits_9bit\t%d\n", raw_bits_9);
    std::fprintf(fp, "encoded_bits\t%zu\n", st.bits_total);
    std::fprintf(fp, "compress_ratio_vs_32bit\t%.2f\n",
                 static_cast<double>(raw_bits_32) / static_cast<double>(st.bits_total));
    std::fprintf(fp, "compress_ratio_vs_9bit\t%.2f\n",
                 static_cast<double>(raw_bits_9) / static_cast<double>(st.bits_total));
    std::fclose(fp);

    std::printf("CAVLC 编码完成：total_coeff=%d trailing_ones=%d total_zeros=%d\n",
                st.total_coeff, st.trailing_ones, st.total_zeros);
    std::printf("  coeff_token=%zu  sign=%zu  level=%zu  total_zeros=%zu  run_before=%zu\n",
                st.bits_coeff_token, st.bits_sign, st.bits_level,
                st.bits_total_zeros, st.bits_run_before);
    std::printf("  总比特=%zu bit （%d 系数：512bit@32位裸存 -> %zu bit，压缩 %.1fx）\n",
                st.bits_total, 16, st.bits_total,
                static_cast<double>(raw_bits_32) / static_cast<double>(st.bits_total));
    std::printf("  往返自证: %s\n", roundtrip_ok ? "OK（解回与输入一致）" : "失败");
    std::printf("  已写入 chart_data/cavlc_encode.txt\n");
    return roundtrip_ok ? 0 : 1;
}
