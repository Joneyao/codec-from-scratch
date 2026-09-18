// test_intra_mode_decision.cpp — 帧内模式决策的最小校验。
//
// 全部用「能手算」的构造输入，逐条核对：
//   1) SAD：完全命中为 0；单点差累计正确。
//   2) SATD：残差只有一个非零像素 k 时，4x4 Hadamard 把它摊到全部 16 个系数
//      （每个幅度 = k），Σ|coeff| = 16k，本模块归一化后 = 8k。可手算。
//   3) 4x4 模式决策：真实块 = 左邻横向复制（横条纹）时，Horizontal 模式能
//      零残差命中，ChooseIntra4x4Mode 必须选它，且代价 = 0。
//   4) 4x4 模式决策：真实块 = 上邻纵向复制（竖条纹）时，选 Vertical，代价 0。
//   5) 平手取小模式号：邻居全常量时 V/H/DC 预测块完全相同，应选模式 0(Vertical)。
//   6) 16x16 模式决策：块 = 上邻纵向复制时选 Vertical，代价 0；SAD/SATD 一致。
//   7) SATD 与 SAD 选出同一模式（零残差块两把尺子都给 0）。
// Release 构建默认带 -DNDEBUG 会把 assert 整个抹掉，导致断言失效、且断言里
// 用到的变量变成「未使用」触发告警。这里强制启用 assert：测试本就该跑断言。
#undef NDEBUG

#include <array>
#include <cassert>
#include <cstdio>

#include "intra_mode_decision.h"
#include "intra_predict.h"

using cfs::Block16x16;
using cfs::Block4x4;
using cfs::CostMetric;
using cfs::Intra16x16Mode;
using cfs::Neighbors16x16;
using cfs::Neighbors4x4;

namespace {

Neighbors4x4 MakeNb4(std::array<int, 8> top, std::array<int, 4> left, int tl) {
    Neighbors4x4 nb;
    nb.top = top;
    nb.left = left;
    nb.top_left = tl;
    return nb;
}

// 测试 1：SAD。
void TestSad() {
    Block4x4 a{}, b{};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            a[y][x] = 128;
            b[y][x] = 128;
        }
    assert(cfs::Sad4x4Cost(a, b) == 0);  // 完全命中
    b[0][0] = 138;                       // 差 10
    b[2][3] = 123;                       // 差 5
    assert(cfs::Sad4x4Cost(a, b) == 15);

    // 16x16 SAD：全 100 vs 全 100 = 0；改一个像素差 7 -> 7。
    Block16x16 c{}, d{};
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            c[y][x] = 100;
            d[y][x] = 100;
        }
    assert(cfs::Sad16x16Cost(c, d) == 0);
    d[5][9] = 107;
    assert(cfs::Sad16x16Cost(c, d) == 7);
    std::printf("  [1] SAD 命中/单点差 ... ok\n");
}

// 测试 2：SATD 单点残差 = k -> 归一化后 8k（k=10 -> 80）。
void TestSatdSinglePoint() {
    Block4x4 orig{}, pred{};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            orig[y][x] = 100;
            pred[y][x] = 100;
        }
    assert(cfs::Satd4x4Cost(orig, pred) == 0);  // 零残差 SATD=0

    orig[1][2] = 110;  // 单点残差 k=10
    // Hadamard 把单点摊到 16 个系数，各 |系数|=10，和=160，(160+1)>>1 = 80。
    assert(cfs::Satd4x4Cost(orig, pred) == 80);
    std::printf("  [2] SATD 单点残差 = 8k ... ok\n");
}

// 测试 3：横条纹块 -> Horizontal(模式1) 零残差命中。
void TestChoose4x4Horizontal() {
    // 左邻每行一个值，块内每行复制该值（横条纹）。
    Neighbors4x4 nb = MakeNb4({50, 50, 50, 50, 50, 50, 50, 50},
                              {30, 70, 110, 150}, 40);
    Block4x4 orig{};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) orig[y][x] = nb.left[y];

    int sad = -1;
    int mode = cfs::ChooseIntra4x4Mode(nb, orig, CostMetric::kSad, &sad);
    assert(mode == 1);  // kHorizontal
    assert(sad == 0);   // 完美命中
    std::printf("  [3] 横条纹 -> Horizontal，SAD=0 ... ok\n");
}

