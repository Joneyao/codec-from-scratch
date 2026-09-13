// deblock_demo.cpp — 用真实重建帧演示去块滤波如何"区分假边界与真边界"。
//
// 做四件事，全部基于确定的真实数据、不手编"想要的结果"：
//   1) 读一张真实测试图当"重建帧"的底子，然后人为制造块效应：把每个 16 像素
//      的块整体加/减一个小台阶(±3~6)，模拟"每块各自量化后 DC 略有偏差"造成的
//      块边界不连续——这正是低码率下最典型的块效应成因。
//   2) 沿一条竖直块边界取一排真实像素 (p3..p0|q0..q3)，跑弱滤波(bS<4)，
//      展示滤波前有台阶、滤波后被磨平，并打印每个像素的前后值。
//   3) 关键对比：同样的阈值下，喂一条"真实图像边缘"(跨界像素差很大)，
//      展示 filterSamplesFlag=0，滤波器主动放手——保边。
//   4) 扫描 QP=20..40，展示 α/β 阈值随 QP 增大而增大(滤得越积极)，以及
//      同一排像素在不同 bS 下的滤波强度差异。
//
// 用法：./deblock_demo ../../samples/camera_photo.ppm [chart_data]
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "deblock_filter.h"
#include "image_io.h"

namespace {

inline int Luma(int r, int g, int b) {
    return (77 * r + 150 * g + 29 * b + 128) >> 8;
}

// 从一排整数样本(长度>=8，中心在 idx=edge)取出跨界 8 像素 p3..p0|q0..q3。
cfs::EdgeSamples TakeEdge(const std::vector<int>& row, int edge) {
    cfs::EdgeSamples s;
    for (int i = 0; i < 4; ++i) {
        s.p[i] = row[edge - 1 - i];  // p0=edge-1, p1=edge-2, ...
        s.q[i] = row[edge + i];      // q0=edge,   q1=edge+1, ...
    }
    return s;
}

void PrintEdge(const char* tag, const cfs::EdgeSamples& s) {
    std::printf("%s  p3..p0 | q0..q3 = %3d %3d %3d %3d | %3d %3d %3d %3d\n", tag,
                s.p[3], s.p[2], s.p[1], s.p[0], s.q[0], s.q[1], s.q[2], s.q[3]);
}

void DumpEdge(FILE* f, const char* name, const cfs::EdgeSamples& s) {
    std::fprintf(f, "%s %d %d %d %d %d %d %d %d\n", name, s.p[3], s.p[2],
                 s.p[1], s.p[0], s.q[0], s.q[1], s.q[2], s.q[3]);
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

    // ---- 取一整行真实亮度像素当"重建帧"的一行 ----
    const int py = 96;  // 选一行纹理较平缓处，块效应看得清
    std::vector<int> orig_row(img.width);
    for (int x = 0; x < img.width; ++x)
        orig_row[x] = Luma(img.r(x, py), img.g(x, py), img.b(x, py));

    // 人为制造块效应：每 16 像素一个块，给整块加一个小的 DC 台阶。
    // 台阶模式 {+4,-3,+5,-4,...} 模拟各块量化后 DC 偏差不同。
    const int kBlk = 16;
    std::vector<int> recon_row = orig_row;
    const int steps[] = {4, -3, 5, -4, 3, -5, 4, -3};
    for (int x = 0; x < img.width; ++x) {
        int blk = x / kBlk;
        int st = steps[blk % 8];
        int v = recon_row[x] + st;
        recon_row[x] = v < 0 ? 0 : (v > 255 ? 255 : v);
    }

    // ---- 演示 1：块效应假边界被弱滤波磨平 ----
    // 取第 6 个块边界(x=96)：左块加了 steps[5]=-5，右块加 steps[6]=+4，
    // 边界处凭空多出 ~9 的台阶，这是纯块效应，应被滤掉。
    const int edge1 = 6 * kBlk;  // x=96
    cfs::EdgeSamples e1 = TakeEdge(recon_row, edge1);
    cfs::EdgeSamples e1_orig = TakeEdge(orig_row, edge1);  // 无块效应的真值

    // bS=2：两侧有非零残差（P 帧常见），QP=32。
    const int qp = 32;
    const int bS_block = 2;
    cfs::EdgeSamples e1_filt = e1;
    bool did1 = cfs::DeblockEdge(e1_filt, bS_block, qp, /*chroma=*/false);
    const int alpha = cfs::AlphaFromIndexA(qp);
    const int beta = cfs::BetaFromIndexB(qp);

    std::printf("\n==== 演示 1：块效应假边界被磨平（QP=%d, bS=%d）====\n", qp,
                bS_block);
    std::printf("α=%d β=%d（QP=%d 查 Table 8-14）\n", alpha, beta, qp);
    PrintEdge("无块效应真值 ", e1_orig);
    PrintEdge("带块效应输入 ", e1);
    PrintEdge("去块滤波之后 ", e1_filt);
    std::printf("filterSamplesFlag=%d（1=判定为块效应，执行滤波）\n", did1 ? 1 : 0);
    std::printf("边界台阶 |p0-q0|：滤波前 %d → 滤波后 %d\n",
                std::abs(e1.p[0] - e1.q[0]), std::abs(e1_filt.p[0] - e1_filt.q[0]));

    // ---- 演示 2：真实图像边缘被保留（保边）----
    // 构造一条"真边缘"：左侧一片暗(约 40)、右侧一片亮(约 200)，
    // 跨界差 ~160 远大于 α。这是真实物体轮廓，不该被抹平。
    cfs::EdgeSamples e2;
    e2.p = {40, 41, 39, 42};   // p3..p0：一片暗
    e2.q = {200, 201, 199, 202};  // q0..q3：一片亮
    cfs::EdgeSamples e2_filt = e2;
    bool did2 = cfs::DeblockEdge(e2_filt, bS_block, qp, /*chroma=*/false);
    std::printf("\n==== 演示 2：真实图像边缘被保留（同样 QP/bS）====\n");
    PrintEdge("真边缘输入 ", e2);
    PrintEdge("滤波之后   ", e2_filt);
    std::printf("filterSamplesFlag=%d（0=判定为真边缘，放手不滤）\n",
                did2 ? 1 : 0);
    std::printf("|p0-q0|=%d ≥ α=%d，超出阈值 → 不滤，边缘保住\n",
                std::abs(e2.p[0] - e2.q[0]), alpha);

    // ---- 演示 3：α/β 阈值随 QP 变化 ----
    std::printf("\n==== 演示 3：α/β 随 QP 增大（Table 8-14）====\n");
    std::printf("  QP :  20  24  28  32  36  40\n");
    std::printf("  α  : ");
    const int qps[] = {20, 24, 28, 32, 36, 40};
    for (int q : qps) std::printf("%4d", cfs::AlphaFromIndexA(q));
    std::printf("\n  β  : ");
    for (int q : qps) std::printf("%4d", cfs::BetaFromIndexB(q));
    std::printf("\n  QP 越大(压得越狠、块效应越重) → α/β 越大 → 滤得越积极\n");

    // ---- 演示 4：同一排像素、不同 bS 的滤波强度 ----
    // 用演示 1 的块效应输入，分别按 bS=1/2/3/4 滤，看改动幅度。
    std::printf("\n==== 演示 4：不同 bS 的滤波强度（QP=%d，同一输入）====\n", qp);
    PrintEdge("输入        ", e1);
    int bs_p1[5] = {0}, bs_q1[5] = {0};
    for (int bs = 1; bs <= 4; ++bs) {
        cfs::EdgeSamples e = e1;
        cfs::DeblockEdge(e, bs, qp, /*chroma=*/false);
        std::printf("bS=%d 之后    p1=%3d p0=%3d | q0=%3d q1=%3d\n", bs, e.p[1],
                    e.p[0], e.q[0], e.q[1]);
        bs_p1[bs] = e.p[0];
        bs_q1[bs] = e.q[0];
    }
    std::printf("bS 越大 → 允许改动越大 → 磨得越狠（bS=4 强滤波可动到 p2/q2）\n");

    // ---- 演示 5：Bs 决策的几种典型情形 ----
    std::printf("\n==== 演示 5：边界强度 bS 决策（spec 8.7.2.1）====\n");
    struct Case {
        const char* name;
        cfs::BsInputs in;
    };
    Case cases[] = {
        {"帧内块的宏块边界", {true, true, false, false, false, 0, 0}},
        {"帧内块的内部边界", {false, true, false, false, false, 0, 0}},
        {"有非零残差的边界", {false, false, false, true, true, 0, 0}},
        {"参考帧不同      ", {false, false, false, false, false, 1, 0}},
        {"MV 差≥1整像素   ", {false, false, false, false, false, 0, 4}},
        {"全同、静止      ", {false, false, false, false, false, 0, 0}},
    };
    for (const auto& c : cases)
        std::printf("  %s → bS=%d\n", c.name, cfs::DeriveBs(c.in));

    // ---- dump 供 gen_charts.py ----
    if (!out_dir.empty()) {
        std::string path = out_dir + "/deblock_dump.txt";
        FILE* f = std::fopen(path.c_str(), "w");
        if (f) {
            std::fprintf(f, "# H.264 deblocking filter demo dump（真实数据）\n");
            std::fprintf(f, "image %s %d %d\n", ppm.c_str(), img.width,
                         img.height);
            std::fprintf(f, "qp %d alpha %d beta %d bs %d\n", qp, alpha, beta,
                         bS_block);
            // 演示 1：块效应边界前后。
            DumpEdge(f, "orig", e1_orig);
            DumpEdge(f, "recon", e1);
            DumpEdge(f, "filt", e1_filt);
            std::fprintf(f, "flag1 %d\n", did1 ? 1 : 0);
            // 演示 2：真边缘。
            DumpEdge(f, "edge_in", e2);
            DumpEdge(f, "edge_out", e2_filt);
            std::fprintf(f, "flag2 %d\n", did2 ? 1 : 0);
            // 演示 3：α/β 曲线（QP 0..51）。
            std::fprintf(f, "alpha_curve");
            for (int q = 0; q <= 51; ++q)
                std::fprintf(f, " %d", cfs::AlphaFromIndexA(q));
            std::fprintf(f, "\n");
            std::fprintf(f, "beta_curve");
            for (int q = 0; q <= 51; ++q)
                std::fprintf(f, " %d", cfs::BetaFromIndexB(q));
            std::fprintf(f, "\n");
            // 演示 4：不同 bS 的 p0/q0。
            for (int bs = 1; bs <= 4; ++bs)
                std::fprintf(f, "bs%d p0 %d q0 %d\n", bs, bs_p1[bs], bs_q1[bs]);
            // 一整段块效应行(用于画整行台阶)。
            std::fprintf(f, "recon_row");
            for (int x = 80; x < 176; ++x) std::fprintf(f, " %d", recon_row[x]);
            std::fprintf(f, "\n");
            std::fprintf(f, "orig_row");
            for (int x = 80; x < 176; ++x) std::fprintf(f, " %d", orig_row[x]);
            std::fprintf(f, "\n");
            std::fclose(f);
            std::printf("\ndumped deblock data -> %s\n", path.c_str());
        }
    }
    return 0;
}
