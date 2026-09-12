// bitwriter.h — 按位写入器，熵编码阶段用来把变长的霍夫曼码和幅值 bit
// 一位一位地拼进字节流（JPEG 熵编码是逐 bit 的，不是逐字节）。
// codec-from-scratch / common
//
// JPEG 的比特序：高位先写（MSB first），见 ITU-T T.81 F.1.2.3。
// 注意：真正的 JFIF 里遇到 0xFF 字节要塞一个 0x00（byte stuffing，见 F.1.2.3），
// 这一步留到 A5 写文件结构时再处理。这里只负责纯 bit 流，方便统计比特数。
#ifndef CODEC_FROM_SCRATCH_COMMON_BITWRITER_H
#define CODEC_FROM_SCRATCH_COMMON_BITWRITER_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cfs {

// 把 bit 逐位塞进内部缓冲，MSB first。
class BitWriter {
 public:
    // 写入 value 的低 nbits 位（nbits 0..24），高位先出。
    void WriteBits(uint32_t value, int nbits);

    // 已写入的总 bit 数（未对齐到字节）。
    size_t BitCount() const { return bit_count_; }

    // 把缓冲按字节取出，最后不足一字节的部分用 1 填充（T.81 F.1.2.3 规定
    // 结尾用 1 补齐）。会 flush 内部状态。
    std::vector<uint8_t> TakeBytes();

 private:
    std::vector<uint8_t> bytes_;
    uint32_t acc_ = 0;   // 尚未凑满 8 位的累加器（左对齐在低位）
    int acc_bits_ = 0;   // acc_ 里有效 bit 数
    size_t bit_count_ = 0;
};

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_COMMON_BITWRITER_H
