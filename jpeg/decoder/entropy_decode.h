// entropy_decode.h — 熵解码：把 JFIF 那段变长比特流还原成一个块的 64 个系数
// （JPEG 解码链条的中段，A4「Zig-Zag + 游程 + 霍夫曼编码」的逆过程）。
//
// 编码（A4）是"已知长度往里塞"：先算出 DC 差分的 category、AC 的 (run,size)，
// 查表拿到定长的霍夫曼码字，直接写进去。长度全程已知，写得干脆。
//
// 解码难就难在这里：霍夫曼码是变长的、无分隔符的，你事先不知道下一个码字有几位。
// 只能一位一位读，边读边在表里试探，直到凑出一个合法码字（T.81 F.2.2.3 DECODE）。
// 解出符号后才知道后面还要跟几位幅值。整个过程是"读一点、才知道下一步读多少"。
//
// 一个块的解码顺序（T.81 F.2.1.2 / F.2.2）：
//   1. DC：解一个符号 = category(SSSS) -> 再读 SSSS 位幅值 -> 得到 DC 差分,
//      加上前一块的 DC 预测值，还原本块真实 DC（差分是 A4 的 DPCM 的逆）。
//   2. AC：循环解符号 = (run<<4 | size)。run 个 0 之后读 size 位幅值填一个系数;
//      遇 EOB(0x00) 表示剩下全是 0，提前结束；遇 ZRL(0xF0) 填 16 个 0 继续。
//
// 幅值解码的陷阱（T.81 F.2.2.1 EXTEND）：读出的 size 位是"有符号数的紧凑表示"，
// 最高位为 1 是正数、为 0 是负数。负数要按 value = bits - (1<<size) + 1 还原，
// 否则解出的系数正负全反。这是本篇最容易翻车的一处，见 DecodeMagnitude。
#ifndef CODEC_FROM_SCRATCH_JPEG_DECODER_ENTROPY_DECODE_H
#define CODEC_FROM_SCRATCH_JPEG_DECODER_ENTROPY_DECODE_H

#include <array>
#include <cstdint>

#include "bitreader.h"
#include "huffman_decode.h"

namespace cfs {

// 把从比特流读出的 size 位幅值还原成有符号系数（T.81 F.2.2.1 的 EXTEND）。
// bits 是刚读进来的 size 位无符号值。规则：
//   最高位为 1（bits >= 2^(size-1)）：正数，直接是 bits。
//   最高位为 0（bits <  2^(size-1)）：负数，value = bits - 2^size + 1。
// size==0 时系数就是 0。
int DecodeMagnitude(uint32_t bits, int size);

// 解一个块的所有系数，按 Zig-Zag 一维顺序写入 out[0..63]（out[0] 是 DC）。
//   br        ：定位在本块熵数据处的读取器（跨块连续读）。
//   dc_tbl    ：本分量的 DC 霍夫曼解码表。
//   ac_tbl    ：本分量的 AC 霍夫曼解码表。
//   dc_pred   ：入参为前一块的 DC 预测值，出参更新为本块 DC（差分链）。
// 返回 true 表示成功解出一个完整块；遇码流损坏/marker/EOF 返回 false。
bool DecodeBlockZigZag(BitReader& br, const HuffDecTable& dc_tbl,
                       const HuffDecTable& ac_tbl, int& dc_pred,
                       std::array<int, 64>& out);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_JPEG_DECODER_ENTROPY_DECODE_H
