// test_forward_transform_quant.cpp — 校验 H.264 4x4 正向整数变换 + 正向量化
// （spec 8.5，编码侧）。核心是与解码器反变换/反量化的往返自洽。
//
// 覆盖：
//   1) 往返一致性：残差 -> 正向变换+量化 -> 解码侧反量化+反变换 -> 接近原残差
//      （误差在量化步长范围内）。
//   2) DC-only 块：只有直流分量的常量残差，量化后只在 (0,0) 有非零。
//   3) QP 翻倍：QP 与 QP+6，同一残差的量化 level 大致差 2 倍。
//   4) 全零残差 -> 全零系数。
//   5) 符号保留：负残差经往返仍为负号方向。
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "forward_transform_quant.h"
// 解码侧反量化 + 反变换，用来做往返验证。
#include "../decoder/transform_quant.h"

using cfs::Coeff4x4;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "CHECK failed: %s (line %d)\n", #cond, \
                         __LINE__);                                     \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

Coeff4x4 Zero() { return Coeff4x4{}; }

int CountNonZero(const Coeff4x4& b) {
    int n = 0;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (b[i][j] != 0) ++n;
    return n;
}

// 测试 1：往返一致性。残差经 forward+quant，再走解码路径反量化+反变换，
// 重建残差应接近原残差；误差上界取该 QP 的量化步长量级。
void TestRoundTrip() {
    Coeff4x4 residual = {{
        {12, 18, 22, 20},
        {16, 10, 6, 14},
        {10, 4, -2, 8},
        {6, 0, -6, 4},
    }};

    // 低 QP 时误差很小；这里用几个 QP 都验证误差在合理范围。
    const int qps[] = {12, 18, 24, 30};
    for (int qP : qps) {
        Coeff4x4 c = cfs::ForwardTransformQuant(residual, qP, /*intra=*/true);
        Coeff4x4 r = cfs::ReconstructResidual4x4(c, qP);
        int max_err = 0;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                int e = std::abs(r[i][j] - residual[i][j]);
                if (e > max_err) max_err = e;
            }
        // 量化步长随 QP 增大，误差上界宽松取 2^(qP/6 + 1)（含反变换归一化余量）。
        int bound = (1 << (qP / 6)) * 2 + 4;
        CHECK(max_err <= bound);
    }
}

// 测试 2：DC-only 残差（整块常量）——正向变换只在 (0,0) 有能量，量化后
// 非零系数只出现在 DC 位置。
void TestDcOnly() {
    Coeff4x4 residual{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) residual[i][j] = 20;  // 常量块

    Coeff4x4 W = cfs::ForwardTransform4x4(residual);
    // 常量块正向变换：只有 W[0][0] 非零，其余全 0。
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (i == 0 && j == 0)
                CHECK(W[i][j] != 0);
            else
                CHECK(W[i][j] == 0);

    Coeff4x4 c = cfs::Quantize4x4(W, 20, /*intra=*/true);
    CHECK(c[0][0] != 0);
    CHECK(CountNonZero(c) == 1);
}

// 测试 3：QP 翻倍。QP 每加 6，qbits 多 1 位，量化步长翻倍，量化 level 大致减半。
// 反过来看：同一残差在 QP 与 QP+6 下，DC level 之比大致为 2:1。
void TestQpDoubling() {
    Coeff4x4 residual = {{
        {40, 38, 30, 28},
        {36, 32, 26, 22},
        {30, 26, 20, 16},
        {24, 20, 14, 10},
    }};

    // 用 (0,0) DC 系数看翻倍关系（DC 能量最大，量化 level 也最大，比值最稳）。
    Coeff4x4 cA = cfs::ForwardTransformQuant(residual, 18, true);
    Coeff4x4 cB = cfs::ForwardTransformQuant(residual, 24, true);  // +6
    CHECK(cA[0][0] > 0);
    CHECK(cB[0][0] > 0);
    // cA/cB 约等于 2（允许量化取整带来的偏差，放宽到 [1.6, 2.5]）。
    double ratio = static_cast<double>(cA[0][0]) / cB[0][0];
    CHECK(ratio >= 1.6 && ratio <= 2.5);

    // 非零系数总数：QP 越大，被压成 0 的越多，非零数不增。
    CHECK(CountNonZero(cB) <= CountNonZero(cA));
}

// 测试 4：全零残差 -> 全零系数。
void TestAllZero() {
    Coeff4x4 c = cfs::ForwardTransformQuant(Zero(), 28, true);
    CHECK(CountNonZero(c) == 0);
    Coeff4x4 W = cfs::ForwardTransform4x4(Zero());
    CHECK(CountNonZero(W) == 0);
}

// 测试 5：符号保留。整体为负的残差，DC 变换系数与量化 level 应为负。
void TestSignPreserved() {
    Coeff4x4 residual{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) residual[i][j] = -15;

    Coeff4x4 W = cfs::ForwardTransform4x4(residual);
    CHECK(W[0][0] < 0);
    Coeff4x4 c = cfs::Quantize4x4(W, 22, true);
    CHECK(c[0][0] < 0);
}

}  // namespace

int main() {
    TestRoundTrip();
    TestDcOnly();
    TestQpDoubling();
    TestAllZero();
    TestSignPreserved();

    if (g_failures != 0) {
        std::fprintf(stderr, "test_forward_transform_quant: %d FAILURE(s)\n",
                     g_failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
