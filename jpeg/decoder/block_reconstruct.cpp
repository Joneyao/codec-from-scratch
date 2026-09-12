// block_reconstruct.cpp — 反 Zig-Zag / 反量化 / IDCT，串成解码后半段。
#include "block_reconstruct.h"

#include "zigzag_rle.h"  // kZigZagOrder（复用编码器的扫描顺序表）

namespace cfs {

Block8d InverseZigZag(const std::array<int, 64>& zz) {
    Block8d out{};
    for (int i = 0; i < 64; ++i) {
        // kZigZagOrder[i] 是一维第 i 个系数对应的行主序下标 row*8+col。
        int idx = kZigZagOrder[i];
        out[idx / 8][idx % 8] = static_cast<double>(zz[i]);
    }
    return out;
}

Block8d DequantizeBlock(const Block8d& quantized, const QuantTable& q) {
    return Dequantize(quantized, q);  // coeff * step，T.81 A.3.4。
}

Block8u ReconstructPixels(const Block8d& coeffs) {
    return InverseDct(coeffs);  // 反 DCT + 128 + clamp（T.81 A.3.3）。
}

}  // namespace cfs
