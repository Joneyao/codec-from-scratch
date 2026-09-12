// dct8x8_demo.cpp — A2 配套：从真实图片取 Y 平面的一个 8x8 块，做 DCT，
// 导出像素块 / DCT 系数 / 重建块 / 能量分布，供文章配图。
//
// 用法: ./dct8x8_demo input.ppm out_dir/
#include <cmath>
#include <cstdio>
#include <string>

#include "color_transform.h"
#include "dct8x8.h"
#include "image_io.h"

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

    // 在 Y 平面上挑一个方差最大的 8x8 块（细节最丰富，DCT 效果最明显）。
    int bx = 0, by = 0;
    long best = -1;
    for (int y = 0; y + 8 <= img.y.height; y += 8)
        for (int x = 0; x + 8 <= img.y.width; x += 8) {
            long s = 0, s2 = 0;
            for (int dy = 0; dy < 8; ++dy)
                for (int dx = 0; dx < 8; ++dx) {
                    int v = img.y.at(x + dx, y + dy);
                    s += v;
                    s2 += (long)v * v;
                }
            long var = s2 - s * s / 64;
            if (var > best) { best = var; bx = x; by = y; }
        }

    // 同时找一个方差最小（最平滑）的块做对照。
    int sx = 0, sy = 0;
    long worst = -1;  // 用 -var 找最小，这里直接记最小 var
    long min_var = -1;
    for (int y = 0; y + 8 <= img.y.height; y += 8)
        for (int x = 0; x + 8 <= img.y.width; x += 8) {
            long s = 0, s2 = 0;
            for (int dy = 0; dy < 8; ++dy)
                for (int dx = 0; dx < 8; ++dx) {
                    int v = img.y.at(x + dx, y + dy);
                    s += v;
                    s2 += (long)v * v;
                }
            long var = s2 - s * s / 64;
            if (var > 0 && (min_var < 0 || var < min_var)) {
                min_var = var; sx = x; sy = y;
            }
        }
    (void)worst;

    cfs::Block8u pixels{};
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            pixels[y][x] = img.y.at(bx + x, by + y);

    cfs::Block8d coeffs = cfs::ForwardDct(pixels);
    cfs::Block8u recon = cfs::InverseDct(coeffs);

    cfs::DumpBlockU8(pixels, out_dir + "dct_pixels.txt");
    cfs::DumpBlock(coeffs, out_dir + "dct_coeffs.txt", true);
    cfs::DumpBlockU8(recon, out_dir + "dct_recon.txt");

    // 平滑块的像素与系数（对照用）。
    cfs::Block8u smooth{};
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            smooth[y][x] = img.y.at(sx + x, sy + y);
    cfs::Block8d smooth_coeffs = cfs::ForwardDct(smooth);
    cfs::DumpBlockU8(smooth, out_dir + "dct_smooth_pixels.txt");
    cfs::DumpBlock(smooth_coeffs, out_dir + "dct_smooth_coeffs.txt", true);

    // 统计能量分布：DC 能量占比、左上 4x4 低频区能量占比、非零系数个数。
    double total = 0, dc = 0, low4x4 = 0;
    int nonzero_all = 0, nonzero_rounded = 0;
    for (int v = 0; v < 8; ++v)
        for (int u = 0; u < 8; ++u) {
            double e = coeffs[v][u] * coeffs[v][u];
            total += e;
            if (u == 0 && v == 0) dc = e;
            if (u < 4 && v < 4) low4x4 += e;
            if (std::fabs(coeffs[v][u]) > 1e-6) ++nonzero_all;
            if (std::lround(coeffs[v][u]) != 0) ++nonzero_rounded;
        }
    FILE* f = std::fopen((out_dir + "dct_stats.txt").c_str(), "w");
    if (f) {
        std::fprintf(f, "block_x %d\nblock_y %d\n", bx, by);
        std::fprintf(f, "dc_value %.2f\n", coeffs[0][0]);
        std::fprintf(f, "energy_total %.1f\n", total);
        std::fprintf(f, "energy_dc_pct %.2f\n", dc / total * 100.0);
        std::fprintf(f, "energy_low4x4_pct %.2f\n", low4x4 / total * 100.0);
        std::fprintf(f, "nonzero_all %d\n", nonzero_all);
        std::fprintf(f, "nonzero_rounded %d\n", nonzero_rounded);
        std::fclose(f);
    }

    std::printf("DCT done at block (%d,%d), DC=%.1f, low4x4 energy=%.1f%%\n",
                bx, by, coeffs[0][0], low4x4 / total * 100.0);
    return 0;
}
