// cavlc_decode.h — H.264 CAVLC 残差系数解码（严格照 ITU-T H.264 9.2）。
//
// CAVLC（基于上下文的自适应变长编码，Context-Adaptive Variable-Length
// Coding）是 Baseline/Main profile 里解析残差系数的熵解码方式。它站在解码
// 流水线的这个位置：
//   码流比特 -> [CAVLC 解出量化系数] -> 反量化 -> 反变换 -> 残差
// 上一篇（整数反变换）拿到的“量化系数”，就是这一篇从比特流里解出来的。
//
// 为什么不直接把 16 个系数逐个写进码流？因为变换量化之后，一个 4x4 块的系数
// 有很强的统计规律：
//   - 非零系数很少（大量是 0），集中在低频（左上角）；
//   - 高频端常常是一串 ±1（叫 trailing ones，尾部 ±1）；
//   - 系数按 zigzag 展开后，末尾是一长串 0。
// CAVLC 就是为这个形状量身定做的：它不逐个记系数，而是拆成几个语法元素，各自
// 用变长码表编，把“稀疏 + 尾部多 ±1 + 多 0”榨到极致（spec 9.2）。
//
// 五个语法元素（解码顺序）：
//   1) coeff_token：一次同时给出 TotalCoeff（非零系数总数）和 TrailingOnes
//      （尾部 ±1 的个数，最多 3 个）。关键——用哪张码表，由左邻块和上邻块的
//      非零系数个数推出的 nC 决定，这就是“上下文自适应”（9.2.1）。
//   2) trailing_ones_sign_flag：每个尾部 ±1 的符号，1 位一个（9.2.2）。
//   3) level：其余非零系数的幅值，用 level_prefix + level_suffix 变长编码，
//      前缀长度会随已解出的幅值自适应变大（9.2.2）。
//   4) total_zeros：第一个非零系数之前、所有非零系数之间的 0 的总数（9.2.3）。
//   5) run_before：每个非零系数“前面”紧跟几个 0，把 total_zeros 分配到各处
//      （9.2.3）。
// 组合起来，就能把这些非零系数和 0 摆回 16 个位置（zigzag 顺序）。
//
// 本文件把 spec Table 9-5..9-10 的标准码表硬编码进来（这些表是标准的一部分，
// 不是我们发明的），并实现按 nC 选表的上下文逻辑。命名空间 cfs，C++17。
#ifndef CODEC_FROM_SCRATCH_H264_CAVLC_DECODE_H
#define CODEC_FROM_SCRATCH_H264_CAVLC_DECODE_H

#include <array>
#include <cstdint>

#include "rbsp_bit_reader.h"

namespace cfs {

// 一个 4x4 残差块解出的量化系数，按“扫描顺序”排列（coeff[0] 是第一个扫描位置
// = DC，coeff[15] 是最后一个高频位置）。要摆回二维块还需 zigzag 反扫描。
using Cavlc4x4 = std::array<int, 16>;

// coeff_token 解码结果：一次给出两个数。
struct CoeffToken {
    int total_coeff = 0;    // TotalCoeff：非零系数个数（0..16）
    int trailing_ones = 0;  // TrailingOnes：尾部 ±1 的个数（0..3）
};

// 4x4 块 CAVLC 解码的完整结果（供演示/配图打印各中间量）。
struct CavlcResult {
    CoeffToken token;                    // coeff_token 解出的两个数
    int nc = 0;                          // 本块选表用的上下文 nC
    std::array<int, 16> levels{};        // 各非零系数的带符号幅值（高频->低频顺序）
    int total_zeros = 0;                 // total_zeros
    std::array<int, 16> runs{};          // 各非零系数前的 run_before
    Cavlc4x4 coeff{};                    // 还原出的 16 个系数（扫描顺序）
    size_t bits_used = 0;                // 本块一共消耗多少比特
};

// 根据上下文 nC 选表，解 coeff_token（spec 9.2.1，Table 9-5）。
//   nC 的分档：0<=nC<2 用第 0 档，2<=nC<4 用第 1 档，4<=nC<8 用第 2 档，
//   nC>=8 用定长 6 位码（第 3 档）。nC==-1（chroma DC）走另一张小表，本实现
//   聚焦亮度 4x4，nC 取 0..16 的整数。
CoeffToken DecodeCoeffToken(RbspBitReader& br, int nc);

// 由左邻/上邻块的非零系数个数推出本块的 nC（spec 9.2.1 式 9-4）。
//   两个邻居都可用：nC = (nA + nB + 1) >> 1；
//   只有一个可用：nC = 那一个；两个都不可用：nC = 0。
// available_a / available_b 表示左邻/上邻块是否在同一 slice 内可用。
int DeriveNc(int nnz_left, bool available_a, int nnz_top, bool available_b);

// 解一个 4x4 亮度块的全部残差系数（spec 9.2 完整流程）。
//   br  ：定位到该块 residual 起点的比特读取器；
//   nc  ：本块的上下文（由 DeriveNc 得到）；
//   max_num_coeff：本块最多多少个系数（亮度 4x4 为 16）。
// 返回解码结果，coeff 已按扫描顺序填好（未做 zigzag 反扫描）。
CavlcResult DecodeResidual4x4(RbspBitReader& br, int nc, int max_num_coeff = 16);

// 把扫描顺序的 16 个系数按 zigzag 反扫描摆回 4x4 二维块（spec 8.5.6 表 8-13）。
//   out[y][x] = coeff[ scan_index_of(y,x) ]。
std::array<std::array<int, 4>, 4> InverseZigzag4x4(const Cavlc4x4& coeff);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_H264_CAVLC_DECODE_H
