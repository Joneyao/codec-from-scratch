// motion_search_demo.cpp — 用真实图片跑运动估计，导出「速度 vs 质量」配图数据。
//
// 做四件事：
//   1) 读一张真实 PPM（默认 samples/camera_photo.ppm），转成灰度平面当参考帧。
//   2) 把参考帧整体平移一个已知位移（模拟摄像机/物体运动）得到「当前帧」。
//      这样每个块的「真实运动」都是同一个已知向量，便于判断搜索准不准。
//   3) 对图像里若干个真实 16x16 块，分别跑 FullSearch 和 DiamondSearch，
//      记录各自的 points_checked（搜索点数）、找到的 SAD、找到的 MV。
//   4) 导出到 chart_data/motion_search.txt，供 matplotlib 画「点数 vs SAD」
//      的散点/柱状对比图——直观看出菱形搜索点数少一个数量级、SAD 略高或持平。
//
// 全程无手编像素：参考帧来自真实图片，当前帧是它的真实平移版本。
//
// 用法：
//   ./motion_search_demo [ppm 路径] [chart_data 目录] [搜索半径]
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "image_io.h"
#include "motion_search.h"

namespace {

// BT.601 整数近似 RGB -> luma（与本仓库其它 demo 一致）。
inline int Luma(int r, int g, int b) {
    return (77 * r + 150 * g + 29 * b + 128) >> 8;
}

cfs::GrayFrame ToGray(const cfs::RgbImage& img) {
    cfs::GrayFrame f;
    f.width = img.width;
    f.height = img.height;
    f.samples.resize(static_cast<size_t>(f.width) * f.height);
    for (int y = 0; y < f.height; ++y)
        for (int x = 0; x < f.width; ++x)
            f.samples[static_cast<size_t>(y) * f.width + x] =
                Luma(img.r(x, y), img.g(x, y), img.b(x, y));
    return f;
}

// 把参考帧整体平移 (sx,sy)，再叠加一点轻微噪声得到当前帧：
//   cur(x,y) = clip(ref(x - sx, y - sy) + noise)
// 越界用参考帧边界钳（GrayFrame::at 已处理）。
//
// 为什么要加噪声？真实相邻两帧从来不是「像素级完美平移」——总有传感器噪声、
// 光照微变、非刚性形变。若不加噪声，纯整数平移会让最佳匹配点 SAD 恰好为 0，
// 而其余位置的代价面出现大片近乎持平的「平台」，菱形搜索会卡在平台上的局部
// 极小值里出不来，把它显得异常糟（这反而不是真实场景）。加一点噪声让代价面
// 在真解附近平滑单峰，菱形搜索得以沿梯度收敛——这才是它真正的用武之地：
// 代价面大体单峰时，用少一个数量级的点数换到略高或持平的 SAD。
cfs::GrayFrame ShiftFrame(const cfs::GrayFrame& ref, int sx, int sy) {
    cfs::GrayFrame f;
    f.width = ref.width;
    f.height = ref.height;
    f.samples.resize(static_cast<size_t>(f.width) * f.height);
    // 固定种子的确定性伪随机噪声，幅度 [-3,3]，保证结果可复现。
    unsigned int seed = 0x1234u;
    auto next = [&seed]() {
        seed = seed * 1103515245u + 12345u;  // 经典 LCG
        return static_cast<int>((seed >> 16) & 0x7FFF);
    };
    for (int y = 0; y < f.height; ++y) {
        for (int x = 0; x < f.width; ++x) {
            int v = ref.at(x - sx, y - sy) + (next() % 7 - 3);  // ±3
            if (v < 0) v = 0;
            else if (v > 255) v = 255;
            f.samples[static_cast<size_t>(y) * f.width + x] = v;
        }
    }
    return f;
}

}  // namespace

