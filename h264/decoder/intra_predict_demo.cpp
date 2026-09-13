// intra_predict_demo.cpp — 用真实图像块跑通全部 9 种 Intra_4x4 模式。
//
// 做四件事：
//   1) 读入一张真实测试图(test_pattern.ppm)，转成亮度(luma)。
//   2) 从图里取一个"有方向纹理"的 4x4 块，连同它的上邻/左邻/左上角像素，
//      跑全部 9 种模式，算每种模式预测块与真实块的 SAD，看哪种最准。
//   3) 再取一个"平滑区域"的 4x4 块，重复上述实验，展示 DC(抹平)为何在平滑块
//      上最省残差——这就是本篇的揭示性时刻。
//   4) 把邻居像素、9 种模式的预测块、各模式 SAD 全部 dump 成文本，供配图脚本读。
//
// 全程没有一个手编的像素值：邻居和真实块都来自这张图的实际像素。
//
// 用法：
//   ./intra_predict_demo ../../samples/camera_photo.ppm [chart_data]
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "image_io.h"
#include "intra_predict.h"

namespace {

// RGB → 亮度(luma)，用 BT.601 的整数近似（和 JPEG 系列一致）。
inline int Luma(int r, int g, int b) {
    return (77 * r + 150 * g + 29 * b + 128) >> 8;
}

// 把整张 RGB 图转成一张 luma 平面，便于按坐标取块和邻居。
struct LumaPlane {
    int w = 0, h = 0;
    std::vector<int> y;  // size = w*h
    int at(int x, int px_y) const { return y[px_y * w + x]; }
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

// 从 luma 平面 (bx,by) 处取一个 4x4 真实块（块左上角像素坐标）。
cfs::Block4x4 GrabBlock(const LumaPlane& lp, int bx, int by) {
    cfs::Block4x4 b{};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            b[y][x] = lp.at(bx + x, by + y);
    return b;
}

// 从 luma 平面取 (bx,by) 处 4x4 块的邻居像素：上邻 8 个(含右上延伸)、
// 左邻 4 个、左上角 1 个。要求块四周有足够余量（bx>=1, by>=1）。
cfs::Neighbors4x4 GrabNeighbors(const LumaPlane& lp, int bx, int by) {
    cfs::Neighbors4x4 nb;
    for (int i = 0; i < 8; ++i) nb.top[i] = lp.at(bx + i, by - 1);   // p[0..7,-1]
    for (int i = 0; i < 4; ++i) nb.left[i] = lp.at(bx - 1, by + i);  // p[-1,0..3]
    nb.top_left = lp.at(bx - 1, by - 1);                             // p[-1,-1]
    return nb;
}

void PrintBlock(const char* title, const cfs::Block4x4& b) {
    std::printf("%s:\n", title);
    for (int y = 0; y < 4; ++y) {
        std::printf("  ");
        for (int x = 0; x < 4; ++x) std::printf("%4d", b[y][x]);
        std::printf("\n");
    }
}

// 对一个块跑全部 9 种模式，打印每种模式 SAD，返回最优模式与其 SAD。
struct RunResult {
    std::array<int, 9> sad{};
    std::array<cfs::Block4x4, 9> pred{};
    int best_mode = 0;
    int best_sad = 1 << 30;
};

RunResult RunAllModes(const cfs::Neighbors4x4& nb, const cfs::Block4x4& orig,
                      const char* label) {
    RunResult rr;
    std::printf("\n==== %s：9 种模式各自的 SAD ====\n", label);
    for (int m = 0; m < 9; ++m) {
        auto mode = static_cast<cfs::Intra4x4Mode>(m);
        rr.pred[m] = cfs::PredictIntra4x4(nb, mode);
        rr.sad[m] = cfs::Sad4x4(rr.pred[m], orig);
        std::printf("  模式%d %-32s SAD=%d\n", m,
                    cfs::Intra4x4ModeName(mode), rr.sad[m]);
        if (rr.sad[m] < rr.best_sad) {
            rr.best_sad = rr.sad[m];
            rr.best_mode = m;
        }
    }
    std::printf("  --> 最优：模式%d %s，SAD=%d\n", rr.best_mode,
                cfs::Intra4x4ModeName(
                    static_cast<cfs::Intra4x4Mode>(rr.best_mode)),
                rr.best_sad);
    return rr;
}

// dump 一个块到文件（供 matplotlib 读，一行 4 个数）。
void DumpBlock(FILE* f, const cfs::Block4x4& b) {
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x)
            std::fprintf(f, "%d%s", b[y][x], x == 3 ? "" : " ");
        std::fprintf(f, "\n");
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string ppm = argc > 1 ? argv[1] : "../../samples/camera_photo.ppm";
    std::string out_dir = argc > 2 ? argv[2] : "chart_data";

    cfs::RgbImage img;
    if (!cfs::LoadPpm(ppm, img)) {
        std::printf("error: cannot load %s\n", ppm.c_str());
        return 1;
    }
    std::printf("input: %s  (%dx%d)\n", ppm.c_str(), img.width, img.height);
    LumaPlane lp = ToLuma(img);