// 测试 4：竖条纹块 -> Vertical(模式0) 零残差命中。
void TestChoose4x4Vertical() {
    Neighbors4x4 nb = MakeNb4({30, 70, 110, 150, 150, 150, 150, 150},
                              {90, 90, 90, 90}, 40);
    Block4x4 orig{};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) orig[y][x] = nb.top[x];

    int sad = -1;
    int mode = cfs::ChooseIntra4x4Mode(nb, orig, CostMetric::kSad, &sad);
    assert(mode == 0);  // kVertical
    assert(sad == 0);
    std::printf("  [4] 竖条纹 -> Vertical，SAD=0 ... ok\n");
}

// 测试 5：邻居全常量 -> V/H/DC 预测块全相同，平手取最小模式号 0。
void TestTieBreak() {
    Neighbors4x4 nb = MakeNb4({80, 80, 80, 80, 80, 80, 80, 80},
                              {80, 80, 80, 80}, 80);
    Block4x4 orig{};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) orig[y][x] = 80;  // 与所有平滑预测一致

    int cost = -1;
    int mode = cfs::ChooseIntra4x4Mode(nb, orig, CostMetric::kSad, &cost);
    assert(mode == 0);   // 平手取 Vertical
    assert(cost == 0);
    std::printf("  [5] 全常量平手 -> 取最小模式号 0 ... ok\n");
}

// 测试 6：16x16 竖条纹 -> Vertical，SAD/SATD 都为 0。
void TestChoose16x16() {
    Neighbors16x16 nb;
    for (int i = 0; i < 16; ++i) {
        nb.top[i] = 40 + 5 * i;  // 上邻纵向纹理
        nb.left[i] = 128;
    }
    nb.top_left = 0;
    Block16x16 orig{};
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) orig[y][x] = nb.top[x];  // 竖条纹

    int sad = -1;
    Intra16x16Mode m1 =
        cfs::ChooseIntra16x16Mode(nb, orig, CostMetric::kSad, &sad);
    assert(m1 == Intra16x16Mode::kVertical);
    assert(sad == 0);

    int satd = -1;
    Intra16x16Mode m2 =
        cfs::ChooseIntra16x16Mode(nb, orig, CostMetric::kSatd, &satd);
    assert(m2 == Intra16x16Mode::kVertical);
    assert(satd == 0);
    std::printf("  [6] 16x16 竖条纹 -> Vertical，SAD=SATD=0 ... ok\n");
}

// 测试 7：SAD 与 SATD 在明显方向块上选出同一模式。
void TestSadSatdAgree() {
    // 竖条纹块：Vertical 零残差，其余模式非零。两把尺子都该选 Vertical。
    Neighbors4x4 nb = MakeNb4({10, 90, 10, 90, 90, 90, 90, 90},
                              {50, 50, 50, 50}, 50);
    Block4x4 orig{};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) orig[y][x] = nb.top[x];

    int mode_sad = cfs::ChooseIntra4x4Mode(nb, orig, CostMetric::kSad);
    int mode_satd = cfs::ChooseIntra4x4Mode(nb, orig, CostMetric::kSatd);
    assert(mode_sad == 0);
    assert(mode_satd == 0);
    assert(mode_sad == mode_satd);
    std::printf("  [7] SAD 与 SATD 选出同一模式 ... ok\n");
}

}  // namespace

int main() {
    std::printf("test_intra_mode_decision:\n");
    TestSad();
    TestSatdSinglePoint();
    TestChoose4x4Horizontal();
    TestChoose4x4Vertical();
    TestTieBreak();
    TestChoose16x16();
    TestSadSatdAgree();
    std::printf("all tests passed\n");
    return 0;
}
