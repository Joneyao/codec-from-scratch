// test_dct8x8.cpp — DCT/IDCT 最小单元测试
#include <cmath>
#include <cstdio>

#include "dct8x8.h"

namespace {
int g_failed = 0;
void Check(bool c, const char* m) {
    std::printf(c ? "[ok]   %s\n" : "[FAIL] %s\n", m);
    if (!c) ++g_failed;
}
}  // namespace

int main() {
    // 1. 均匀块（全 100）：只有 DC 有值，AC 全 0。
    {
        cfs::Block8u p{};
        for (auto& row : p) row.fill(100);
        auto c = cfs::ForwardDct(p);
        // DC = 8 * C(0)^2 * (100-128) ... 直接验证 AC 近似为 0
        bool ac_zero = true;
        for (int v = 0; v < 8; ++v)
            for (int u = 0; u < 8; ++u)
                if (!(u == 0 && v == 0) && std::fabs(c[v][u]) > 1e-6)
                    ac_zero = false;
        Check(ac_zero, "flat block -> only DC, AC all zero");
        // DC = 1/4 * (1/sqrt2)^2 * 8*8 * (100-128) = 0.25*0.5*64*(-28) = -224
        Check(std::fabs(c[0][0] - (-224.0)) < 1e-6, "flat block DC = -224");
    }
    // 2. DCT→IDCT 往返一致（渐变块），误差应 <=1（取整误差）。
    {
        cfs::Block8u p{};
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                p[y][x] = static_cast<uint8_t>(20 + (x * 8 + y * 4));
        auto c = cfs::ForwardDct(p);
        auto r = cfs::InverseDct(c);
        int maxerr = 0;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                maxerr = std::max(maxerr,
                                  std::abs(int(p[y][x]) - int(r[y][x])));
        Check(maxerr <= 1, "DCT->IDCT round-trip error <= 1");
    }
    // 3. 能量集中：一个平滑渐变块，左上 4x4 应集中绝大部分能量。
    {
        cfs::Block8u p{};
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                p[y][x] = static_cast<uint8_t>(80 + x * 10);  // 水平渐变
        auto c = cfs::ForwardDct(p);
        double total = 0, low = 0;
        for (int v = 0; v < 8; ++v)
            for (int u = 0; u < 8; ++u) {
                double e = c[v][u] * c[v][u];
                total += e;
                if (u < 4 && v < 4) low += e;
            }
        Check(low / total > 0.99, "smooth block: >99% energy in low 4x4");
    }

    if (g_failed == 0) { std::printf("\nALL TESTS PASSED\n"); return 0; }
    std::printf("\n%d TEST(S) FAILED\n", g_failed);
    return 1;
}
