// transform_demo.cpp — 用一组真实的量化系数，走通 H.264 的反量化 + 4x4 整数
//                      反变换，重建出残差块；并把 H.264 整数变换和 JPEG 那种
//                      浮点 DCT 放在一起对照，展示整数变换的“逐比特确定”。
//
// 做四件事：
//   1) 取一个真实残差块（帧内预测残差的典型形状：低频有能量、高频接近 0），
//      用 H.264 的正向整数变换 + 量化，得到一组真实量化系数 c。
//      —— 这组 c 不是手编的，是把残差真的压一遍得到的。
//   2) 走解码路径：c -> 反量化(Scaling) -> 4x4 整数反变换 -> 重建残差 r。
//      打印“量化系数 -> 反量化 -> 反变换残差”这条接力。
//   3) QP 对照：同一组 c 用 QP=22/28/34 分别反量化，展示 QP 每 +6 翻倍。
//   4) 整数 vs 浮点对照：同一残差块，一边用 H.264 整数变换往返，一边用浮点
//      DCT 往返，把整数往返多跑几次证明结果完全一致（bit-exact），
//      并观察浮点路径的重建误差。
//
// 所有数字都能复现：换台机器、换个编译器，整数路径的输出一模一样。
//
// 用法：
//   ./transform_demo [chart_data]
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "transform_quant.h"

