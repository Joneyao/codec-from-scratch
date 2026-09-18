// intra_mode_demo.cpp — 用真实图片跑帧内模式决策，导出配图数据。
//
// 做四件事：
//   1) 读一张真实 PPM（默认 samples/test_pattern.ppm），转成亮度平面。
//   2) 扫全图找一个「方向性最强」的 4x4 块——即最优模式远胜次优模式（两者
//      SAD 差距最大）的块。这种块最能体现「模式决策为什么值得做」。
//   3) 对该块跑全部 9 种模式，用 SAD 和 SATD 各选一次，打印每种模式的代价。
//   4) 再取一个真实 16x16 宏块跑 4 种模式。把「每种模式名 + SAD」写成文本
//      到 chart_data/intra_mode_sad.txt，供 matplotlib 画柱状图；并单独导出
//      「选中模式 vs 次优模式」的对比数据。
//
// 全程无手编像素：邻居和原始块都来自图片实际像素。
//
// 用法：
//   ./intra_mode_demo ../../samples/test_pattern.ppm [chart_data]
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "image_io.h"
#include "intra_mode_decision.h"
#include "intra_predict.h"

namespace {

// BT.601 整数近似 RGB -> luma（与本仓库 JPEG/H.264 其他 demo 一致）。
inline int Luma(int r, int g, int b) {
    return (77 * r + 150 * g + 29 * b + 128) >> 8;
}

struct LumaPlane {
    int w = 0, h = 0;
    std::vector<int> y;
    int at(int px, int py) const { return y[py * w + px]; }
};

LumaPlane ToLuma(const cfs::RgbImage& img) {
    LumaPlane lp;
    lp.w = img.width;
    lp.h = img.height;
    lp.y.resize(static_cast<size_t>(lp.w) * lp.h);
    for (int j = 0; j < lp.h; ++j)
        for (int i = 0; i < lp.w; ++i)
            lp.y[j * lp.w + i] = Luma(img.r(i, j), img.g(i, j), img.b(i, j));
    return lp;
}

cfs::Block4x4 Grab4x4(const LumaPlane& lp, int bx, int by) {
    cfs::Block4x4 b{};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) b[y][x] = lp.at(bx + x, by + y);
    return b;
}

cfs::Neighbors4x4 Grab4x4Neighbors(const LumaPlane& lp, int bx, int by) {
    cfs::Neighbors4x4 nb;
    for (int i = 0; i < 8; ++i) nb.top[i] = lp.at(bx + i, by - 1);
    for (int i = 0; i < 4; ++i) nb.left[i] = lp.at(bx - 1, by + i);
    nb.top_left = lp.at(bx - 1, by - 1);
    return nb;
}

cfs::Block16x16 Grab16x16(const LumaPlane& lp, int bx, int by) {
    cfs::Block16x16 b{};
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) b[y][x] = lp.at(bx + x, by + y);
    return b;
}

cfs::Neighbors16x16 Grab16x16Neighbors(const LumaPlane& lp, int bx, int by) {
    cfs::Neighbors16x16 nb;
    for (int i = 0; i < 16; ++i) nb.top[i] = lp.at(bx + i, by - 1);
    for (int i = 0; i < 16; ++i) nb.left[i] = lp.at(bx - 1, by + i);
    nb.top_left = lp.at(bx - 1, by - 1);
    return nb;
}

// 跑一个 4x4 块的 9 种模式，收集 SAD。
struct Run4x4 {
    std::array<int, 9> sad{};
    int best = 0, second = 0;
};

