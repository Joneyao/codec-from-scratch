// intra_mode_decision.h — H.264 编码器的帧内预测「模式决策」。
//
// 这是编码器和解码器的分水岭。解码器拿到码流里写死的模式号，直接照着
// 8.3.1 / 8.3.3 的公式算预测块，它「不做选择」。编码器不一样：模式号是它
// 自己定的——对每个 4x4 / 16x16 亮度块，把候选模式挨个试一遍，各自算出预测
// 块与原始块的「代价」，挑代价最小的那个写进码流。
//
// ── spec 依据 ──
//   预测模式的定义、编号、公式：ITU-T H.264
//     · Intra_4x4   9 种模式  见 8.3.1（表 8-2）
//     · Intra_16x16 4 种模式  见 8.3.3 / 8.3.2（表 8-3）
//   本模块直接复用 decoder/intra_predict.{h,cpp} 里已按 spec 实现的预测函数
//   （PredictIntra4x4 / PredictIntra16x16），不重复造预测轮子。
//
// ── 标准不规定「怎么选」──
//   H.264 规范只定义了「给定模式号该算出什么预测块」（规范化解码过程），
//   至于编码器该挑哪个模式，spec 一个字都没写。用 SAD、用 SATD、还是用带
//   码率的完整 RDO（率失真优化），全凭编码器自己。这正是不同 H.264 编码器
//   压缩效率拉开差距的地方之一。本模块给出两把最常见的「尺子」：
//     · SAD  = Σ|original − predicted|              —— 最省算力，x264 的 me=dia 快挡也用它
//     · SATD = Σ|Hadamard(original − predicted)|    —— 在变换域上量残差，和真正编码后的
//                                                      比特数更相关，选出的模式通常更优
//   本模块只做「简化版 RDO」：只比预测残差的代价，不含真正的量化+熵编码比特估计。
//   完整 RDO 是后续文章的事。
#ifndef CODEC_FROM_SCRATCH_H264_ENCODER_INTRA_MODE_DECISION_H
#define CODEC_FROM_SCRATCH_H264_ENCODER_INTRA_MODE_DECISION_H

#include "intra_predict.h"  // decoder 里的 Neighbors*/Block*/PredictIntra*/枚举

namespace cfs {

// 代价函数的类型：决定用哪把「尺子」量残差。
enum class CostMetric {
    kSad,   // 绝对差之和：Σ|orig − pred|
    kSatd,  // Hadamard 变换后再求绝对值和：更贴近编码后的比特数
};

// ── 代价函数（残差度量）──

// SAD：4x4 块预测残差的绝对差之和。等价于 decoder 里的 Sad4x4，这里为对称
// 起见另给一个 Cost 命名，并补上 16x16 的版本。
int Sad4x4Cost(const Block4x4& original, const Block4x4& predicted);
int Sad16x16Cost(const Block16x16& original, const Block16x16& predicted);

// SATD：对残差(orig − pred)做 4x4 Hadamard 变换，再对所有变换系数求绝对值
// 和。16x16 的 SATD 按 4x4 分块分别做 Hadamard 再累加（x264 的常规做法）。
// SATD 在变换域衡量残差能量的分布，比 SAD 更能预示「变换量化+熵编码之后要花
// 多少比特」，因此模式决策更准。
int Satd4x4Cost(const Block4x4& original, const Block4x4& predicted);
int Satd16x16Cost(const Block16x16& original, const Block16x16& predicted);

// 按给定度量算一个 4x4 / 16x16 块的代价（预测块 vs 原始块）。
int Cost4x4(const Block4x4& original, const Block4x4& predicted,
            CostMetric metric);
int Cost16x16(const Block16x16& original, const Block16x16& predicted,
              CostMetric metric);

// ── 模式决策 ──

// 试遍 Intra_4x4 的 9 种模式，返回代价最小的模式号(0..8)。
//   nb        邻居像素（上邻/左邻/左上角），来自已重建的相邻块
//   original  当前 4x4 原始亮度块
//   metric    用 SAD 还是 SATD 作为选择尺子
//   out_cost  非空时写回最优模式的代价值
// 平手时取模式号较小者（与 x264 一致，也便于测试可预测）。
int ChooseIntra4x4Mode(const Neighbors4x4& nb, const Block4x4& original,
                       CostMetric metric = CostMetric::kSad,
                       int* out_cost = nullptr);

// 试遍 Intra_16x16 的 4 种模式，返回代价最小的模式（enum）。
//   out_cost 非空时写回最优模式的代价值。平手时取枚举值较小者。
Intra16x16Mode ChooseIntra16x16Mode(const Neighbors16x16& nb,
                                    const Block16x16& original,
                                    CostMetric metric = CostMetric::kSad,
                                    int* out_cost = nullptr);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_H264_ENCODER_INTRA_MODE_DECISION_H
