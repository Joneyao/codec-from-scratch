// dct8x8.h — 8x8 二维离散余弦变换（JPEG 编码第二步）
//
// 依据 ITU-T T.81 Annex A.3.3。这里用最直白的双重求和实现（O(N^4)），
// 追求可读而非速度——目的是让公式和代码一一对应。
//
// 编码前先做 level shift：8 bit 采样减 128，变成 [-128,127] 的有符号值
// （T.81 A.3.1）。解码后 IDCT 结果加 128 并 clamp 回 [0,255]。
#ifndef CODEC_FROM_SCRATCH_JPEG_DCT8X8_H
#define CODEC_FROM_SCRATCH_JPEG_DCT8X8_H

#include <array>
#include <cstdint>
#include <string>

namespace cfs {

// 一个 8x8 块。像素块用 uint8（0-255），系数/中间结果用 double。
using Block8u = std::array<std::array<uint8_t, 8>, 8>;
using Block8d = std::array<std::array<double, 8>, 8>;

// 正向 DCT：输入 8x8 的 8bit 像素块，内部做 level shift(-128)，
// 输出 8x8 DCT 系数（浮点）。左上角 [0][0] 为 DC 系数。
Block8d ForwardDct(const Block8u& pixels);

// 反向 DCT：输入 8x8 系数，输出重建像素块（加 128、clamp 到 0-255）。
Block8u InverseDct(const Block8d& coeffs);

// 把一个浮点 8x8 块导出为文本矩阵，供 matplotlib 读取绘图。
// round=true 时四舍五入为整数（系数图更好看）。
bool DumpBlock(const Block8d& b, const std::string& path, bool round = true);
bool DumpBlockU8(const Block8u& b, const std::string& path);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_JPEG_DCT8X8_H