Run4x4 RunAll4x4(const cfs::Neighbors4x4& nb, const cfs::Block4x4& orig) {
    Run4x4 r;
    int best_cost = 1 << 30, second_cost = 1 << 30;
    for (int m = 0; m < 9; ++m) {
        cfs::Block4x4 pred =
            cfs::PredictIntra4x4(nb, static_cast<cfs::Intra4x4Mode>(m));
        r.sad[m] = cfs::Sad4x4Cost(orig, pred);
        if (r.sad[m] < best_cost) {
            second_cost = best_cost;
            r.second = r.best;
            best_cost = r.sad[m];
            r.best = m;
        } else if (r.sad[m] < second_cost) {
            second_cost = r.sad[m];
            r.second = m;
        }
    }
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    std::string ppm = argc > 1 ? argv[1] : "../../samples/test_pattern.ppm";
    std::string out_dir = argc > 2 ? argv[2] : "chart_data";

    cfs::RgbImage img;
    if (!cfs::LoadPpm(ppm, img)) {
        std::printf("error: cannot load %s\n", ppm.c_str());
        return 1;
    }
    std::printf("input: %s  (%dx%d)\n", ppm.c_str(), img.width, img.height);
    LumaPlane lp = ToLuma(img);

    // ---- 扫全图找「方向性最强」的 4x4 块：最优与次优 SAD 差距最大 ----
    // 这种块换个模式代价就明显变差，最能说明模式决策的价值。
    int best_x = 1, best_y = 1, best_gap = -1;
    Run4x4 best_run{};
    for (int by = 1; by + 4 <= lp.h; by += 4) {
        for (int bx = 1; bx + 8 <= lp.w; bx += 4) {  // +8 留出右上延伸余量
            cfs::Block4x4 orig = Grab4x4(lp, bx, by);
            cfs::Neighbors4x4 nb = Grab4x4Neighbors(lp, bx, by);
            Run4x4 r = RunAll4x4(nb, orig);
            int gap = r.sad[r.second] - r.sad[r.best];
            if (gap > best_gap) {
                best_gap = gap;
                best_x = bx;
                best_y = by;
                best_run = r;
            }
        }
    }

    cfs::Block4x4 orig4 = Grab4x4(lp, best_x, best_y);
    cfs::Neighbors4x4 nb4 = Grab4x4Neighbors(lp, best_x, best_y);

    int sad_cost = -1, satd_cost = -1;
    int mode_sad = cfs::ChooseIntra4x4Mode(nb4, orig4, cfs::CostMetric::kSad,
                                           &sad_cost);
    int mode_satd = cfs::ChooseIntra4x4Mode(nb4, orig4, cfs::CostMetric::kSatd,
                                            &satd_cost);

    std::printf("\n==== 方向性最强的 4x4 块 @(%d,%d) ====\n", best_x, best_y);
    std::printf("9 种模式的 SAD：\n");
    for (int m = 0; m < 9; ++m)
        std::printf("  模式%d %-32s SAD=%d\n", m,
                    cfs::Intra4x4ModeName(static_cast<cfs::Intra4x4Mode>(m)),
                    best_run.sad[m]);
    std::printf("SAD  选中模式%d(%s) 代价=%d\n", mode_sad,
                cfs::Intra4x4ModeName(static_cast<cfs::Intra4x4Mode>(mode_sad)),
                sad_cost);
    std::printf("SATD 选中模式%d(%s) 代价=%d\n", mode_satd,
                cfs::Intra4x4ModeName(static_cast<cfs::Intra4x4Mode>(mode_satd)),
                satd_cost);
    std::printf("次优模式%d(%s) SAD=%d，比最优多 %d\n", best_run.second,
                cfs::Intra4x4ModeName(
                    static_cast<cfs::Intra4x4Mode>(best_run.second)),
                best_run.sad[best_run.second], best_gap);

    // ---- 取一个真实 16x16 宏块（图像中心附近，四周有邻居）----
    int mb_x = ((lp.w / 2) & ~15);
    int mb_y = ((lp.h / 2) & ~15);
    if (mb_x < 16) mb_x = 16;
    if (mb_y < 16) mb_y = 16;
    if (mb_x + 16 > lp.w) mb_x = lp.w - 16;
    if (mb_y + 16 > lp.h) mb_y = lp.h - 16;
    cfs::Block16x16 orig16 = Grab16x16(lp, mb_x, mb_y);
    cfs::Neighbors16x16 nb16 = Grab16x16Neighbors(lp, mb_x, mb_y);

    std::array<int, 4> sad16{};
    for (int m = 0; m < 4; ++m) {
        cfs::Block16x16 pred =
            cfs::PredictIntra16x16(nb16, static_cast<cfs::Intra16x16Mode>(m));
        sad16[m] = cfs::Sad16x16Cost(orig16, pred);
    }
    int c16 = -1;
    cfs::Intra16x16Mode m16 =
        cfs::ChooseIntra16x16Mode(nb16, orig16, cfs::CostMetric::kSad, &c16);
    std::printf("\n==== 真实 16x16 宏块 @(%d,%d) ====\n", mb_x, mb_y);
    for (int m = 0; m < 4; ++m)
        std::printf("  模式%d %-24s SAD=%d\n", m,
                    cfs::Intra16x16ModeName(
                        static_cast<cfs::Intra16x16Mode>(m)),
                    sad16[m]);
    std::printf("SAD 选中 %s 代价=%d\n", cfs::Intra16x16ModeName(m16), c16);

    // ---- 导出 chart_data/intra_mode_sad.txt（供 matplotlib 读）----
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    std::string path = out_dir + "/intra_mode_sad.txt";
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        std::printf("error: cannot write %s\n", path.c_str());
        return 1;
    }
    std::fprintf(f, "# H.264 帧内模式决策 SAD 数据（真实图片，可用于 matplotlib）\n");
    std::fprintf(f, "# image %s %dx%d\n", ppm.c_str(), img.width, img.height);
    // 段一：4x4 块 9 种模式的 SAD（格式：mode_name sad，每行一个）。
    std::fprintf(f, "[intra4x4] block=(%d,%d)\n", best_x, best_y);
    for (int m = 0; m < 9; ++m)
        std::fprintf(f, "%s %d\n",
                     cfs::Intra4x4ModeShort(static_cast<cfs::Intra4x4Mode>(m)),
                     best_run.sad[m]);
    // 段二：16x16 宏块 4 种模式的 SAD。
    std::fprintf(f, "[intra16x16] block=(%d,%d)\n", mb_x, mb_y);
    const char* n16[4] = {"V", "H", "DC", "Plane"};
    for (int m = 0; m < 4; ++m) std::fprintf(f, "%s %d\n", n16[m], sad16[m]);
    // 段三：选中模式 vs 次优模式对比。
    std::fprintf(f, "[best_vs_second]\n");
    std::fprintf(f, "best %s %d\n",
                 cfs::Intra4x4ModeShort(static_cast<cfs::Intra4x4Mode>(best_run.best)),
                 best_run.sad[best_run.best]);
    std::fprintf(f, "second %s %d\n",
                 cfs::Intra4x4ModeShort(static_cast<cfs::Intra4x4Mode>(best_run.second)),
                 best_run.sad[best_run.second]);
    std::fprintf(f, "gap %d\n", best_gap);
    std::fclose(f);
    std::printf("\nchart data -> %s\n", path.c_str());
    return 0;
}
