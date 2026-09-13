// rbsp_bit_reader.h — H.264 专用的按位读取器，工作在已去转义的 RBSP 上。
//
// 为什么不复用 common/bitreader？两者场景完全不同：
//   - common/BitReader 服务 JPEG 熵段，要在读取时处理 0xFF00 byte stuffing 和
//     marker 边界（那是 JPEG 特有的字节层约定）。
//   - H.264 的 RBSP 是 C1 里已经去掉 emulation_prevention_three_byte 的纯净字节。
//     这里不需要再管转义，只需要 MSB-first 逐位读，外加指数哥伦布(Exp-Golomb)
//     变长解码——这是 H.264 语法元素最主要的编码方式（ITU-T H.264 第 9.1 节）。
//
// 所以本篇给 H.264 单独写一个轻量读取器，专注两件事：
//   1) MSB-first 读 n 位无符号整数（u(n)）。
//   2) ue(v) / se(v) 指数哥伦布解码。
#ifndef CODEC_FROM_SCRATCH_H264_RBSP_BIT_READER_H
#define CODEC_FROM_SCRATCH_H264_RBSP_BIT_READER_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cfs {

// 在一段 RBSP 字节上逐位读取，MSB 在前。
// 越界读取返回 0，并置 overrun 标志（调用方可据此判断码流是否被读穿）。
class RbspBitReader {
 public:
    RbspBitReader(const uint8_t* data, size_t len) : data_(data), len_(len) {}
    explicit RbspBitReader(const std::vector<uint8_t>& v)
        : data_(v.data()), len_(v.size()) {}

    // 读 1 位，返回 0/1。越界返回 0 并置 overrun_。
    uint32_t ReadBit();

    // 读 n 位（0..32），高位先读，拼成无符号整数。u(n)。
    uint32_t ReadBits(int n);

    // ue(v)：无符号指数哥伦布解码（ITU-T H.264 9.1）。
    //   先数前导 0 的个数 leadingZeroBits，
    //   再读同样多的位作为 suffix，
    //   codeNum = 2^leadingZeroBits - 1 + suffix。
    uint32_t ReadUE();

    // se(v)：有符号指数哥伦布解码（ITU-T H.264 9.1.1）。
    //   先按 ue(v) 得到 codeNum k，再映射为带符号值：
    //   0->0, 1->+1, 2->-1, 3->+2, 4->-2 ...
    //   即 value = (-1)^(k+1) * ceil(k/2)。
    int32_t ReadSE();

    // 当前读到的比特位置（供调试/配图）。
    size_t BitPos() const { return bit_pos_; }

    // 是否发生过越界读取。
    bool Overrun() const { return overrun_; }

 private:
    const uint8_t* data_;
    size_t len_;
    size_t bit_pos_ = 0;
    bool overrun_ = false;
};

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_H264_RBSP_BIT_READER_H
