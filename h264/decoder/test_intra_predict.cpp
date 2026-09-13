// test_intra_predict.cpp — 校验 9 种 Intra_4x4 与 4 种 Intra_16x16 的公式。
//
// 用可手算验证的构造输入，逐模式核对：
//   1) Vertical：每列 = 对应上邻像素（竖条纹）。
//   2) Horizontal：每行 = 对应左邻像素（横条纹）。
//   3) DC：全块 = 邻居均值（上左邻全 100 -> DC=100；带舍入的用例也核对）。
//   4) DC 回退：只有左邻/只有上邻/都没有(=128) 三条支路。
//   5) Diagonal_Down_Right 在特定输入下的角点值。
//   6) 16x16 Vertical/Horizontal/DC。
//   7) 16x16 Plane：邻居沿水平线性渐变时，预测块也水平线性渐变。
//   8) SAD：预测==真实时为 0；差 1 的像素累计正确。
#include <array>
#include <cstdio>

#include "intra_predict.h"

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

using cfs::Block4x4;
using cfs::Intra16x16Mode;
using cfs::Intra4x4Mode;
using cfs::Neighbors16x16;
using cfs::Neighbors4x4;

// 造一组邻居：上邻 top[0..7]，左邻 left[0..3]，左上角 tl。
Neighbors4x4 MakeNb(std::array<int, 8> top, std::array<int, 4> left, int tl) {
    Neighbors4x4 nb;
    nb.top = top;
    nb.left = left;
    nb.top_left = tl;
    return nb;
}

// 测试 1：Vertical，每列复制上邻。
void TestVertical() {
    Neighbors4x4 nb = MakeNb({10, 20, 30, 40, 0, 0, 0, 0}, {99, 99, 99, 99}, 0);
    Block4x4 p = cfs::PredictIntra4x4(nb, Intra4x4Mode::kVertical);
    for (int y = 0; y < 4; ++y) {
        CHECK(p[y][0] == 10);
        CHECK(p[y][1] == 20);
        CHECK(p[y][2] == 30);
        CHECK(p[y][3] == 40);
    }
}

// 测试 2：Horizontal，每行复制左邻。
void TestHorizontal() {
    Neighbors4x4 nb = MakeNb({99, 99, 99, 99, 0, 0, 0, 0}, {11, 22, 33, 44}, 0);
    Block4x4 p = cfs::PredictIntra4x4(nb, Intra4x4Mode::kHorizontal);
    for (int x = 0; x < 4; ++x) {
        CHECK(p[0][x] == 11);
        CHECK(p[1][x] == 22);
        CHECK(p[2][x] == 33);
        CHECK(p[3][x] == 44);
    }
}

// 测试 3：DC，上邻左邻全 100 -> DC=100；带舍入的用例。
void TestDC() {
    // 全 100：(100*8 + 4) >> 3 = 100。
    Neighbors4x4 nb1 = MakeNb({100, 100, 100, 100, 0, 0, 0, 0},
                              {100, 100, 100, 100}, 0);
    Block4x4 p1 = cfs::PredictIntra4x4(nb1, Intra4x4Mode::kDC);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) CHECK(p1[y][x] == 100);

    // 上邻 {10,10,10,10} 左邻 {20,20,20,20}：和=120，(120+4)>>3 = 15。
    Neighbors4x4 nb2 = MakeNb({10, 10, 10, 10, 0, 0, 0, 0},
                              {20, 20, 20, 20}, 0);
    Block4x4 p2 = cfs::PredictIntra4x4(nb2, Intra4x4Mode::kDC);
    CHECK(p2[0][0] == 15);
    CHECK(p2[3][3] == 15);
}

// 测试 4：DC 回退三条支路。
void TestDCFallback() {
    // 只有左邻可用：(left 和 + 2) >> 2。左邻全 80 -> 80。
    Neighbors4x4 nl = MakeNb({0, 0, 0, 0, 0, 0, 0, 0}, {80, 80, 80, 80}, 0);
    nl.top_available = false;
    Block4x4 pl = cfs::PredictIntra4x4(nl, Intra4x4Mode::kDC);
    CHECK(pl[0][0] == 80);

    // 只有上邻可用：(top 和 + 2) >> 2。上邻全 60 -> 60。
    Neighbors4x4 nt = MakeNb({60, 60, 60, 60, 0, 0, 0, 0}, {0, 0, 0, 0}, 0);
    nt.left_available = false;
    Block4x4 pt = cfs::PredictIntra4x4(nt, Intra4x4Mode::kDC);
    CHECK(pt[0][0] == 60);

    // 都不可用 -> 128。
    Neighbors4x4 nn = MakeNb({0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}, 0);
    nn.top_available = false;
    nn.left_available = false;
    Block4x4 pn = cfs::PredictIntra4x4(nn, Intra4x4Mode::kDC);
    CHECK(pn[0][0] == 128);
    CHECK(pn[3][3] == 128);
}

