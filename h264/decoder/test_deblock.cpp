// test_deblock.cpp — 校验去块滤波的核心逻辑，全部可手算核对：
//   1) bS 推导：帧内宏块边界=4、帧内内部边界=3、有残差=2、参考/MV=1、全同=0。
//   2) α/β 查表：QP<16 时为 0（不滤区），QP 越大 α/β 越大。
//   3) filterSamplesFlag：真边缘(差大)不滤，块效应(差小)才滤。
//   4) 保边：跨界差远大于 α 时不改任何像素。
//   5) 平滑：小台阶(块效应)被弱滤波磨小。
//   6) 强滤波(bS=4)：满足强条件时改到 p2/q2；常量输入还原常量。
//   7) tC0 查表抽样与 Clip3 边界。
#include <cstdio>
#include <cstdlib>

#include "deblock_filter.h"

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

using cfs::BsInputs;
using cfs::EdgeSamples;

// 测试 1：bS 推导。
void TestBs() {
    // 帧内块 + 宏块边界 → 4。
    CHECK(cfs::DeriveBs({true, true, false, false, false, 0, 0}) == 4);
    CHECK(cfs::DeriveBs({true, false, true, false, false, 0, 0}) == 4);
    // 帧内块但非宏块边界 → 3。
    CHECK(cfs::DeriveBs({false, true, false, false, false, 0, 0}) == 3);
    // 非帧内、有非零残差 → 2。
    CHECK(cfs::DeriveBs({false, false, false, true, false, 0, 0}) == 2);
    CHECK(cfs::DeriveBs({false, false, false, false, true, 0, 0}) == 2);
    // 无残差、参考帧不同 → 1。
    CHECK(cfs::DeriveBs({false, false, false, false, false, 1, 0}) == 1);
    // 无残差、MV 差 >=4（1/4 像素单位）→ 1。
    CHECK(cfs::DeriveBs({false, false, false, false, false, 0, 4}) == 1);
    // MV 差 3 (<4) 且其余都无 → 0。
    CHECK(cfs::DeriveBs({false, false, false, false, false, 0, 3}) == 0);
    // 全同、静止 → 0。
    CHECK(cfs::DeriveBs({false, false, false, false, false, 0, 0}) == 0);
}

// 测试 2：α/β 查表。
void TestThresholds() {
    // QP<16：α=β=0（不滤区，spec Table 8-14 前 16 项为 0）。
    for (int q = 0; q < 16; ++q) {
        CHECK(cfs::AlphaFromIndexA(q) == 0);
        CHECK(cfs::BetaFromIndexB(q) == 0);
    }
    // 抽样几个已知值（对照 spec Table 8-14）。
    CHECK(cfs::AlphaFromIndexA(16) == 4);
    CHECK(cfs::BetaFromIndexB(16) == 2);
    CHECK(cfs::AlphaFromIndexA(28) == 20);
    CHECK(cfs::BetaFromIndexB(28) == 7);
    CHECK(cfs::AlphaFromIndexA(51) == 255);
    CHECK(cfs::BetaFromIndexB(51) == 18);
    // 单调性：QP 越大 α/β 不减。
    for (int q = 1; q <= 51; ++q) {
        CHECK(cfs::AlphaFromIndexA(q) >= cfs::AlphaFromIndexA(q - 1));
        CHECK(cfs::BetaFromIndexB(q) >= cfs::BetaFromIndexB(q - 1));
    }
    // 越界 clamp。
    CHECK(cfs::AlphaFromIndexA(100) == 255);
    CHECK(cfs::AlphaFromIndexA(-5) == 0);
}

// 测试 3：filterSamplesFlag。
void TestFilterFlag() {
    int a = cfs::AlphaFromIndexA(32);  // =32
    int b = cfs::BetaFromIndexB(32);   // =9
    // 小台阶（块效应）：跨界差 8 < α，两侧内部差 1 < β → 该滤。
    EdgeSamples s1;
    s1.p = {100, 100, 100, 100};
    s1.q = {108, 108, 108, 108};
    CHECK(cfs::FilterSamplesFlag(s1, 2, a, b) == true);
    // 真边缘：跨界差 160 >= α → 不滤。
    EdgeSamples s2;
    s2.p = {40, 40, 40, 40};
    s2.q = {200, 200, 200, 200};
    CHECK(cfs::FilterSamplesFlag(s2, 2, a, b) == false);
    // bS=0：永远不滤。
    CHECK(cfs::FilterSamplesFlag(s1, 0, a, b) == false);
}

