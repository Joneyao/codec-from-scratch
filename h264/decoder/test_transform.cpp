// test_transform.cpp — 校验 H.264 4x4 整数反变换与反量化（spec 8.5.8）。
//
// 全部用可手算核对或结构性质校验，不依赖外部文件：
//   1) 反变换确定性：同样输入永远同样输出（整数运算，无浮点误差）。
//   2) DC-only 输入：只有直流系数时，反变换出一个常量块。
//   3) 全零输入：反变换出全零残差。
//   4) 反量化 QP+6 翻倍：qP 每加 6，反量化值恰好翻倍。
//   5) LevelScale 位置分类与 normAdjust 取值正确（照 spec 8-252/8-253）。
//   6) 反量化 (0,0) 位置随 QP 的具体数值（可手算核对）。
#include <array>
#include <cstdio>

#include "transform_quant.h"

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

using cfs::Coeff4x4;

Coeff4x4 Zero() { return Coeff4x4{}; }

// 测试 1：反变换确定性——跑 3 次同一输入，结果逐元素相同。
void TestDeterminism() {
    Coeff4x4 d{};
    d[0][0] = 320;
    d[0][1] = -64;
    d[1][0] = 48;
    d[2][2] = -16;
    d[3][3] = 8;
    Coeff4x4 r1 = cfs::InverseTransform4x4(d);
    Coeff4x4 r2 = cfs::InverseTransform4x4(d);
    Coeff4x4 r3 = cfs::InverseTransform4x4(d);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            CHECK(r1[i][j] == r2[i][j]);
            CHECK(r2[i][j] == r3[i][j]);
        }
}

// 测试 2：DC-only 反变换出常量块。
// 只有 d[0][0]=D 时，蝶形里所有 e1/e2/e3、g1/g2/g3 都为 0，
// 每个 h_ij = D，故 r_ij = (D + 32) >> 6，全块相同。
void TestDcOnly() {
    Coeff4x4 d = Zero();
    d[0][0] = 256;  // (256 + 32) >> 6 = 288 >> 6 = 4
    Coeff4x4 r = cfs::InverseTransform4x4(d);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) CHECK(r[i][j] == 4);

    d[0][0] = 512;  // (512 + 32) >> 6 = 544 >> 6 = 8
    Coeff4x4 r2 = cfs::InverseTransform4x4(d);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) CHECK(r2[i][j] == 8);
}

// 测试 3：全零输入 -> 全零残差。
void TestAllZero() {
    Coeff4x4 r = cfs::InverseTransform4x4(Zero());
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) CHECK(r[i][j] == 0);

    // 反量化全零也全零，且组合路径也全零。
    Coeff4x4 rr = cfs::ReconstructResidual4x4(Zero(), 28);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) CHECK(rr[i][j] == 0);
}

// 测试 4：反量化 QP+6 翻倍规律。
// 式 8-265 里 << (qP/6)，m=qP%6 不变时 qP 加 6 只让左移多一位 => 值翻倍。
// 取 qP=6 与 qP=12（m 都为 0），以及 qP=10 与 qP=16（m 都为 4）。
void TestQpDoubling() {
    Coeff4x4 c = Zero();
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) c[i][j] = 3;  // 任意非零常量

    Coeff4x4 d6 = cfs::DequantResidual4x4(c, 6);
    Coeff4x4 d12 = cfs::DequantResidual4x4(c, 12);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) CHECK(d12[i][j] == 2 * d6[i][j]);

    Coeff4x4 d10 = cfs::DequantResidual4x4(c, 10);
    Coeff4x4 d16 = cfs::DequantResidual4x4(c, 16);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) CHECK(d16[i][j] == 2 * d10[i][j]);

    // 隔 12（两个 +6）应是 4 倍。
    Coeff4x4 d18 = cfs::DequantResidual4x4(c, 18);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) CHECK(d18[i][j] == 4 * d6[i][j]);
}

// 测试 5：LevelScale 位置分类 + normAdjust 取值（spec 8-252/8-253）。
void TestLevelScale() {
    // m=0 那行 v = {10,16,13}。
    CHECK(cfs::LevelScale(0, 0, 0) == 10);  // 类 0
    CHECK(cfs::LevelScale(0, 2, 2) == 10);  // 类 0
    CHECK(cfs::LevelScale(0, 1, 1) == 16);  // 类 1
    CHECK(cfs::LevelScale(0, 3, 3) == 16);  // 类 1
    CHECK(cfs::LevelScale(0, 0, 1) == 13);  // 类 2
    CHECK(cfs::LevelScale(0, 1, 0) == 13);  // 类 2

    // m=5 那行 v = {18,29,23}。
    CHECK(cfs::LevelScale(5, 0, 0) == 18);
    CHECK(cfs::LevelScale(5, 1, 1) == 29);
    CHECK(cfs::LevelScale(5, 2, 1) == 23);
}

// 测试 6：反量化 (0,0) 具体数值可手算。
// c00=5，qP=6 -> m=0, LevelScale=10, shift=1 -> (5*10)<<1 = 100。
// qP=0 -> (5*10)<<0 = 50。qP=12 -> (5*10)<<2 = 200。
void TestDequantExact() {
    Coeff4x4 c = Zero();
    c[0][0] = 5;
    CHECK(cfs::DequantResidual4x4(c, 0)[0][0] == 50);
    CHECK(cfs::DequantResidual4x4(c, 6)[0][0] == 100);
    CHECK(cfs::DequantResidual4x4(c, 12)[0][0] == 200);
}

}  // namespace

int main() {
    TestDeterminism();
    TestDcOnly();
    TestAllZero();
    TestQpDoubling();
    TestLevelScale();
    TestDequantExact();

    if (g_failures != 0) {
        std::fprintf(stderr, "test_transform: %d FAILURE(s)\n", g_failures);
        return 1;
    }
    std::printf("test_transform: 全部通过\n");
    return 0;
}