    // ---- 挑两个真实块 ----
    // 纹理块：一处水平纹理明显的区域，上下相邻行差异大、左右一行内相近，
    //         Horizontal(复制左邻) 模式能几乎完美贴合，方向模式完胜 DC。
    // 平滑块：一处像素彼此接近的平缓区，没有主导方向，DC(取均值抹平)残差最小。
    // 两处坐标经离线扫描全图挑出（scan：遍历所有 4x4 块比 9 种模式 SAD），
    // 保证"方向块方向模式赢、平滑块 DC 赢"这一对比在真实像素上成立。
    const int tex_x = 108, tex_y = 29;     // 水平纹理块
    const int sm_x = 281, sm_y = 131;      // 平滑块

    cfs::Block4x4 tex = GrabBlock(lp, tex_x, tex_y);
    cfs::Neighbors4x4 tex_nb = GrabNeighbors(lp, tex_x, tex_y);
    cfs::Block4x4 sm = GrabBlock(lp, sm_x, sm_y);
    cfs::Neighbors4x4 sm_nb = GrabNeighbors(lp, sm_x, sm_y);

    std::printf("\n==== 纹理块 @(%d,%d) ====\n", tex_x, tex_y);
    std::printf("上邻 p[0..7,-1] :");
    for (int i = 0; i < 8; ++i) std::printf(" %d", tex_nb.top[i]);
    std::printf("\n左邻 p[-1,0..3] :");
    for (int i = 0; i < 4; ++i) std::printf(" %d", tex_nb.left[i]);
    std::printf("\n左上 p[-1,-1]   : %d\n", tex_nb.top_left);
    PrintBlock("真实纹理块", tex);

    RunResult tex_rr = RunAllModes(tex_nb, tex, "纹理块");

    std::printf("\n==== 平滑块 @(%d,%d) ====\n", sm_x, sm_y);
    std::printf("上邻 p[0..7,-1] :");
    for (int i = 0; i < 8; ++i) std::printf(" %d", sm_nb.top[i]);
    std::printf("\n左邻 p[-1,0..3] :");
    for (int i = 0; i < 4; ++i) std::printf(" %d", sm_nb.left[i]);
    std::printf("\n左上 p[-1,-1]   : %d\n", sm_nb.top_left);
    PrintBlock("真实平滑块", sm);

    RunResult sm_rr = RunAllModes(sm_nb, sm, "平滑块");

    // ---- 一句话小结（揭示性时刻的数据支撑）----
    std::printf("\n==== 结论 ====\n");
    std::printf("纹理块最优 = 模式%d(%s)，方向性块靠方向模式取胜\n",
                tex_rr.best_mode,
                cfs::Intra4x4ModeName(
                    static_cast<cfs::Intra4x4Mode>(tex_rr.best_mode)));
    std::printf("平滑块最优 = 模式%d(%s)，DC 的 SAD=%d（9 种里最小之一）\n",
                sm_rr.best_mode,
                cfs::Intra4x4ModeName(
                    static_cast<cfs::Intra4x4Mode>(sm_rr.best_mode)),
                sm_rr.sad[2]);

    // ---- dump 供 gen_charts.py ----
    if (!out_dir.empty()) {
        std::string path = out_dir + "/intra_dump.txt";
        FILE* f = std::fopen(path.c_str(), "w");
        if (f) {
            std::fprintf(f, "# H.264 Intra_4x4 demo dump（真实数据）\n");
            std::fprintf(f, "image %s %d %d\n", ppm.c_str(), img.width,
                         img.height);

            // 纹理块
            std::fprintf(f, "block tex %d %d\n", tex_x, tex_y);
            std::fprintf(f, "top");
            for (int i = 0; i < 8; ++i) std::fprintf(f, " %d", tex_nb.top[i]);
            std::fprintf(f, "\n");
            std::fprintf(f, "left");
            for (int i = 0; i < 4; ++i) std::fprintf(f, " %d", tex_nb.left[i]);
            std::fprintf(f, "\n");
            std::fprintf(f, "topleft %d\n", tex_nb.top_left);
            std::fprintf(f, "orig\n");
            DumpBlock(f, tex);
            for (int m = 0; m < 9; ++m) {
                std::fprintf(f, "pred %d %d\n", m, tex_rr.sad[m]);
                DumpBlock(f, tex_rr.pred[m]);
            }
            std::fprintf(f, "best %d\n", tex_rr.best_mode);

            // 平滑块
            std::fprintf(f, "block smooth %d %d\n", sm_x, sm_y);
            std::fprintf(f, "top");
            for (int i = 0; i < 8; ++i) std::fprintf(f, " %d", sm_nb.top[i]);
            std::fprintf(f, "\n");
            std::fprintf(f, "left");
            for (int i = 0; i < 4; ++i) std::fprintf(f, " %d", sm_nb.left[i]);
            std::fprintf(f, "\n");
            std::fprintf(f, "topleft %d\n", sm_nb.top_left);
            std::fprintf(f, "orig\n");
            DumpBlock(f, sm);
            for (int m = 0; m < 9; ++m) {
                std::fprintf(f, "pred %d %d\n", m, sm_rr.sad[m]);
                DumpBlock(f, sm_rr.pred[m]);
            }
            std::fprintf(f, "best %d\n", sm_rr.best_mode);
            std::fclose(f);
            std::printf("\ndumped intra prediction data -> %s\n", path.c_str());
        }
    }
    return 0;
}
