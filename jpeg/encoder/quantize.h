// quantize.h — 量化 / 反量化（JPEG 编码第三步，也是唯一"有损"的一步）
//
// DCT（上一篇）只是把像素块换个基底表示，信息不丢；熵编码（下一篇）是无损打包。
// 真正丢数据的地方只有这里：把每个 DCT 系数除以量化表对应的步长，再四舍五入取整。
// 步长越大，越多系数被压成 0，画质越低、文件越小。
//
// 量化过程见 ITU-T T.81 A.3.4；推荐量化表见 Annex K（Table K.1 亮度、K.2 色度）。
// 这两张表是 50 质量的基准，其它质量由一个缩放公式推导（见 quantize.cpp）。
#ifndef CODEC_FROM_SCRATCH_JPEG_QUANTIZE_H
#define CODEC_FROM_SCRATCH_JPEG_QUANTIZE_H

#include <array>
#include <cstdint>
#include <string>

#include "dct8x8.h"  // 复用 Block8d / DumpBlock

namespace cfs {

// 一张 8x8 量化表，每个位置是对应频率系数的量化步长（1-255）。
using QuantTable = std::array<std::array<uint16_t, 8>, 8>;

// T.81 Annex K 的推荐基准表（对应质量 50）。
// kBaseLuma = Table K.1（亮度），kBaseChroma = Table K.2（色度）。
extern const QuantTable kBaseLuma;
extern const QuantTable kBaseChroma;

// 按质量因子 quality(1-100) 缩放基准表，得到实际使用的量化表。
// 缩放规则（libjpeg 沿用的经典做法）：
//   quality < 50 : scale = 5000 / quality
//   quality >= 50: scale = 200 - 2 * quality
//   每项 = (base * scale + 50) / 100，clamp 到 [1, 255]。
// quality 越大 scale 越小，步长越接近 1（越接近无损）。
QuantTable ScaleTable(const QuantTable& base, int quality);

// 量化：对每个系数做 round(coeff / step)。返回的是"量化后的整数系数"
// （仍用 Block8d 存放，方便统一导出，但值都是整数）。
Block8d Quantize(const Block8d& coeffs, const QuantTable& table);

// 反量化：quantized[v][u] * step，得到解码端能拿到的近似系数。
// 反量化 != 还原，量化时四舍五入丢掉的小数永远回不来了。
Block8d Dequantize(const Block8d& quantized, const QuantTable& table);

// 把量化表导出为文本矩阵（整数），供 matplotlib 画热力图。
bool DumpQuantTable(const QuantTable& t, const std::string& path);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_JPEG_QUANTIZE_H
