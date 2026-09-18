// intra_mode_decision.cpp — 帧内模式决策的实现。
//
// 三件事：算 SAD、算 SATD、拿它们当尺子挑模式。预测块一律交给 decoder 里
// 已按 spec 实现的 PredictIntra4x4 / PredictIntra16x16 产出，本文件只负责
// 「量残差 + 比大小」。
#include "intra_mode_decision.h"

#include <array>
#include <cstdlib>

namespace cfs {

namespace {

// ── SAD：逐像素 |orig − pred| 累加 ──
template <int N>
int SadN(const std::array<std::array<int, N>, N>& orig,
         const std::array<std::array<int, N>, N>& pred) {
    int sad = 0;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
            sad += std::abs(orig[y][x] - pred[y][x]);
    return sad;
}

// ── 4x4 Hadamard 变换 ──
// H.264 里 SATD 用的是 4x4 Hadamard（不是 DCT）。分两趟蝶形：先按行、再按列。
// 变换矩阵每个元素是 ±1，所以整个变换只有加减法，没有乘法。
//   H4 = [ 1  1  1  1
//          1  1 -1 -1
//          1 -1 -1  1
//          1 -1  1 -1 ]
// 对残差块 d 做 T = H4 · d · H4^T，等价于先对每行做一维蝶形，再对每列做一维
// 蝶形。返回的是变换系数（未做任何归一化，SATD 只取绝对值和，尺度不影响选择）。
void Hadamard4x4(const std::array<std::array<int, 4>, 4>& in,
                 std::array<std::array<int, 4>, 4>& out) {
    std::array<std::array<int, 4>, 4> tmp{};

    // 第一趟：对每一行做一维 Hadamard（4 点蝶形）。
    for (int i = 0; i < 4; ++i) {
        int a0 = in[i][0] + in[i][2];
        int a1 = in[i][1] + in[i][3];
        int a2 = in[i][0] - in[i][2];
        int a3 = in[i][1] - in[i][3];
        tmp[i][0] = a0 + a1;
        tmp[i][1] = a2 + a3;
        tmp[i][2] = a2 - a3;
        tmp[i][3] = a0 - a1;
    }

    // 第二趟：对每一列做一维 Hadamard。
    for (int j = 0; j < 4; ++j) {
        int a0 = tmp[0][j] + tmp[2][j];
        int a1 = tmp[1][j] + tmp[3][j];
        int a2 = tmp[0][j] - tmp[2][j];
        int a3 = tmp[1][j] - tmp[3][j];
        out[0][j] = a0 + a1;
        out[1][j] = a2 + a3;
        out[2][j] = a2 - a3;
        out[3][j] = a0 - a1;
    }
}

// 对一个 4x4 残差块求 SATD：Hadamard 后对所有系数取绝对值累加。
int Satd4x4Block(const std::array<std::array<int, 4>, 4>& residual) {
    std::array<std::array<int, 4>, 4> coeff{};
    Hadamard4x4(residual, coeff);
    int satd = 0;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            satd += std::abs(coeff[y][x]);
    // 归一化：4x4 Hadamard 的能量放大因子是 4（两趟一维变换各放大 2）。
    // 除掉它让 SATD 的量级和 SAD 可比，便于文章里同尺度对照。这一步对「谁最小」
    // 的排序没有任何影响（所有模式同除一个常数）。
    return (satd + 1) >> 1;
}

}  // namespace

// ── 对外的代价函数 ──

int Sad4x4Cost(const Block4x4& original, const Block4x4& predicted) {
    return SadN<4>(original, predicted);
}

int Sad16x16Cost(const Block16x16& original, const Block16x16& predicted) {
    return SadN<16>(original, predicted);
}

int Satd4x4Cost(const Block4x4& original, const Block4x4& predicted) {
    std::array<std::array<int, 4>, 4> residual{};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            residual[y][x] = original[y][x] - predicted[y][x];
    return Satd4x4Block(residual);
}

int Satd16x16Cost(const Block16x16& original, const Block16x16& predicted) {
    // 16x16 拆成 16 个 4x4 子块，各自做 Hadamard SATD 再累加。
    int total = 0;
    std::array<std::array<int, 4>, 4> residual{};
    for (int by = 0; by < 16; by += 4) {
        for (int bx = 0; bx < 16; bx += 4) {
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                    residual[y][x] =
                        original[by + y][bx + x] - predicted[by + y][bx + x];
            total += Satd4x4Block(residual);
        }
    }
    return total;
}

int Cost4x4(const Block4x4& original, const Block4x4& predicted,
            CostMetric metric) {
    return metric == CostMetric::kSad ? Sad4x4Cost(original, predicted)
                                      : Satd4x4Cost(original, predicted);
}

int Cost16x16(const Block16x16& original, const Block16x16& predicted,
              CostMetric metric) {
    return metric == CostMetric::kSad ? Sad16x16Cost(original, predicted)
                                      : Satd16x16Cost(original, predicted);
}

// ── 模式决策 ──

int ChooseIntra4x4Mode(const Neighbors4x4& nb, const Block4x4& original,
                       CostMetric metric, int* out_cost) {
    int best_mode = 0;
    int best_cost = -1;
    // 9 种模式 0..8 逐个试（枚举定义见 8.3.1 表 8-2）。
    for (int m = 0; m < 9; ++m) {
        Block4x4 pred = PredictIntra4x4(nb, static_cast<Intra4x4Mode>(m));
        int cost = Cost4x4(original, pred, metric);
        // 严格小于才更新 -> 平手时保留较小模式号。
        if (best_cost < 0 || cost < best_cost) {
            best_cost = cost;
            best_mode = m;
        }
    }
    if (out_cost) *out_cost = best_cost;
    return best_mode;
}

Intra16x16Mode ChooseIntra16x16Mode(const Neighbors16x16& nb,
                                    const Block16x16& original,
                                    CostMetric metric, int* out_cost) {
    int best_mode = 0;
    int best_cost = -1;
    // 4 种模式 0..3 逐个试（枚举定义见 8.3.3 表 8-3）。
    for (int m = 0; m < 4; ++m) {
        Block16x16 pred = PredictIntra16x16(nb, static_cast<Intra16x16Mode>(m));
        int cost = Cost16x16(original, pred, metric);
        if (best_cost < 0 || cost < best_cost) {
            best_cost = cost;
            best_mode = m;
        }
    }
    if (out_cost) *out_cost = best_cost;
    return static_cast<Intra16x16Mode>(best_mode);
}

}  // namespace cfs
