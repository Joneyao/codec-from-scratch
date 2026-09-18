// cavlc_encode.h — H.264 CAVLC 残差系数正向熵编码（严格照 ITU-T H.264 9.2 反向）。
//
// 这是解码器 cavlc_decode.h 的镜像。解码器从比特流里解出 4x4 块的 16 个量化
// 系数；编码器反过来，把 16 个量化系数榨成最少的比特写进码流。编解码是一面
// 镜子——编码器选哪张 VLC 表（数邻居非零系数得到 nC）的逻辑，必须和解码器数
// 邻居的逻辑严丝合缝，否则解码器会用错表，整段码流解崩。
//
// 编码顺序（与解码顺序一致，只是方向相反）：
//   1) coeff_token：由 TotalCoeff（非零系数总数）与 TrailingOnes（尾部 ±1 的
//      个数，最多 3 个）反查 Table 9-5，用哪张表由上下文 nC 决定（9.2.1）。
//   2) trailing_ones_sign_flag：每个尾部 ±1 的符号，1 位（+1->0，-1->1）。
//   3) level：其余非零系数的幅值，反算 level_prefix + level_suffix，suffixLength
//      随已编幅值自适应增长（9.2.2）。
//   4) total_zeros：所有非零系数之前/之间的 0 总数，反查 Table 9-7/9-8（9.2.3）。
//   5) run_before：把 total_zeros 分配到各非零系数前，反查 Table 9-10（9.2.3）。
//
// 码表常量与解码侧同一张标准表（Table 9-5..9-10），本文件从 decoder 拷来复用。
// 命名空间 cfs，C++17。本实现聚焦亮度 4x4（nC 取 0..16 的整数）。
#ifndef CODEC_FROM_SCRATCH_H264_CAVLC_ENCODE_H
#define CODEC_FROM_SCRATCH_H264_CAVLC_ENCODE_H

#include <array>
#include <cstddef>

#include "../../common/bitwriter.h"
#include "../decoder/cavlc_decode.h"  // 复用 Cavlc4x4 类型定义

namespace cfs {

// 编码一个 4x4 块各语法元素消耗的比特数（供演示/配图，展示压缩来自哪里）。
struct CavlcEncodeStats {
    int total_coeff = 0;         // 统计出的 TotalCoeff
    int trailing_ones = 0;       // 统计出的 TrailingOnes
    int total_zeros = 0;         // 统计出的 total_zeros
    int nc = 0;                  // 选表用的上下文 nC
    size_t bits_coeff_token = 0; // coeff_token 花了多少比特
    size_t bits_sign = 0;        // trailing_ones 符号位
    size_t bits_level = 0;       // level（prefix+suffix）
    size_t bits_total_zeros = 0; // total_zeros
    size_t bits_run_before = 0;  // run_before
    size_t bits_total = 0;       // 本块总比特数
};

// 把扫描顺序的 16 个量化系数编成 CAVLC 比特写入 bw（spec 9.2 完整流程，反向）。
//   coeff_scan_order：按“扫描顺序”排列的 16 个量化系数（coeff[0]=DC=最低频，
//                     coeff[15]=最高频），与解码器 DecodeResidual4x4 输出同序。
//   nc：本块上下文（由 DeriveNc 得到，与解码侧同一逻辑）。
// 返回各语法元素的比特统计。编出的比特喂给 DecodeResidual4x4 必须原样解回。
CavlcEncodeStats EncodeResidual4x4(BitWriter& bw, const Cavlc4x4& coeff_scan_order,
                                   int nc);

// ForwardZigzag4x4：把 4x4 二维块按 zigzag 正向扫描成扫描顺序的 16 个系数。
//   coeff_scan[ scan_index_of(y,x) ] = block[y][x]。
// 它是解码侧 InverseZigzag4x4 的逆：ForwardZigzag(InverseZigzag(c)) == c。
Cavlc4x4 ForwardZigzag4x4(const std::array<std::array<int, 4>, 4>& block);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_H264_CAVLC_ENCODE_H
