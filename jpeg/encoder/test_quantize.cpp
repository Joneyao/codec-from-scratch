// test_quantize.cpp — 量化 / 反量化最小单元测试。
#include <cmath>
#include <cstdio>

#include "quantize.h"

namespace {
int g_failed = 0;
void Check(bool c, const char* m) {
    std::printf(c ? "[ok]   %s\n" : "[FAIL] %s\n", m);
    if (!c) ++g_failed;
}

int CountNonZero(const cfs::Block8d& b) {
    int n = 0;
    for (int v = 0; v < 8; ++v)
        for (int u = 0; u < 8; ++u)
            if (std::lround(b[v][u]) != 0) ++n;
    return n;
}
}  // namespace

int main() {
    // 1. Q=100：量化表几乎全 1（scale=0 -> step=(base*0+50)/100=0 -> clamp 到 1）。
    {
        cfs::QuantTable t = cfs::ScaleTable(cfs::kBaseLuma, 100);
        bool all_one = true;
        for (int v = 0; v < 8; ++v)
            for (int u = 0; u < 8; ++u)
                if (t[v][u] != 1) all_one = false;
        Check(all_one, "Q=100 luma table is all 1 (near lossless)");
    }

    // 2. Q 越小，步长越大：Q=10 的 DC 步长应远大于 Q=90。
    {
        cfs::QuantTable t90 = cfs::ScaleTable(cfs::kBaseLuma, 90);
        cfs::QuantTable t10 = cfs::ScaleTable(cfs::kBaseLuma, 10);
        Check(t10[0][0] > t90[0][0], "smaller Q -> larger DC step");
        Check(t10[7][7] >= t90[7][7], "smaller Q -> larger HF step");
    }

    // 3. Q 越小，非零系数越少（用一个含高频的合成系数块）。
    {
        cfs::Block8d c{};
        // 造一个能量分散、各频率都有值的系数块。
        for (int v = 0; v < 8; ++v)
            for (int u = 0; u < 8; ++u)
                c[v][u] = 120.0 / (1 + u + v);  // 低频大、高频小
        int nz90 = CountNonZero(cfs::Quantize(c, cfs::ScaleTable(cfs::kBaseLuma, 90)));
        int nz50 = CountNonZero(cfs::Quantize(c, cfs::ScaleTable(cfs::kBaseLuma, 50)));
        int nz10 = CountNonZero(cfs::Quantize(c, cfs::ScaleTable(cfs::kBaseLuma, 10)));
        Check(nz90 >= nz50 && nz50 >= nz10,
              "nonzero count decreases as Q decreases");
        Check(nz10 < nz90, "Q=10 keeps strictly fewer nonzeros than Q=90");
    }

    // 4. 量化->反量化误差不超过半个量化步长（四舍五入的数学保证）。
    {
        cfs::Block8d c{};
        for (int v = 0; v < 8; ++v)
            for (int u = 0; u < 8; ++u)
                c[v][u] = 37.3 * std::sin(0.7 * u) - 21.1 * std::cos(0.5 * v);
        cfs::QuantTable t = cfs::ScaleTable(cfs::kBaseLuma, 50);
        cfs::Block8d q = cfs::Quantize(c, t);
        cfs::Block8d dq = cfs::Dequantize(q, t);
        bool within_half_step = true;
        for (int v = 0; v < 8; ++v)
            for (int u = 0; u < 8; ++u) {
                double err = std::fabs(c[v][u] - dq[v][u]);
                // 允许极小的浮点余量。
                if (err > t[v][u] / 2.0 + 1e-6) within_half_step = false;
            }
        Check(within_half_step, "|coeff - dequant| <= step/2 for every position");
    }

    // 5. Quantize 输出确实是整数（round 后无小数）。
    {
        cfs::Block8d c{};
        for (int v = 0; v < 8; ++v)
            for (int u = 0; u < 8; ++u) c[v][u] = 55.5 - 3.0 * u;
        cfs::Block8d q = cfs::Quantize(c, cfs::ScaleTable(cfs::kBaseLuma, 50));
        bool integral = true;
        for (int v = 0; v < 8; ++v)
            for (int u = 0; u < 8; ++u)
                if (std::fabs(q[v][u] - std::round(q[v][u])) > 1e-9)
                    integral = false;
        Check(integral, "quantized coefficients are integers");
    }

    if (g_failed == 0) { std::printf("\nALL TESTS PASSED\n"); return 0; }
    std::printf("\n%d TEST(S) FAILED\n", g_failed);
    return 1;
}
