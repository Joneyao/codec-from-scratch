// bitwriter.cpp — 逐位写入实现，MSB first。
#include "bitwriter.h"

namespace cfs {

void BitWriter::WriteBits(uint32_t value, int nbits) {
    if (nbits <= 0) return;
    // 逐位从高到低取出，塞进 8 位累加器，满 8 位就吐一个字节。
    for (int i = nbits - 1; i >= 0; --i) {
        uint32_t bit = (value >> i) & 1u;
        acc_ = (acc_ << 1) | bit;
        ++acc_bits_;
        ++bit_count_;
        if (acc_bits_ == 8) {
            bytes_.push_back(static_cast<uint8_t>(acc_ & 0xFF));
            acc_ = 0;
            acc_bits_ = 0;
        }
    }
}

std::vector<uint8_t> BitWriter::TakeBytes() {
    if (acc_bits_ > 0) {
        // 结尾不足 8 位，低位补 1（T.81 F.1.2.3）。
        acc_ = (acc_ << (8 - acc_bits_)) | ((1u << (8 - acc_bits_)) - 1u);
        bytes_.push_back(static_cast<uint8_t>(acc_ & 0xFF));
        acc_ = 0;
        acc_bits_ = 0;
    }
    std::vector<uint8_t> out;
    out.swap(bytes_);
    return out;
}

}  // namespace cfs
