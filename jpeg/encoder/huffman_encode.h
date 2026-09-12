// huffman_encode.h — 标准霍夫曼编码（JPEG 熵编码后半段，真正把体积压下去的一步）
//
// 前面的 Zig-Zag + 游程编码只是把系数整理成 (run, value) 序列，还没省一个 bit。
// 这一步才动真格：把每个符号换成变长的霍夫曼码——出现频繁的符号给短码，
// 罕见的给长码，整体 bit 数就塌下来了。而且它是无损的：解码端能一位不差还原。
//
// 用的是 T.81 Annex K 给出的"典型表"（Table K.3 亮度 DC、Table K.5 亮度 AC）。
// 这些表是从大量 8-bit 图像统计出来的，绝大多数基线 JPEG 直接沿用。
//
// 编码格式（T.81 F.1.2）：
//   DC：先算差分 diff 的 category(SSSS，需要几个 bit) -> 查 DC 霍夫曼表得码字，
//       再直接跟上 SSSS 位的 diff 幅值编码。
//   AC：把 (run, size) 拼成一个字节 (run<<4 | size) 作为符号 -> 查 AC 霍夫曼表，
//       再跟上 size 位的 value 幅值编码。EOB=0x00，ZRL=0xF0。
#ifndef CODEC_FROM_SCRATCH_JPEG_HUFFMAN_ENCODE_H
#define CODEC_FROM_SCRATCH_JPEG_HUFFMAN_ENCODE_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "bitwriter.h"
#include "zigzag_rle.h"

namespace cfs {

// 一张霍夫曼表：给每个 8-bit 符号一个 (码字, 码长)。code_len==0 表示该符号未定义。
struct HuffTable {
    std::array<uint32_t, 256> code{};  // 码字（右对齐，取低 len 位）
    std::array<uint8_t, 256> len{};    // 码长（bit 数），0 表示无此符号
};

// 由 T.81 规定的 BITS/HUFFVAL 两段式描述生成霍夫曼表（构码算法见 Annex C）。
// bits[i] = 码长为 (i+1) 的符号个数；huffval = 按码长升序排列的符号值列表。
HuffTable BuildHuffTable(const std::array<uint8_t, 16>& bits,
                         const std::vector<uint8_t>& huffval);

// Annex K 的标准亮度表（构建一次即可复用）。
// 亮度：Table K.3 (DC) / Table K.5 (AC)。
const HuffTable& StdLumaDcTable();
const HuffTable& StdLumaAcTable();

// 色度表：Table K.4 (DC) / Table K.6 (AC)。Cb/Cr 共用这一组。
// A5 组装完整 JFIF 时，色度分量的 DC/AC 熵编码要用这两张表。
const HuffTable& StdChromaDcTable();
const HuffTable& StdChromaAcTable();

// 把标准表的 BITS / HUFFVAL 原始描述暴露出来，供 A5 写 DHT 段时直接落盘。
// 返回的 pair.first = 16 个 BITS（码长 1..16 各有几个符号），
// pair.second = 按码长升序排列的 HUFFVAL 符号列表。
const std::array<uint8_t, 16>& StdLumaDcBits();
const std::vector<uint8_t>& StdLumaDcVals();
const std::array<uint8_t, 16>& StdLumaAcBits();
const std::vector<uint8_t>& StdLumaAcVals();
const std::array<uint8_t, 16>& StdChromaDcBits();
const std::vector<uint8_t>& StdChromaDcVals();
const std::array<uint8_t, 16>& StdChromaAcBits();
const std::vector<uint8_t>& StdChromaAcVals();

// 计算一个（有符号）系数的 category（SSSS）：表示它需要几个 bit 才能编码幅值。
// 定义见 T.81 Table F.1 / F.2：0->0, ±1->1, ±2..3->2, ±4..7->3 ... 依此类推。
int Category(int value);

// 计算一个系数在 SSSS 位下的"幅值编码"：正数取本身低 size 位；
// 负数取 (value-1) 的低 size 位（等价 value + (2^size - 1)），见 T.81 F.1.2.1。
uint32_t MagnitudeBits(int value, int size);

// 编码一个块：dc_diff 是该块 DC 与前块 DC 的差分；ac 是游程符号序列。
// 结果写入 bw。返回本块写入的 bit 数（供统计）。
size_t EncodeBlock(int dc_diff, const std::vector<RleSymbol>& ac,
                   const HuffTable& dc_table, const HuffTable& ac_table,
                   BitWriter& bw);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_JPEG_HUFFMAN_ENCODE_H
