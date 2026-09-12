// bitreader.h — 按位读取器，熵解码阶段用来把 JFIF 里那段熵编码数据一位一位
// 抠出来（霍夫曼码是变长的，必须逐 bit 读，读一位判断一次是否凑成了码字）。
// codec-from-scratch / common
//
// 它是 bitwriter 的镜像：写的时候 MSB first，读的时候也 MSB first
// （T.81 F.1.2.3）。但读比写多两件麻烦事，都源于"熵数据里 0xFF 有歧义"：
//   1. byte stuffing 还原：熵数据里真正的 0xFF 后面会跟一个 0x00
//      （编码时塞进去防止和 marker 撞车），读时遇到 0xFF00 要把 0x00 吞掉，
//      只返回一个 0xFF（T.81 F.1.2.3）。
//   2. marker 边界：如果 0xFF 后面跟的不是 0x00，那是一个真正的 marker
//      （比如重启标记 0xFFD0..0xFFD7，或结束标记 0xFFD9），此时熵数据到头了，
//      读取器要停下，不能把 marker 当数据吞掉。
//
// 本篇（A6）重点是解析结构 + 建霍夫曼查表，真正用它逐系数解码留给 A7。
// 但先把这个正确处理 0xFF 的读取器搭好，A7 才不用返工。
#ifndef CODEC_FROM_SCRATCH_COMMON_BITREADER_H
#define CODEC_FROM_SCRATCH_COMMON_BITREADER_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cfs {

// 从一段字节流里逐 bit 读取，MSB first，自动处理 byte stuffing 与 marker 边界。
class BitReader {
 public:
    // data 指向熵编码数据的起点（SOS 段之后的第一个字节）；len 是可读长度。
    BitReader(const uint8_t* data, size_t len) : data_(data), len_(len) {}

    // 读 1 位。正常返回 0/1。若碰到 marker 或数据耗尽，置 hit_marker_/eof_
    // 并返回 0（调用方应先看 AtMarker()/Eof() 决定是否继续）。
    int ReadBit();

    // 读 nbits 位，拼成一个整数（高位先读）。nbits 0..24。
    uint32_t ReadBits(int nbits);

    // 是否已经撞上一个 marker（0xFF 后跟非 0x00 的字节）。撞上后应停止熵解码，
    // 由上层去处理这个 marker（重启标记要复位 DC 预测值，EOI 表示扫描结束）。
    bool AtMarker() const { return hit_marker_; }

    // 撞上的 marker 的第二字节（如 0xD0..0xD7 是 RSTn，0xD9 是 EOI）。
    uint8_t MarkerByte() const { return marker_byte_; }

    // 数据是否读完（没有更多字节，也没有 marker）。
    bool Eof() const { return eof_; }

    // 已消费到的字节偏移（相对 data_ 起点），供调试。
    size_t BytePos() const { return byte_pos_; }

 private:
    // 从底层字节流取下一个"熵数据字节"，处理 0xFF00 还原与 marker 检测。
    // 成功返回 true 并把字节放进 next_byte_；遇 marker/EOF 返回 false。
    bool FillByte();

    const uint8_t* data_;
    size_t len_;
    size_t byte_pos_ = 0;    // 下一个要读的底层字节下标
    uint32_t cur_ = 0;       // 当前正在逐位吐出的字节
    int cur_bits_ = 0;       // cur_ 里还剩几位没吐
    bool hit_marker_ = false;
    bool eof_ = false;
    uint8_t marker_byte_ = 0;
};

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_COMMON_BITREADER_H
