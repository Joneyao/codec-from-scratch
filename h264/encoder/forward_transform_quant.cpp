// forward_transform_quant.cpp — H.264 4x4 整数正向变换 + 正向量化的实现，
// 严格照 ITU-T H.264 8.5（编码侧）。与解码器 transform_quant.cpp 逐位对偶。
//
// 全文件没有一个浮点数：变换是整数加减和乘 2，量化是整数乘 MF、加偏移、右移。
// 这保证编码端算出的量化系数在任何平台上都逐比特一致。
#include "forward_transform_quant.h"

namespace cfs {

namespace {

// 正向量化因子 MF（multiplication factor，spec 表 8-15 对应值）。
// 6 行(m = qP%6) x 3 列(位置类别)，与解码侧 normAdjust v 精确对偶：
//   列 0：位置 (0,0)(0,2)(2,0)(2,2)  —— 对偶 v 的 {10,11,13,14,16,18}
//   列 1：位置 (1,1)(1,3)(3,1)(3,3)  —— 对偶 v 的 {16,18,20,23,25,29}
//   列 2：其余位置                    —— 对偶 v 的 {13,14,16,18,20,23}
// 标准常量：列 0 = {13107,11916,10082,9362,8192,7282}。
constexpr int kMF[6][3] = {
    {13107, 5243, 8066},
    {11916, 4660, 7490},
    {10082, 4194, 6554},
    {9362, 3647, 5825},
    {8192, 3355, 5243},
    {7282, 2893, 4559},
};

// 位置 (i, j) 到 MF 的列索引（与解码侧 PosClass 完全一致）。
inline int PosClass(int i, int j) {
    const bool i_even = (i % 2 == 0);
    const bool j_even = (j % 2 == 0);
    if (i_even && j_even) return 0;    // (0,0)(0,2)(2,0)(2,2)
    if (!i_even && !j_even) return 1;  // (1,1)(1,3)(3,1)(3,3)
    return 2;                          // 其余
}

}  // namespace

Coeff4x4 ForwardTransform4x4(const Coeff4x4& residual) {
    // ---- 第一步：对每一行做一维正向蝶形（水平方向）----
    // 核矩阵 Cf 每行：sum/diff 先算 a0..a3，再组合出 4 个频率分量。
    int t[4][4];
    for (int i = 0; i < 4; ++i) {
        const int a0 = residual[i][0] + residual[i][3];
        const int a1 = residual[i][1] + residual[i][2];
        const int a2 = residual[i][1] - residual[i][2];
        const int a3 = residual[i][0] - residual[i][3];
        t[i][0] = a0 + a1;         // 行 [1  1  1  1]
        t[i][1] = 2 * a3 + a2;     // 行 [2  1 -1 -2]
        t[i][2] = a0 - a1;         // 行 [1 -1 -1  1]
        t[i][3] = a3 - 2 * a2;     // 行 [1 -2  2 -1]
    }

    // ---- 第二步：对每一列做同样的一维正向蝶形（垂直方向）----
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

Coeff4x4 Quantize4x4(const Coeff4x4& coeff, int qP, bool intra) {
    const int m = qP % 6;
    const int qbits = 15 + qP / 6;
    // 取整偏移 f：帧内 2^qbits/3，帧间 2^qbits/6。用 long long 防大 QP 溢出。
    const long long f = intra ? ((1LL << qbits) / 3) : ((1LL << qbits) / 6);

    Coeff4x4 level{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            const long long w = coeff[i][j];
            const long long absw = w < 0 ? -w : w;
            const long long mf = kMF[m][PosClass(i, j)];
            // level = (|W|*MF + f) >> qbits，符号单独保留。
            const long long q = (absw * mf + f) >> qbits;
            level[i][j] = static_cast<int>(w < 0 ? -q : q);
        }
    return level;
}

Coeff4x4 ForwardTransformQuant(const Coeff4x4& residual, int qP, bool intra) {
    return Quantize4x4(ForwardTransform4x4(residual), qP, intra);
}

}  // namespace cfs
