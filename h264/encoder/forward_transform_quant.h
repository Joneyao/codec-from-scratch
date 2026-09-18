// forward_transform_quant.h — H.264 4x4 整数正向变换 + 正向量化（严格照 ITU-T
// H.264 8.5，编码侧）。这是解码器 transform_quant.h 里反变换/反量化的镜像。
//
// 编码端拿到残差(residual = 原始块 - 预测块)后，要把它送进码流之前做两步：
//   1) 正向 4x4 整数变换(forward core transform)：和反变换用的是同一套整数核
//      矩阵 Cf，全程加减和乘 2、移位，没有一次浮点乘法。变换把残差的能量集中
//      到左上角(低频)，右下角(高频)大多接近 0。
//   2) 正向量化(quantization)：变换系数 W 乘以正向量化因子 MF(multiplication
//      factor)，加上取整偏移 f，再右移 qbits 位，得到量化 level。这一步是有损的
//      ——大量小系数被压成 0，QP 越大压得越狠。
//
// 与解码侧的自洽性：forward(residual) 再 inverse 能还原到接近原残差，差异只来自
// 量化损失。本文件不含浮点数，MF 表与解码侧的 LevelScale(normAdjust)对偶。
//
// 揭示点：量化 qbits = 15 + qP/6，QP 每加 6，qbits 多 1 位，量化步长翻倍，
// 保留下来的非零系数骤减 —— 也就是"QP 每 +6，码率大致减半"。demo 会用真实
// 数据把这条曲线画出来。
//
// 命名空间 cfs，C++17。qP 取 QPY(0..51)。
#ifndef CODEC_FROM_SCRATCH_H264_FORWARD_TRANSFORM_QUANT_H
#define CODEC_FROM_SCRATCH_H264_FORWARD_TRANSFORM_QUANT_H

// 复用解码侧的 Coeff4x4 定义，保证正-反变换用同一个块类型。
#include "../decoder/transform_quant.h"

namespace cfs {

// 正向 4x4 整数变换(core transform，spec 8.5 编码侧对偶式)。
// 用整数核矩阵 Cf：
//   [ 1  1  1  1 ]
//   [ 2  1 -1 -2 ]
//   [ 1 -1 -1  1 ]
//   [ 1 -2  2 -1 ]
// 先对每一行做一维蝶形，再对每一列做同样的蝶形。输出未量化的变换系数 W。
// 这里只做核变换 Cf·X·Cf^T，不含 spec 里被并进量化的后置缩放(post-scaling)——
// 那部分缩放已经吸收进量化的 MF 表里，与解码侧 LevelScale 精确对偶。
Coeff4x4 ForwardTransform4x4(const Coeff4x4& residual);

// 正向量化(spec 8.5)：对每个变换系数按位置类别取 MF，
//   level = ( |coeff| * MF + f ) >> qbits ，符号单独保留。
//   qbits = 15 + qP/6 ；f 是取整偏移。
// intra=true 用 f = 2^qbits/3(帧内偏移)，intra=false 用 f = 2^qbits/6(帧间偏移)。
// MF 按 spec 分 6 行(m = qP%6) x 3 类位置((0,0)类 / (1,1)类 / 其余)。
Coeff4x4 Quantize4x4(const Coeff4x4& coeff, int qP, bool intra = true);

// 组合：残差 -> 正向整数变换 -> 正向量化 -> 量化系数(进码流的就是它)。
Coeff4x4 ForwardTransformQuant(const Coeff4x4& residual, int qP, bool intra = true);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_H264_FORWARD_TRANSFORM_QUANT_H