// 测试 5：Diagonal_Down_Right 的对角线值。左上角 p[-1,-1]=50，上邻左邻各常量。
// x==y 时 pred = (p[0,-1] + 2*p[-1,-1] + p[-1,0] + 2) >> 2。
void TestDDR() {
    // top={40,...}, left={60,...}, tl=50。对角线 (0,0): (40 + 100 + 60 + 2)>>2 = 50。
    Neighbors4x4 nb = MakeNb({40, 40, 40, 40, 40, 40, 40, 40},
                             {60, 60, 60, 60}, 50);
    Block4x4 p = cfs::PredictIntra4x4(nb, Intra4x4Mode::kDiagonalDownRight);
    CHECK(p[0][0] == 50);  // (40+2*50+60+2)>>2 = 202>>2 = 50
}

// 测试 6：16x16 Vertical / Horizontal / DC。
void Test16x16Basic() {
    Neighbors16x16 nb;
    for (int i = 0; i < 16; ++i) {
        nb.top[i] = 100 + i;  // 递增上邻
        nb.left[i] = 50;      // 常量左邻
    }
    nb.top_left = 0;

    // Vertical：每列 = 上邻。
    auto v = cfs::PredictIntra16x16(nb, Intra16x16Mode::kVertical);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) CHECK(v[y][x] == 100 + x);

    // Horizontal：每行 = 左邻(全 50)。
    auto h = cfs::PredictIntra16x16(nb, Intra16x16Mode::kHorizontal);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) CHECK(h[y][x] == 50);

    // DC：上邻和 = sum(100..115) = 1720，左邻和 = 50*16 = 800，
    // (1720 + 800 + 16) >> 5 = 2536 >> 5 = 79。
    auto d = cfs::PredictIntra16x16(nb, Intra16x16Mode::kDC);
    CHECK(d[0][0] == 79);
    CHECK(d[15][15] == 79);
}

// 测试 7：16x16 Plane，邻居沿水平线性渐变时预测块水平线性变化。
// 构造：上邻 p[x,-1] = 100 + 2x（水平斜率 2），左邻全 100（垂直无斜率），
// 左上角配合线性。期望预测块每行相同、每列按固定步长递增（水平渐变）。
void Test16x16Plane() {
    Neighbors16x16 nb;
    for (int i = 0; i < 16; ++i) {
        nb.top[i] = 100 + 2 * i;   // 水平线性
        nb.left[i] = 100;          // 垂直平坦
    }
    nb.top_left = 100;

    auto p = cfs::PredictIntra16x16(nb, Intra16x16Mode::kPlane);
    // 同一行内随 x 单调不减（水平渐变），且各行 x 方向步长一致。
    for (int y = 0; y < 16; ++y)
        for (int x = 1; x < 16; ++x) CHECK(p[y][x] >= p[y][x - 1]);
    int step_row0 = p[0][15] - p[0][0];
    int step_row8 = p[8][15] - p[8][0];
    CHECK(step_row0 == step_row8);  // 每行水平变化幅度一致 = 线性平面
    CHECK(step_row0 > 0);           // 确实在水平方向渐变
}

// 测试 8：SAD。
void TestSad() {
    Block4x4 a{}, b{};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            a[y][x] = 100;
            b[y][x] = 100;
        }
    CHECK(cfs::Sad4x4(a, b) == 0);  // 完全相同
    b[0][0] = 105;                  // 差 5
    b[1][1] = 97;                   // 差 3
    CHECK(cfs::Sad4x4(a, b) == 8);
}

}  // namespace

int main() {
    TestVertical();
    TestHorizontal();
    TestDC();
    TestDCFallback();
    TestDDR();
    Test16x16Basic();
    Test16x16Plane();
    TestSad();

    if (g_failures != 0) {
        std::fprintf(stderr, "test_intra_predict: %d FAILURE(s)\n", g_failures);
        return 1;
    }
    std::printf("test_intra_predict: 全部通过\n");
    return 0;
}
