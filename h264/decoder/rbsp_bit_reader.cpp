// rbsp_bit_reader.cpp — MSB-first 逐位读取 + 指数哥伦布解码的实现。
#include "rbsp_bit_reader.h"

namespace cfs {

uint32_t RbspBitReader::ReadBit() {
    size_t byte_idx = bit_pos_ >> 3;         // 当前位落在第几个字节
    if (byte_idx >= len_) {
        overrun_ = true;
        return 0;
    }
    // MSB first：bit_pos_ 的低 3 位决定字节内位置，0 是最高位(第7位)。
    int shift = 7 - static_cast<int>(bit_pos_ & 7);
    uint32_t bit = (data_[byte_idx] >> shift) & 1u;
    ++bit_pos_;
    return bit;
}

uint32_t RbspBitReader::ReadBits(int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) {
        v = (v << 1) | ReadBit();
    }
    return v;
}

void RbspBitReader::SkipBits(size_t n) {
    bit_pos_ += n;
    if ((bit_pos_ >> 3) > len_) overrun_ = true;
}

uint32_t RbspBitReader::ReadUE() {
    // 9.1: leadingZeroBits = -1; for(b=0; !b; leadingZeroBits++) b = read_bits(1);
    // 即：一直读 0，直到读到第一个 1，数出前面有几个 0。
    int leading_zeros = 0;
    while (ReadBit() == 0 && !overrun_) {
        ++leading_zeros;
    }
    // codeNum = 2^leadingZeroBits - 1 + read_bits(leadingZeroBits)
    uint32_t suffix = ReadBits(leading_zeros);
    return (1u << leading_zeros) - 1u + suffix;
}

int32_t RbspBitReader::ReadSE() {
    // 9.1.1: 由 codeNum k 映射到带符号值。
    // k=0 -> 0；k 为奇数 -> +(k+1)/2；k 为偶数(且>0) -> -(k/2)。
    // 合并成一个式子：value = (-1)^(k+1) * ceil(k/2)。
    uint32_t k = ReadUE();
    int32_t magnitude = static_cast<int32_t>((k + 1) >> 1);  // ceil(k/2)
    return (k & 1) ? magnitude : -magnitude;
}

}  // namespace cfs
