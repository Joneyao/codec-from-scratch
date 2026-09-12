// block_reconstruct.h — 把熵解码出的 64 个 zigzag 系数一路还原成 8x8 像素块。
// 这是 A4→A3→A2 三步的逆，串起来就是 JPEG 解码的"后半段"。
//
// 熵解码交出来的是一维的 64 个量化系数（zigzag 顺序）。要变回像素，反着走三步：
//   1. 反 Zig-Zag：把一维序列按 kZigZagOrder 摆回 8x8（A4 Zig-Zag 扫描的逆）。
//   2. 反量化：每个系数乘以量化表对应步长（A3 量化的逆，T.81 A.3.4 的乘法）。
//      注意：这一步"还原"不回量化时四舍五入丢掉的精度，误差就是这么来的。
//   3. IDCT：8x8 系数做反离散余弦变换，+128 level shift、clamp 回 [0,255]，
//      得到最终像素（A2 DCT 的逆，直接复用编码器的 InverseDct）。
#ifndef CODEC_FROM_SCRATCH_JPEG_DECODER_BLOCK_RECONSTRUCT_H
#define CODEC_FROM_SCRATCH_JPEG_DECODER_BLOCK_RECONSTRUCT_H

#include <array>

#include "dct8x8.h"   // Block8d / Block8u / InverseDct（复用编码器）
#include "quantize.h"  // QuantTable / Dequantize（复用编码器）

namespace cfs {

// 反 Zig-Zag：把一维 64 系数（zigzag 顺序）摆回 8x8 行主序块。
// out[ kZigZagOrder[i] ] = zz[i]，正好是 A4 ZigZagScan 的逆映射。
Block8d InverseZigZag(const std::array<int, 64>& zz);

// 反量化：coeff[v][u] *= quant[v][u]（T.81 A.3.4）。复用编码器 Dequantize，
// 这里包一层是为了让"反量化"在解码侧有个语义清晰的名字。
Block8d DequantizeBlock(const Block8d& quantized, const QuantTable& q);

// IDCT + level shift + clamp：8x8 系数 -> 8x8 像素（0-255）。直接复用 InverseDct。
Block8u ReconstructPixels(const Block8d& coeffs);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_JPEG_DECODER_BLOCK_RECONSTRUCT_H