namespace {

using cfs::Coeff4x4;

// ---- H.264 正向 4x4 整数变换（spec 8.6 的编码侧对偶，这里只为造真实系数）----
// 用的是与反变换配套的核变换 Cf；这里直接写核心变换 + 量化，得到量化系数。
// 前向核变换矩阵（整数）：
//   [ 1  1  1  1 ]
//   [ 2  1 -1 -2 ]
//   [ 1 -1 -1  1 ]
//   [ 1 -2  2 -1 ]
Coeff4x4 ForwardCore4x4(const Coeff4x4& x) {
    // 行变换
    int t[4][4];
    for (int i = 0; i < 4; ++i) {
        const int a0 = x[i][0] + x[i][3];
        const int a1 = x[i][1] + x[i][2];
        const int a2 = x[i][1] - x[i][2];
        const int a3 = x[i][0] - x[i][3];
        t[i][0] = a0 + a1;
        t[i][1] = 2 * a3 + a2;
        t[i][2] = a0 - a1;
        t[i][3] = a3 - 2 * a2;
    }
    // 列变换
    Coeff4x4 W{};
    for (int j = 0; j < 4; ++j) {
        const int a0 = t[0][j] + t[3][j];
        const int a1 = t[1][j] + t[2][j];
        const int a2 = t[1][j] - t[2][j];
        const int a3 = t[0][j] - t[3][j];
        W[0][j] = a0 + a1;
        W[1][j] = 2 * a3 + a2;
        W[2][j] = a0 - a1;
        W[3][j] = a3 - 2 * a2;
    }
    return W;
}

// 一个简化的前向量化，只为得到“真实的、成形的”量化系数 c 用于演示解码路径。
// 用与 LevelScale 对应的一组前向缩放 MF（近似），保证 c 的量级像真实码流。
// 说明：解码正确性不依赖这套前向量化的精确性——它只负责生成输入。
Coeff4x4 ForwardQuant(const Coeff4x4& residual, int qP) {
    Coeff4x4 W = ForwardCore4x4(residual);
    // 前向缩放矩阵 MF（H.264 常见取值，m = qP%6），位置分三类，与 LevelScale 对偶。
    static const int MF[6][3] = {
        {13107, 5243, 8066}, {11916, 4660, 7490}, {10082, 4194, 6554},
        {9362, 3647, 5825},  {8192, 3355, 5243},  {7282, 2893, 4559},
    };
    const int m = qP % 6;
    const int qbits = 15 + qP / 6;
    const int f = (1 << qbits) / 6;  // 舍入偏移
    Coeff4x4 c{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            const bool ie = (i % 2 == 0), je = (j % 2 == 0);
            const int cls = (ie && je) ? 0 : ((!ie && !je) ? 1 : 2);
            const long long w = W[i][j];
            const long long absw = w < 0 ? -w : w;
            long long level = (absw * MF[m][cls] + f) >> qbits;
            c[i][j] = static_cast<int>(w < 0 ? -level : level);
        }
    return c;
}

// ---- 浮点 4x4 DCT / IDCT（对照用，模拟 JPEG 那种浮点变换）----
void FloatDct4x4(const double in[4][4], double out[4][4]) {
    const double pi = 3.14159265358979323846;
    for (int u = 0; u < 4; ++u)
        for (int v = 0; v < 4; ++v) {
            double s = 0.0;
            for (int x = 0; x < 4; ++x)
                for (int y = 0; y < 4; ++y)
                    s += in[x][y] * std::cos((2 * x + 1) * u * pi / 8.0) *
                         std::cos((2 * y + 1) * v * pi / 8.0);
            const double cu = (u == 0) ? 1.0 / std::sqrt(2.0) : 1.0;
            const double cv = (v == 0) ? 1.0 / std::sqrt(2.0) : 1.0;
            out[u][v] = 0.5 * cu * cv * s;
        }
}

void FloatIdct4x4(const double in[4][4], double out[4][4]) {
    const double pi = 3.14159265358979323846;
    for (int x = 0; x < 4; ++x)
        for (int y = 0; y < 4; ++y) {
            double s = 0.0;
            for (int u = 0; u < 4; ++u)
                for (int v = 0; v < 4; ++v) {
                    const double cu = (u == 0) ? 1.0 / std::sqrt(2.0) : 1.0;
                    const double cv = (v == 0) ? 1.0 / std::sqrt(2.0) : 1.0;
                    s += cu * cv * in[u][v] *
                         std::cos((2 * x + 1) * u * pi / 8.0) *
                         std::cos((2 * y + 1) * v * pi / 8.0);
                }
            out[x][y] = 0.5 * s;
        }
}

void PrintBlock(const char* title, const Coeff4x4& b) {
    std::printf("%s:\n", title);
    for (int i = 0; i < 4; ++i) {
        std::printf("  ");
        for (int j = 0; j < 4; ++j) std::printf("%6d", b[i][j]);
        std::printf("\n");
    }
}

void DumpBlock(FILE* f, const Coeff4x4& b) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j)
            std::fprintf(f, "%d%s", b[i][j], j == 3 ? "" : " ");
        std::fprintf(f, "\n");
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string out_dir = argc > 1 ? argv[1] : "chart_data";

    // ---- 1) 一个真实形状的残差块 ----
    // 帧内预测猜得不错时，残差往往是小幅、低频占主导的样子。
    // 这里用一个平滑起伏的残差（不是随机数、也不是手写系数），
    // 走一遍前向变换+量化，得到真实的量化系数。
    Coeff4x4 residual = {{
        {12, 18, 22, 20},
        {16, 10, 6, 14},
        {10, 4, -2, 8},
        {6, 0, -6, 4},
    }};

    const int qP = 16;  // 一个中低 QP：保留更多系数，接力矩阵更有看头
    Coeff4x4 W = ForwardCore4x4(residual);
    Coeff4x4 c = ForwardQuant(residual, qP);

    std::printf("==== H.264 4x4 整数变换 · 反量化 · 反变换（真实数据）====\n\n");
    PrintBlock("① 原始残差块 residual", residual);
    PrintBlock("② 正向整数变换系数 W（未量化，只含整数运算）", W);
    std::printf("   （QP=%d 量化后 -> 量化系数 c）\n\n", qP);
    PrintBlock("③ 量化系数 c（进码流的就是它）", c);

    // ---- 2) 解码路径：c -> 反量化 -> 反变换 -> 残差 ----
    Coeff4x4 d = cfs::DequantResidual4x4(c, qP);
    Coeff4x4 r = cfs::InverseTransform4x4(d);
    std::printf("\n");
    std::printf("④ 反量化后 d = Scaling(c, QP=%d):\n", qP);
    for (int i = 0; i < 4; ++i) {
        std::printf("  ");
        for (int j = 0; j < 4; ++j) std::printf("%6d", d[i][j]);
        std::printf("\n");
    }
    PrintBlock("⑤ 4x4 整数反变换后 重建残差 r", r);

    // 重建误差（量化带来的，不是变换带来的）。
    int max_err = 0;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            int e = std::abs(r[i][j] - residual[i][j]);
            if (e > max_err) max_err = e;
        }
    std::printf("   重建残差 vs 原残差 最大逐点误差 = %d（QP=%d 量化损失）\n",
                max_err, qP);

    // ---- 3) QP 对照：同一组 c，不同 QP 反量化 ----
    std::printf("\n==== QP 每 +6 反量化翻倍（同一系数 c[0][0]=%d）====\n",
                c[0][0]);
    const int qps[3] = {22, 28, 34};
    int dc_vals[3];
    for (int k = 0; k < 3; ++k) {
        Coeff4x4 dk = cfs::DequantResidual4x4(c, qps[k]);
        dc_vals[k] = dk[0][0];
        std::printf("   QP=%2d  ->  反量化 d[0][0] = %d\n", qps[k], dc_vals[k]);
    }
    std::printf("   QP 22->28 比值 = %.2f，28->34 比值 = %.2f（≈2 即翻倍）\n",
                dc_vals[0] ? static_cast<double>(dc_vals[1]) / dc_vals[0] : 0.0,
                dc_vals[1] ? static_cast<double>(dc_vals[2]) / dc_vals[1] : 0.0);

    // ---- 4) 整数 vs 浮点：确定性对照 ----
    std::printf("\n==== 整数变换 vs 浮点 DCT：确定性对照 ====\n");
    // 整数路径：反变换跑 3 次，逐元素完全一致。
    Coeff4x4 r_a = cfs::InverseTransform4x4(d);
    Coeff4x4 r_b = cfs::InverseTransform4x4(d);
    bool int_identical = true;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (r_a[i][j] != r_b[i][j]) int_identical = false;
    std::printf("   整数反变换重复运行结果逐比特一致：%s\n",
                int_identical ? "是" : "否");

    // 浮点路径：残差 -> 浮点 DCT -> 浮点 IDCT，看往返能否精确还原、误差多大。
    double fin[4][4], fcoef[4][4], frec[4][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) fin[i][j] = residual[i][j];
    FloatDct4x4(fin, fcoef);
    FloatIdct4x4(fcoef, frec);
    double max_fp_err = 0.0;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double e = std::fabs(frec[i][j] - fin[i][j]);
            if (e > max_fp_err) max_fp_err = e;
        }
    std::printf("   浮点 DCT 往返最大误差 = %.3e（非零 = 浮点舍入的痕迹）\n",
                max_fp_err);
    std::printf("   关键区别：整数变换的每个中间值都是精确整数，跨平台钉死；\n");
    std::printf("             浮点变换依赖 FPU/编译器，不同环境可能有微小漂移。\n");

    // ---- dump 供 gen_charts.py ----
    if (!out_dir.empty()) {
        std::string path = out_dir + "/transform_dump.txt";
        FILE* f = std::fopen(path.c_str(), "w");
        if (f) {
            std::fprintf(f, "# H.264 transform/quant demo dump（真实数据）\n");
            std::fprintf(f, "qP %d\n", qP);
            std::fprintf(f, "residual\n");
            DumpBlock(f, residual);
            std::fprintf(f, "forward_W\n");
            DumpBlock(f, W);
            std::fprintf(f, "quant_c\n");
            DumpBlock(f, c);
            std::fprintf(f, "dequant_d\n");
            DumpBlock(f, d);
            std::fprintf(f, "recon_r\n");
            DumpBlock(f, r);
            std::fprintf(f, "max_err %d\n", max_err);
            // QP 对照：三组 QP 的整块反量化 + DC 值
            for (int k = 0; k < 3; ++k) {
                Coeff4x4 dk = cfs::DequantResidual4x4(c, qps[k]);
                std::fprintf(f, "dequant_qp %d %d\n", qps[k], dk[0][0]);
                DumpBlock(f, dk);
            }
            std::fprintf(f, "int_identical %d\n", int_identical ? 1 : 0);
            std::fprintf(f, "fp_max_err %.6e\n", max_fp_err);
            std::fclose(f);
            std::printf("\ndumped transform data -> %s\n", path.c_str());
        }
    }
    return 0;
}
