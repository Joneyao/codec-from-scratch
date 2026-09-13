// transform_quant.h — H.264 的 4x4 整数反变换与反量化（严格照 ITU-T H.264 8.5）。
//
// 上一篇用帧内预测(Intra Prediction)猜出了当前块的样子，预测块和真实块之差
// 就是残差(residual)。残差不会直接进码流：编码端先对残差做变换、再量化，
// 解出来的是一堆量化系数(quantized coefficients)。这一篇做的就是逆过程——
// 从量化系数出发，反量化(scaling)恢复出变换系数，再做 4x4 整数反变换
// (inverse transform)重建出残差块。
//
// H.264 最关键的一个工程决定：它不用 JPEG 那种浮点 DCT(离散余弦变换)，而是
// 自己设计了一套“整数变换”——变换矩阵只含 ±1、±2 这样的小整数，全程只用加法
// 和移位就能算完，一次浮点乘法都不需要。这样带来一个决定性的好处：
// 反变换结果在任何 CPU、任何编译器、任何优化级别下都逐比特一致(bit-exact)，
// 编码端和解码端永远算出完全相同的残差，预测环路里不会累积漂移(drift)。
//
// 本文件实现两件事（对应 spec 8.5.8 的 residual 4x4）：
//   1) 反量化 Scaling(8.5.8 式 8-265)：量化系数 c_ij 乘以缩放因子 LevelScale，
//      再按 QP(量化参数, Quantization Parameter)左移。QP 每增加 6，缩放翻一倍。
//   2) 4x4 整数反变换(8.5.8 式 8-266..8-282)：一维反变换先做水平行、再做垂直列，
//      核心是加减和 >>1 的“蝶形运算(butterfly)”，最后 (h+32)>>6 归一化。
//
// 命名空间 cfs，C++17。像素范围 8bit，qP 取 QPY(0..51)。
#ifndef CODEC_FROM_SCRATCH_H264_TRANSFORM_QUANT_H
#define CODEC_FROM_SCRATCH_H264_TRANSFORM_QUANT_H

#include <array>
#include <cstdint>

namespace cfs {

// 一个 4x4 系数/残差块：block[y][x]，x,y = 0..3。元素可正可负（残差是有符号的）。
using Coeff4x4 = std::array<std::array<int, 4>, 4>;

// 反量化用的缩放因子 LevelScale(m, i, j)（spec 8.5.8 式 8-252 / 8-253）。
//   m = qP % 6（0..5），(i, j) 是块内位置。
// 位置分三类：
//   (0,0)(0,2)(2,0)(2,2) -> 取 v[m][0]
//   (1,1)(1,3)(3,1)(3,3) -> 取 v[m][1]
//   其余                 -> 取 v[m][2]
int LevelScale(int m, int i, int j);

// 反量化 Scaling（spec 8.5.8 式 8-265）：
//   d_ij = ( c_ij * LevelScale(qP%6, i, j) ) << ( qP / 6 )
// 输入量化系数 c，输出反量化后的变换系数 d。qP = QPY(0..51)。
// 说明：本实现针对普通 4x4 残差块（非 Intra_16x16 luma DC、非 chroma DC 的
// 特例），DC 位置 (0,0) 也走同一式子。
Coeff4x4 DequantResidual4x4(const Coeff4x4& c, int qP);

// 4x4 整数反变换（spec 8.5.8 式 8-266..8-282）：
//   先对每一行做一维反变换，再对每一列做同样的一维反变换，
//   最后 r_ij = ( h_ij + 32 ) >> 6。
// 输入反量化后的系数 d，输出残差块 r。全程整数加减与移位，结果完全确定。
Coeff4x4 InverseTransform4x4(const Coeff4x4& d);

// 组合：量化系数 -> 反量化 -> 反变换 -> 残差块（本篇处理残差的完整路径）。
Coeff4x4 ReconstructResidual4x4(const Coeff4x4& c, int qP);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_H264_TRANSFORM_QUANT_H
