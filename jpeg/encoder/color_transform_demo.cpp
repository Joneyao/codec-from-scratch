// color_transform_demo.cpp — A1 配套：读入一张 PPM，做 RGB→YCbCr + 4:2:0 抽样，
// 导出真实中间数据供文章配图使用。
//
// 用法:
//   ./color_transform_demo input.ppm out_dir/
// 产出（out_dir 下）:
//   y_plane.ppm / cb_plane.ppm / cr_plane.ppm      各分量灰度图
//   y_block.txt / cb_full_block.txt / cb_420_block.txt  一个局部块的数值矩阵
//   stats.txt                                        分辨率/数据量统计
#include <cstdio>
#include <string>

#include "color_transform.h"
#include "image_io.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s input.ppm out_dir/\n", argv[0]);
        return 1;
    }
    const std::string in_path = argv[1];
    std::string out_dir = argv[2];
    if (!out_dir.empty() && out_dir.back() != '/') out_dir += '/';

    cfs::RgbImage rgb;
    if (!cfs::LoadPpm(in_path, rgb)) {
        std::fprintf(stderr, "failed to load PPM: %s\n", in_path.c_str());
        return 2;
    }

    // 先算一份 4:4:4（不抽样）用于对照，再算 4:2:0。
    cfs::YCbCrImage img420 =
        cfs::RgbToYCbCr(rgb, cfs::ChromaSubsampling::k420);
    cfs::YCbCrImage img444 =
        cfs::RgbToYCbCr(rgb, cfs::ChromaSubsampling::k444);

    cfs::SavePlaneAsGrayPpm(img420.y, out_dir + "y_plane.ppm");
    cfs::SavePlaneAsGrayPpm(img420.cb, out_dir + "cb_plane.ppm");
    cfs::SavePlaneAsGrayPpm(img420.cr, out_dir + "cr_plane.ppm");

    // 在 Cb 全分辨率平面上扫描，找方差最大的 8x8 块（最有代表性、能体现抽样差异）。
    int bx = 0, by = 0;
    long best_var = -1;
    for (int y = 0; y + 8 <= img444.cb.height; y += 8) {
        for (int x = 0; x + 8 <= img444.cb.width; x += 8) {
            long sum = 0, sum2 = 0;
            for (int dy = 0; dy < 8; ++dy)
                for (int dx = 0; dx < 8; ++dx) {
                    int v = img444.cb.at(x + dx, y + dy);
                    sum += v;
                    sum2 += static_cast<long>(v) * v;
                }
            long var = sum2 - sum * sum / 64;
            if (var > best_var) {
                best_var = var;
                bx = x;
                by = y;
            }
        }
    }
    cfs::DumpPlaneRegion(img420.y, bx, by, 8, 8, out_dir + "y_block.txt");
    // 全分辨率 Cb 的同一 8x8 区域，与抽样后 4x4 区域对照。
    cfs::DumpPlaneRegion(img444.cb, bx, by, 8, 8,
                         out_dir + "cb_full_block.txt");
    cfs::DumpPlaneRegion(img420.cb, bx / 2, by / 2, 4, 4,
                         out_dir + "cb_420_block.txt");

    // 统计：数据量对比（4:4:4 vs 4:2:0）。
    long full = static_cast<long>(rgb.width) * rgb.height * 3;
    long y_bytes = static_cast<long>(img420.y.width) * img420.y.height;
    long c420 = static_cast<long>(img420.cb.width) * img420.cb.height * 2;
    long c444 = static_cast<long>(img444.cb.width) * img444.cb.height * 2;
    FILE* f = std::fopen((out_dir + "stats.txt").c_str(), "w");
    if (f) {
        std::fprintf(f, "width %d\nheight %d\n", rgb.width, rgb.height);
        std::fprintf(f, "rgb_bytes %ld\n", full);
        std::fprintf(f, "ycbcr_444_bytes %ld\n", y_bytes + c444);
        std::fprintf(f, "ycbcr_420_bytes %ld\n", y_bytes + c420);
        std::fprintf(f, "y_bytes %ld\n", y_bytes);
        std::fprintf(f, "chroma_444_bytes %ld\n", c444);
        std::fprintf(f, "chroma_420_bytes %ld\n", c420);
        std::fclose(f);
    }

    std::printf("done. Y=%dx%d Cb=%dx%d (420)\n", img420.y.width,
                img420.y.height, img420.cb.width, img420.cb.height);
    return 0;
}