// 测试 4：保边——真边缘不被改动。
void TestEdgePreserve() {
    EdgeSamples s;
    s.p = {40, 41, 39, 42};      // p3..p0 一片暗
    s.q = {200, 201, 199, 202};  // q0..q3 一片亮
    EdgeSamples before = s;
    bool did = cfs::DeblockEdge(s, /*bS=*/2, /*qp=*/32, /*chroma=*/false);
    CHECK(did == false);  // filterSamplesFlag=0
    // 所有像素原封不动。
    for (int i = 0; i < 4; ++i) {
        CHECK(s.p[i] == before.p[i]);
        CHECK(s.q[i] == before.q[i]);
    }
}

// 测试 5：平滑——小台阶被磨小。
void TestSmooth() {
    EdgeSamples s;
    // 块效应：左块 100，右块 112，边界处 12 的台阶。
    s.p = {100, 100, 100, 100};
    s.q = {112, 112, 112, 112};
    int step_before = std::abs(s.p[0] - s.q[0]);  // 12
    bool did = cfs::DeblockEdge(s, /*bS=*/3, /*qp=*/32, /*chroma=*/false);
    CHECK(did == true);
    int step_after = std::abs(s.p[0] - s.q[0]);
    // 滤波后边界台阶应变小（被磨平）。
    CHECK(step_after < step_before);
    // p0 应向 q0 方向靠拢（增大），q0 应向 p0 方向靠拢（减小）。
    CHECK(s.p[0] > 100);
    CHECK(s.q[0] < 112);
}

// 测试 6：强滤波 bS=4。
void TestStrong() {
    // 常量输入：全 100 应还原 100（各强滤波公式对常量都是恒等）。
    EdgeSamples c;
    c.p = {100, 100, 100, 100};
    c.q = {100, 100, 100, 100};
    cfs::DeblockEdge(c, /*bS=*/4, /*qp=*/32, /*chroma=*/false);
    for (int i = 0; i < 3; ++i) {
        CHECK(c.p[i] == 100);
        CHECK(c.q[i] == 100);
    }
    // 小台阶 + 满足强条件：强滤波可动到 p2/q2（弱滤波不会）。
    EdgeSamples s;
    s.p = {100, 101, 102, 103};  // p3..p0
    s.q = {105, 106, 107, 108};  // q0..q3
    EdgeSamples before = s;
    bool did = cfs::DeblockEdge(s, /*bS=*/4, /*qp=*/40, /*chroma=*/false);
    CHECK(did == true);
    // p2/q2 在强滤波下允许被改（对照弱滤波恒保持不变）。
    bool p2_or_q2_changed = (s.p[2] != before.p[2]) || (s.q[2] != before.q[2]);
    CHECK(p2_or_q2_changed);
}

// 测试 7：tC0 查表与 Clip3。
void TestTc0AndClip() {
    // 对照 spec Table 8-15 抽样。
    CHECK(cfs::Tc0FromTable(1, 23) == 1);
    CHECK(cfs::Tc0FromTable(2, 51) == 17);
    CHECK(cfs::Tc0FromTable(3, 51) == 25);
    CHECK(cfs::Tc0FromTable(1, 0) == 0);
    // bS=4 或越界 bS 返回 0（不走此表）。
    CHECK(cfs::Tc0FromTable(4, 40) == 0);
    CHECK(cfs::Tc0FromTable(0, 40) == 0);
    // Clip3。
    CHECK(cfs::Clip3(0, 51, 60) == 51);
    CHECK(cfs::Clip3(0, 51, -3) == 0);
    CHECK(cfs::Clip3(0, 51, 25) == 25);
}

// 测试 8：色度只滤 p0/q0，不动 p1/q1/p2/q2。
void TestChroma() {
    EdgeSamples s;
    s.p = {100, 100, 100, 106};  // p3..p0
    s.q = {112, 118, 118, 118};  // q0..q3
    EdgeSamples before = s;
    bool did = cfs::DeblockEdge(s, /*bS=*/2, /*qp=*/32, /*chroma=*/true);
    CHECK(did == true);
    // 色度：p1/q1 必须保持不变。
    CHECK(s.p[1] == before.p[1]);
    CHECK(s.q[1] == before.q[1]);
    CHECK(s.p[2] == before.p[2]);
    CHECK(s.q[2] == before.q[2]);
}

}  // namespace

int main() {
    TestBs();
    TestThresholds();
    TestFilterFlag();
    TestEdgePreserve();
    TestSmooth();
    TestStrong();
    TestTc0AndClip();
    TestChroma();

    if (g_failures != 0) {
        std::fprintf(stderr, "test_deblock: %d FAILURE(s)\n", g_failures);
        return 1;
    }
    std::printf("test_deblock: 全部通过\n");
    return 0;
}
