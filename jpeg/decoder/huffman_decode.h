// huffman_decode.h — 从 DHT 段的 BITS/HUFFVAL 重建霍夫曼"解码"表（JPEG 解码
// 的关键一步）。
//
// 编码时（A4）我们从 BITS/HUFFVAL 建的是"符号→码字"表（给定符号查它的码字）。
// 解码要反过来：给定一串 bit，判断它凑成了哪个符号。这就是解码表要解决的问题。
//
// 难点在于霍夫曼码是变长的、无分隔符的。解码端只能一位一位读，每读一位就问：
// "我手里这串 bit，是不是恰好等于某个码长为 L 的合法码字？"要能高效回答，得先
// 把 BITS/HUFFVAL 展开成一张按码长组织的查找结构。
//
// 我们用 T.81 Annex F.2.2.3 描述的经典办法：为每个码长 L 预计算三个数组——
//   mincode[L]：码长 L 的最小码字
//   maxcode[L]：码长 L 的最大码字（没有该码长时置 -1）
//   valptr[L] ：码长 L 的第一个符号在 HUFFVAL 里的下标
// 解码时把已读的 code 和 maxcode[L] 比较，命中就用 valptr 反查符号。构码规则
// 与编码端 Annex C 完全一致（同长度内 code 递增、换长度左移），保证编解码自洽。
#ifndef CODEC_FROM_SCRATCH_JPEG_DECODER_HUFFMAN_DECODE_H
#define CODEC_FROM_SCRATCH_JPEG_DECODER_HUFFMAN_DECODE_H

#include <array>
#include <cstdint>
#include <vector>

#include "bitreader.h"

namespace cfs {

// 一张霍夫曼解码表（T.81 Annex F.2.2.3 的 MINCODE/MAXCODE/VALPTR 结构）。
// 下标用 1..16 表示码长，索引 0 不用（保持和标准里码长从 1 起算一致）。
struct HuffDecTable {
    std::array<int32_t, 17> mincode{};   // 各码长的最小码字
    std::array<int32_t, 17> maxcode{};   // 各码长的最大码字，无则 -1
    std::array<int32_t, 17> valptr{};    // 各码长首符号在 huffval 的下标
    std::vector<uint8_t> huffval;        // 原始 HUFFVAL 符号表（按码长升序）
};

// 由 BITS/HUFFVAL 生成解码表。
// bits[i] = 码长为 (i+1) 的符号个数；huffval = 按码长升序排列的符号值。
// 生成 MINCODE/MAXCODE/VALPTR 的过程照 T.81 Annex C（HUFFSIZE/HUFFCODE 生成）
// 与 F.2.2.3（Decoder_tables 流程）。
HuffDecTable BuildDecTable(const std::array<uint8_t, 16>& bits,
                           const std::vector<uint8_t>& huffval);

// 从比特流里解出一个符号（T.81 F.2.2.3 的 DECODE 过程）。
// 逐位读入拼成 code，每读一位就和当前码长的 maxcode 比较；命中即用 valptr
// 反查 huffval 得到符号。返回解出的 8-bit 符号；若读到 marker/EOF 前无法凑成
// 合法码字，通过 ok 返回 false（真实码流有损时要能报错而非死循环）。
uint8_t DecodeSymbol(BitReader& br, const HuffDecTable& t, bool* ok);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_JPEG_DECODER_HUFFMAN_DECODE_H
