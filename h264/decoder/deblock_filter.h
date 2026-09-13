// deblock_filter.h — H.264 去块滤波(Deblocking Filter)：在解码环路内(in-loop)
//                    把宏块边界上的"块效应"(blocking artifact)抹平。
//
// 上一篇把帧间预测/运动补偿解完了：P 帧的每个块从参考帧搬一块像素、补上残差，
// 再加上帧内预测的 I 块——到这一步，一帧的所有宏块都各自重建好了。可问题是：
// 每个 4x4 / 16x16 块是"各画各的"，块内部平滑，块与块的交界处却对不齐，
// 出现一条条台阶状的假边界。这就是块效应。低码率下尤其明显，画面像贴了瓷砖。
//
// 去块滤波干的事：沿每条块边界，对跨边界的一小排像素做加权平滑，把台阶磨平。
// 但它不能见边就磨——真实图像里的物体边缘(比如黑白分界)也是"大像素差",
// 那是该保留的细节，磨了就糊。所以核心是"区分假边界和真边界"：
//   1) 边界强度 bS(Boundary Strength，spec 8.7.2.1)：看边界两侧块的性质
//      (是不是帧内块、有没有非零残差、运动矢量差多大)，给出 0..4 的强度。
//      bS=4 最强(帧内块的宏块边界)，bS=0 完全不滤。
//   2) 阈值 α、β(spec Table 8-14，由量化参数 QP 查表)：决定"多大的像素差
//      才算真边缘(不滤)、多小才算块效应(要滤)"。QP 越大(压得越狠、块效应越重)，
//      α/β 越大，滤得越积极。
//   3) 滤波(spec 8.7.2.3 / 8.7.2.4)：bS<4 用弱滤波(最多动边界两侧各 2 个像素),
//      bS=4 用强滤波(最多动各 3 个像素)。都带 Clip 限幅，防止改过头。
//
// 揭示性：去块滤波必须在环路内(in-loop)。滤波后的帧不只用于显示，还要当作
// 后续 P 帧运动补偿的参考帧。若参考的是带块效应的帧，误差会顺着预测链一直传下去。
// 这与 JPEG 那种"只在输出时后处理"根本不同。
//
// 本文件只实现单条边界上、一排 8 个跨界像素 (p3 p2 p1 p0 | q0 q1 q2 q3) 的处理，
// 以及 bS/阈值的查表。整帧遍历所有边界的调度逻辑放在 demo 里演示。
#ifndef CODEC_FROM_SCRATCH_H264_DEBLOCK_FILTER_H
#define CODEC_FROM_SCRATCH_H264_DEBLOCK_FILTER_H

#include <array>
#include <cstdint>

namespace cfs {

// 一条边界上、跨界的一排像素。约定索引：
//   p3 p2 p1 p0 | q0 q1 q2 q3
// 竖直边界时是"一行"，水平边界时是"一列"。p0/q0 紧贴边界。
struct EdgeSamples {
    std::array<int, 4> p{};  // p[0]=p0 紧贴边界, p[3]=p3 最远
    std::array<int, 4> q{};  // q[0]=q0 紧贴边界, q[3]=q3 最远
};

// 边界强度 bS 的输入条件（把 spec 8.7.2.1 里那一长串判据浓缩成解码器实际
// 会用到的几项；本系列只做 frame 图、非 MBAFF、P/I slice，故略去场/SP/SI 分支）。
struct BsInputs {
    bool edge_is_mb_edge = false;  // 该 4x4 边界是否同时是宏块(16x16)边界
    bool p_intra = false;          // p 侧块是否帧内预测
    bool q_intra = false;          // q 侧块是否帧内预测
    bool p_has_coeff = false;      // p 侧 4x4 块是否含非零残差系数
    bool q_has_coeff = false;      // q 侧 4x4 块是否含非零残差系数
    int ref_diff = 0;              // 两侧参考帧是否不同(0=相同,1=不同)
    int mv_diff = 0;               // 两侧 MV 分量最大绝对差(1/4 像素单位)
};

// 边界强度 bS 推导(spec 8.7.2.1)。返回 0..4。
int DeriveBs(const BsInputs& in);

// 由平均量化参数查 α 阈值(spec Table 8-14)。indexA = Clip3(0,51, qP+offsetA)。
int AlphaFromIndexA(int index_a);

// 由平均量化参数查 β 阈值(spec Table 8-14)。indexB = Clip3(0,51, qP+offsetB)。
int BetaFromIndexB(int index_b);

// 查 tC0(spec Table 8-15)，bS ∈ {1,2,3}，index_a ∈ 0..51。bS=4 不用此表。
int Tc0FromTable(int bS, int index_a);

// filterSamplesFlag(spec 式 8-333)：只有当边界确实像块效应时才滤。
// bS!=0 且 |p0-q0|<α 且 |p1-p0|<β 且 |q1-q0|<β。
bool FilterSamplesFlag(const EdgeSamples& s, int bS, int alpha, int beta);

// 对一排像素做去块滤波(spec 8.7.2.3 bS<4 / 8.7.2.4 bS=4)。
//   s        : 输入的 8 个跨界像素(会被就地修改为滤波后结果)。
//   bS       : 边界强度 0..4。
//   qp_av    : 平均量化参数 (qPp+qPq+1)>>1。
//   chroma   : 是否色度边界(色度只滤 p0/q0，且强滤波用弱公式)。
//   offsetA/B: 码流里的 slice 级滤波偏移(FilterOffsetA/B)，默认 0。
// 返回 true 表示实际发生了滤波(filterSamplesFlag==1)。
bool DeblockEdge(EdgeSamples& s, int bS, int qp_av, bool chroma,
                 int offsetA = 0, int offsetB = 0);

// Clip3(lo,hi,v)：把 v 钳到 [lo,hi]。
int Clip3(int lo, int hi, int v);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_H264_DEBLOCK_FILTER_H
