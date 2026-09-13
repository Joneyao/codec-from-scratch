// transform_quant.cpp — H.264 4x4 整数反变换与反量化的实现，严格照 ITU-T H.264 8.5.8。
//
// 关键点：整个文件里没有一个浮点数。反量化是整数乘 + 左移，反变换是整数加减 +
// 右移一位。这正是 H.264 相对 JPEG 浮点 DCT 的根本区别——结果在任何平台上都
// 逐比特一致(bit-exact)，不会有浮点舍入带来的平台差异。
#include "transform_quant.h"

namespace cfs {

namespace {

// normAdjust 矩阵 v（spec 式 8-253）：6 行(m = qP%6) x 3 列(位置类别)。
// 只含 10..29 这样的小整数——这就是“整数变换/整数量化”名字的由来。
constexpr int kV[6][3] = {
    {10, 16, 13},
    {11, 18, 14},
    {13, 20, 16},
    {14, 23, 18},
    {16, 25, 20},
    {18, 29, 23},
};

// 位置 (i, j) 到 v 的列索引（spec 式 8-252）。
inline int PosClass(int i, int j) {
    const bool i_even = (i % 2 == 0);
    const bool j_even = (j % 2 == 0);
    if (i_even && j_even) return 0;   // (0,0)(0,2)(2,0)(2,2)
    if (!i_even && !j_even) return 1;  // (1,1)(1,3)(3,1)(3,3)
    return 2;                          // 其余
}

}  // namespace

int LevelScale(int m, int i, int j) {
    return kV[m][PosClass(i, j)];
}

Coeff4x4 DequantResidual4x4(const Coeff4x4& c, int qP) {
    const int m = qP % 6;
    const int shift = qP / 6;
    Coeff4x4 d{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            // 式 8-265：d_ij = ( c_ij * LevelScale(qP%6,i,j) ) << (qP/6)
            d[i][j] = (c[i][j] * LevelScale(m, i, j)) << shift;
    return d;
}

Coeff4x4 InverseTransform4x4(const Coeff4x4& d) {
    // ---- 第一步：对每一行做一维反变换（水平方向，spec 式 8-266..8-273）----
    // e/f 是行变换的中间量与结果。i 是行号，做“列内”的蝶形。
    int f[4][4];
    for (int i = 0; i < 4; ++i) {
        const int e0 = d[i][0] + d[i][2];        // 8-266
        const int e1 = d[i][0] - d[i][2];        // 8-267
        const int e2 = (d[i][1] >> 1) - d[i][3];  // 8-268
        const int e3 = d[i][1] + (d[i][3] >> 1);  // 8-269
        f[i][0] = e0 + e3;                        // 8-270
        f[i][1] = e1 + e2;                        // 8-271
        f[i][2] = e1 - e2;                        // 8-272
        f[i][3] = e0 - e3;                        // 8-273
    }

    // ---- 第二步：对每一列做同样的一维反变换（垂直方向，spec 式 8-274..8-281）----
    int h[4][4];
    for (int j = 0; j < 4; ++j) {
        const int g0 = f[0][j] + f[2][j];         // 8-274
        const int g1 = f[0][j] - f[2][j];         // 8-275
        const int g2 = (f[1][j] >> 1) - f[3][j];  // 8-276
        const int g3 = f[1][j] + (f[3][j] >> 1);  // 8-277
        h[0][j] = g0 + g3;                        // 8-278
        h[1][j] = g1 + g2;                        // 8-279
        h[2][j] = g1 - g2;                        // 8-280
        h[3][j] = g0 - g3;                        // 8-281
    }

    // ---- 第三步：归一化，r_ij = ( h_ij + 32 ) >> 6（spec 式 8-282）----
    Coeff4x4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r[i][j] = (h[i][j] + 32) >> 6;
    return r;
}

Coeff4x4 ReconstructResidual4x4(const Coeff4x4& c, int qP) {
    return InverseTransform4x4(DequantResidual4x4(c, qP));
}

}  // namespace cfs
