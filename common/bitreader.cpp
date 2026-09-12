// bitreader.cpp — 逐位读取实现，MSB first，处理 byte stuffing 与 marker 边界。
#include "bitreader.h"

namespace cfs {

bool BitReader::FillByte() {
    if (byte_pos_ >= len_) {
        eof_ = true;
        return false;
    }
    uint8_t b = data_[byte_pos_];
    if (b != 0xFF) {
        // 普通数据字节，直接用。
        ++byte_pos_;
        cur_ = b;
        cur_bits_ = 8;
        return true;
    }
    // b == 0xFF：看下一个字节决定是"被塞过的 0xFF"还是一个 marker。
    if (byte_pos_ + 1 >= len_) {
        // 数据末尾孤零零一个 0xFF，当作耗尽处理。
        eof_ = true;
        return false;
    }
    uint8_t nxt = data_[byte_pos_ + 1];
    if (nxt == 0x00) {
        // byte stuffing：0xFF00 还原成一个真正的 0xFF 数据字节（T.81 F.1.2.3）。
        byte_pos_ += 2;
        cur_ = 0xFF;
        cur_bits_ = 8;
        return true;
    }
    // 0xFF 后跟非 0x00 —— 这是一个 marker，熵数据到头了。
    hit_marker_ = true;
    marker_byte_ = nxt;
    return false;
}

int BitReader::ReadBit() {
    if (cur_bits_ == 0) {
        if (!FillByte()) return 0;  // marker/EOF：调用方应先查 AtMarker()/Eof()
    }
    --cur_bits_;
    return static_cast<int>((cur_ >> cur_bits_) & 1u);
}

uint32_t BitReader::ReadBits(int nbits) {
    uint32_t v = 0;
    for (int i = 0; i < nbits; ++i) {
        v = (v << 1) | static_cast<uint32_t>(ReadBit());
        if (hit_marker_ || eof_) break;
    }
    return v;
}

}  // namespace cfs
