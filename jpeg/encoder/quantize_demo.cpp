// quantize_demo.cpp — A3 配套：从真实图片取 Y 平面一个块，DCT 后用不同质量
// 量化 / 反量化，导出量化表、量化前后系数、以及各质量下的统计，供文章配图。
//
// 用法: ./quantize_demo input.ppm out_dir/
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "color_transform.h"
#include "dct8x8.h"
#include "image_io.h"
#include "quantize.h"

namespace {

// 在 Y 平面挑一个方差最大（细节最丰富）的 8x8 块，量化效果最直观。
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

// 统计一个块：非零系数个数。
int CountNonZero(const cfs::Block8d& b) {
    int n = 0;
    for (int v = 0; v < 8; ++v)
        for (int u = 0; u < 8; ++u)
            if (std::lround(b[v][u]) != 0) ++n;
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

    int bx = 0, by = 0;
    PickHighVarBlock(img.y, bx, by);

    cfs::Block8u pixels{};
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            pixels[y][x] = img.y.at(bx + x, by + y);

    cfs::Block8d coeffs = cfs::ForwardDct(pixels);

    // 1) 导出 Q=50 基准量化表（亮度、色度）。
    cfs::DumpQuantTable(cfs::kBaseLuma, out_dir + "q_luma_q50.txt");
    cfs::DumpQuantTable(cfs::kBaseChroma, out_dir + "q_chroma_q50.txt");

    // 2) 导出真实块的 DCT 系数（量化前）。
    cfs::DumpBlock(coeffs, out_dir + "q_coeffs_dct.txt", true);

    // 3) 用 Q=90 / 50 / 10 三档量化，各导出：量化表、量化后系数、反量化系数。
    const int kQualities[] = {90, 50, 10};
    for (int q : kQualities) {
        cfs::QuantTable table = cfs::ScaleTable(cfs::kBaseLuma, q);
        cfs::Block8d quant = cfs::Quantize(coeffs, table);
        cfs::Block8d dequant = cfs::Dequantize(quant, table);

        char name[64];
        std::snprintf(name, sizeof(name), "q_table_q%d.txt", q);
        cfs::DumpQuantTable(table, out_dir + name);
        std::snprintf(name, sizeof(name), "q_quant_q%d.txt", q);
        cfs::DumpBlock(quant, out_dir + name, true);
        std::snprintf(name, sizeof(name), "q_dequant_q%d.txt", q);
        cfs::DumpBlock(dequant, out_dir + name, true);
    }

    // 4) 扫描 quality 1..100，统计非零系数个数、块内均方误差(反量化 vs 原系数)、
    //    以及一个粗略的"压缩率代理"(非零占比)。写成一张表供画曲线。
    {
        FILE* f = std::fopen((out_dir + "q_sweep.txt").c_str(), "w");
        if (f) {
            std::fprintf(f, "# quality nonzero rmse_coeff\n");
            for (int q = 1; q <= 100; ++q) {
                cfs::QuantTable table = cfs::ScaleTable(cfs::kBaseLuma, q);
                cfs::Block8d quant = cfs::Quantize(coeffs, table);
                cfs::Block8d dequant = cfs::Dequantize(quant, table);
                int nz = CountNonZero(quant);
                double se = 0.0;
                for (int v = 0; v < 8; ++v)
                    for (int u = 0; u < 8; ++u) {
                        double d = coeffs[v][u] - dequant[v][u];
                        se += d * d;
                    }
                double rmse = std::sqrt(se / 64.0);
                std::fprintf(f, "%d %d %.4f\n", q, nz, rmse);
            }
            std::fclose(f);
        }
    }

    // 5) 关键统计摘要（文章正文引用的真实数字）。
    {
        FILE* f = std::fopen((out_dir + "q_stats.txt").c_str(), "w");
        if (f) {
            std::fprintf(f, "block_x %d\nblock_y %d\n", bx, by);
            std::fprintf(f, "nonzero_dct %d\n", CountNonZero(coeffs));
            for (int q : kQualities) {
                cfs::QuantTable table = cfs::ScaleTable(cfs::kBaseLuma, q);
                cfs::Block8d quant = cfs::Quantize(coeffs, table);
                std::fprintf(f, "nonzero_q%d %d\n", q, CountNonZero(quant));
                std::fprintf(f, "step_dc_q%d %d\n", q, table[0][0]);
                std::fprintf(f, "step_hf_q%d %d\n", q, table[7][7]);
            }
            std::fclose(f);
        }
    }

    std::printf("quantize demo done at block (%d,%d): DCT nonzero=%d, "
                "Q90 nz=%d, Q50 nz=%d, Q10 nz=%d\n",
                bx, by, CountNonZero(coeffs),
                CountNonZero(cfs::Quantize(
                    coeffs, cfs::ScaleTable(cfs::kBaseLuma, 90))),
                CountNonZero(cfs::Quantize(
                    coeffs, cfs::ScaleTable(cfs::kBaseLuma, 50))),
                CountNonZero(cfs::Quantize(
                    coeffs, cfs::ScaleTable(cfs::kBaseLuma, 10))));
    return 0;
}