int main(int argc, char** argv) {
    std::string ppm = argc > 1 ? argv[1] : "../../samples/test_pattern.ppm";
    std::string out_dir = argc > 2 ? argv[2] : "chart_data";
    int range = argc > 3 ? std::atoi(argv[3]) : 16;
    if (range < 1) range = 16;

    cfs::RgbImage img;
    if (!cfs::LoadPpm(ppm, img)) {
        std::printf("error: cannot load %s\n", ppm.c_str());
        return 1;
    }
    std::printf("input: %s  (%dx%d)  search range=+-%d\n", ppm.c_str(),
                img.width, img.height, range);

    cfs::GrayFrame ref = ToGray(img);
    // 已知运动：向右 5、向下 -3（向上 3）。当前帧是参考帧的平移版本。
    const int true_sx = 5, true_sy = -3;
    cfs::GrayFrame cur = ShiftFrame(ref, true_sx, true_sy);
    // 当前块内容取自 cur；它在 ref 里的最佳匹配位置相对同位偏移约为
    // (true_sx,true_sy)（因叠了噪声，最佳 SAD 是个小的非零值而非 0）。

    // 均匀取一批真实 16x16 块（留出 range 余量避免完全落在边界外）。
    struct Row {
        int block_id;
        int bx, by;
        cfs::SearchResult full;
        cfs::SearchResult diamond;
    };
    std::vector<Row> rows;

    const int step = 32;  // 块采样间隔
    int block_id = 0;
    long full_points_total = 0, diamond_points_total = 0;
    long full_sad_total = 0, diamond_sad_total = 0;
    int diamond_hits = 0;  // 菱形找到与全搜索同样 MV 的次数

    for (int by = range; by + 16 + range <= ref.height; by += step) {
        for (int bx = range; bx + 16 + range <= ref.width; bx += step) {
            // 抠出当前帧里这个块（行优先，stride = 帧宽），指向 cur 内部。
            const int* blk =
                &cur.samples[static_cast<size_t>(by) * cur.width + bx];
            cfs::SearchResult full =
                cfs::FullSearch(ref, blk, cur.width, bx, by, range);
            cfs::SearchResult diamond =
                cfs::DiamondSearch(ref, blk, cur.width, bx, by, range);

            rows.push_back({block_id, bx, by, full, diamond});
            full_points_total += full.points_checked;
            diamond_points_total += diamond.points_checked;
            full_sad_total += full.sad;
            diamond_sad_total += diamond.sad;
            if (diamond.mv.mvx == full.mv.mvx && diamond.mv.mvy == full.mv.mvy)
                ++diamond_hits;
            ++block_id;
        }
    }

    int n = static_cast<int>(rows.size());
    if (n == 0) {
        std::printf("error: image too small for range=%d\n", range);
        return 1;
    }

    // ---- 打印摘要 ----
    std::printf("\n==== 对 %d 个真实 16x16 块跑运动估计 ====\n", n);
    std::printf("真实运动(平移): MV=(%d,%d)\n", true_sx, true_sy);
    std::printf("%-8s %10s %10s %8s\n", "method", "avg_points", "avg_sad",
                "总点数");
    std::printf("%-8s %10.1f %10.1f %8ld\n", "Full",
                static_cast<double>(full_points_total) / n,
                static_cast<double>(full_sad_total) / n, full_points_total);
    std::printf("%-8s %10.1f %10.1f %8ld\n", "Diamond",
                static_cast<double>(diamond_points_total) / n,
                static_cast<double>(diamond_sad_total) / n,
                diamond_points_total);
    double speedup = diamond_points_total > 0
                         ? static_cast<double>(full_points_total) /
                               diamond_points_total
                         : 0.0;
    double sad_ratio = full_sad_total > 0
                           ? static_cast<double>(diamond_sad_total) /
                                 full_sad_total
                           : 0.0;
    std::printf("菱形搜索点数是全搜索的 1/%.1f（省 %.0f%%）\n", speedup,
                (1.0 - 1.0 / speedup) * 100.0);
    std::printf("菱形搜索总 SAD 是全搜索的 %.3f 倍（>1 表示质量略降）\n",
                sad_ratio);
    std::printf("菱形搜索命中全搜索同一 MV 的块：%d/%d (%.0f%%)\n",
                diamond_hits, n, 100.0 * diamond_hits / n);

    // ---- 导出 chart_data/motion_search.txt ----
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    std::string path = out_dir + "/motion_search.txt";
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        std::printf("error: cannot write %s\n", path.c_str());
        return 1;
    }
    std::fprintf(f,
                 "# H.264 运动估计：全搜索 vs 菱形搜索（真实图片，matplotlib 可读）\n");
    std::fprintf(f, "# image %s %dx%d  range %d  true_mv %d %d\n", ppm.c_str(),
                 img.width, img.height, range, true_sx, true_sy);
    // 每行一条：method block_id points sad mvx mvy bx by
    std::fprintf(f, "# columns: method block_id points sad mvx mvy bx by\n");
    for (const Row& r : rows) {
        std::fprintf(f, "Full %d %d %d %d %d %d %d\n", r.block_id,
                     r.full.points_checked, r.full.sad, r.full.mv.mvx,
                     r.full.mv.mvy, r.bx, r.by);
        std::fprintf(f, "Diamond %d %d %d %d %d %d %d\n", r.block_id,
                     r.diamond.points_checked, r.diamond.sad, r.diamond.mv.mvx,
                     r.diamond.mv.mvy, r.bx, r.by);
    }
    // 汇总段（便于直接读平均值）。
    std::fprintf(f, "[summary]\n");
    std::fprintf(f, "blocks %d\n", n);
    std::fprintf(f, "full_avg_points %.3f\n",
                 static_cast<double>(full_points_total) / n);
    std::fprintf(f, "diamond_avg_points %.3f\n",
                 static_cast<double>(diamond_points_total) / n);
    std::fprintf(f, "full_avg_sad %.3f\n",
                 static_cast<double>(full_sad_total) / n);
    std::fprintf(f, "diamond_avg_sad %.3f\n",
                 static_cast<double>(diamond_sad_total) / n);
    std::fprintf(f, "speedup_x %.3f\n", speedup);
    std::fprintf(f, "diamond_sad_ratio %.4f\n", sad_ratio);
    std::fprintf(f, "diamond_mv_hit %d %d\n", diamond_hits, n);
    std::fclose(f);
    std::printf("\nchart data -> %s\n", path.c_str());
    return 0;
}
