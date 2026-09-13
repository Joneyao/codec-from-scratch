// deblock_filter.cpp — 去块滤波实现，严格照 ITU-T H.264 8.7。
#include "deblock_filter.h"

#include <algorithm>
#include <cstdlib>

namespace cfs {

int Clip3(int lo, int hi, int v) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

namespace {

inline int Clip1(int v) { return Clip3(0, 255, v); }
inline int AbsI(int v) { return v < 0 ? -v : v; }

// spec Table 8-14：indexA/indexB(0..51) -> α / β。
// 前 16 项(indexA<16)α、β 均为 0，即不滤波区。
constexpr int kAlpha[52] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   4,   4,   5,   6,   7,   8,   9,   10,  12,  13,
    15,  17,  20,  22,  25,  28,  32,  36,  40,  45,  50,  56,  63,
    71,  80,  90,  101, 113, 127, 144, 162, 182, 203, 226, 255, 255};

constexpr int kBeta[52] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  2,  2,
    2, 3, 3, 3, 3, 4, 4, 4, 6, 6,  7,  7,  8,  8,  9,  9,  10, 10,
    11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18};

// spec Table 8-15：tC0[bS-1][indexA]，bS ∈ {1,2,3}。
constexpr int kTc0[3][52] = {
    // bS = 1
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2,
     2, 3, 3, 3, 4, 4, 4, 5, 6, 6, 7, 8, 9, 10, 11, 13},
    // bS = 2
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3,
     3, 3, 4, 4, 5, 5, 6, 7, 8, 8, 10, 11, 12, 13, 15, 17},
    // bS = 3
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
     1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4,
     4, 5, 6, 6, 7, 8, 9, 10, 11, 13, 14, 16, 18, 20, 23, 25}};

}  // namespace

int AlphaFromIndexA(int index_a) { return kAlpha[Clip3(0, 51, index_a)]; }
int BetaFromIndexB(int index_b) { return kBeta[Clip3(0, 51, index_b)]; }

int Tc0FromTable(int bS, int index_a) {
    if (bS < 1 || bS > 3) return 0;
    return kTc0[bS - 1][Clip3(0, 51, index_a)];
}

int DeriveBs(const BsInputs& in) {
    // spec 8.7.2.1（frame、非 MBAFF、P/I slice 精简分支）。
    // bS=4：块边界同时是宏块边界，且两侧任一为帧内块。
    if (in.edge_is_mb_edge && (in.p_intra || in.q_intra)) return 4;
    // bS=3：非宏块边界但两侧任一为帧内块。
    if (in.p_intra || in.q_intra) return 3;
    // bS=2：两侧任一 4x4 块含非零残差系数。
    if (in.p_has_coeff || in.q_has_coeff) return 2;
    // bS=1：参考帧不同，或 MV 分量差 >= 4（1/4 像素单位，即 >=1 整像素）。
    if (in.ref_diff != 0 || in.mv_diff >= 4) return 1;
    // 否则不滤。
    return 0;
}

bool FilterSamplesFlag(const EdgeSamples& s, int bS, int alpha, int beta) {
    // spec 式 8-333：只有跨界差落在阈值内(像块效应)才滤。
    return bS != 0 && AbsI(s.p[0] - s.q[0]) < alpha &&
           AbsI(s.p[1] - s.p[0]) < beta && AbsI(s.q[1] - s.q[0]) < beta;
}

namespace {

// bS<4 的弱滤波(spec 8.7.2.3)。就地改 p0,p1,q0,q1（p2/q2 保持不变）。
void FilterWeak(EdgeSamples& s, int bS, int index_a, int beta, bool chroma) {
    const int p0 = s.p[0], p1 = s.p[1], p2 = s.p[2];
    const int q0 = s.q[0], q1 = s.q[1], q2 = s.q[2];
    const int tc0 = Tc0FromTable(bS, index_a);

    // ap=|p2-p0|, aq=|q2-q0|（spec 式 8-339/8-340）。
    const int ap = AbsI(p2 - p0);
    const int aq = AbsI(q2 - q0);

    // tC（spec 式 8-337 亮度 / 8-338 色度）。
    int tc;
    if (!chroma)
        tc = tc0 + (ap < beta ? 1 : 0) + (aq < beta ? 1 : 0);
    else
        tc = tc0 + 1;

    // Δ（spec 式 8-334..8-336）：改 p0/q0。
    int delta = Clip3(-tc, tc, (((q0 - p0) << 2) + (p1 - q1) + 4) >> 3);
    s.p[0] = Clip1(p0 + delta);
    s.q[0] = Clip1(q0 - delta);

    // p1（spec 式 8-341）：仅亮度且 ap<β 时才改。
    if (!chroma && ap < beta) {
        int dp = Clip3(-tc0, tc0, (p2 + ((p0 + q0 + 1) >> 1) - (p1 << 1)) >> 1);
        s.p[1] = p1 + dp;
    }
    // q1（spec 式 8-343）：仅亮度且 aq<β 时才改。
    if (!chroma && aq < beta) {
        int dq = Clip3(-tc0, tc0, (q2 + ((p0 + q0 + 1) >> 1) - (q1 << 1)) >> 1);
        s.q[1] = q1 + dq;
    }
    // p2/q2 保持不变（spec 式 8-345/8-346）。
}

// bS=4 的强滤波(spec 8.7.2.4)。就地改 p0,p1,p2,q0,q1,q2。
void FilterStrong(EdgeSamples& s, int alpha, int beta, bool chroma) {
    const int p0 = s.p[0], p1 = s.p[1], p2 = s.p[2], p3 = s.p[3];
    const int q0 = s.q[0], q1 = s.q[1], q2 = s.q[2], q3 = s.q[3];
    const int ap = AbsI(p2 - p0);
    const int aq = AbsI(q2 - q0);
    const bool strong_cond = AbsI(p0 - q0) < ((alpha >> 2) + 2);

    // p 侧（spec 式 8-347..8-353）。
    if (!chroma && ap < beta && strong_cond) {
        s.p[0] = (p2 + 2 * p1 + 2 * p0 + 2 * q0 + q1 + 4) >> 3;   // 8-348
        s.p[1] = (p2 + p1 + p0 + q0 + 2) >> 2;                    // 8-349
        s.p[2] = (2 * p3 + 3 * p2 + p1 + p0 + q0 + 4) >> 3;       // 8-350
    } else {
        s.p[0] = (2 * p1 + p0 + q1 + 2) >> 2;                     // 8-351
        // p1/p2 不变（8-352/8-353）。
    }
    // q 侧（spec 式 8-354..8-360）。
    if (!chroma && aq < beta && strong_cond) {
        s.q[0] = (q2 + 2 * q1 + 2 * q0 + 2 * p0 + p1 + 4) >> 3;   // 8-355
        s.q[1] = (q2 + q1 + q0 + p0 + 2) >> 2;                    // 8-356
        s.q[2] = (2 * q3 + 3 * q2 + q1 + q0 + p0 + 4) >> 3;       // 8-357
    } else {
        s.q[0] = (2 * q1 + q0 + p1 + 2) >> 2;                     // 8-358
        // q1/q2 不变（8-359/8-360）。
    }
}

}  // namespace

bool DeblockEdge(EdgeSamples& s, int bS, int qp_av, bool chroma, int offsetA,
                 int offsetB) {
    if (bS == 0) return false;  // bS=0：整条边界不滤。

    // spec 式 8-331/8-332：indexA/indexB 由平均 QP 加 slice 偏移查表。
    const int index_a = Clip3(0, 51, qp_av + offsetA);
    const int index_b = Clip3(0, 51, qp_av + offsetB);
    const int alpha = kAlpha[index_a];
    const int beta = kBeta[index_b];

    // filterSamplesFlag：像真边缘就不滤（保边），像块效应才滤（平滑）。
    if (!FilterSamplesFlag(s, bS, alpha, beta)) return false;

    if (bS < 4)
        FilterWeak(s, bS, index_a, beta, chroma);
    else
        FilterStrong(s, alpha, beta, chroma);
    return true;
}

}  // namespace cfs
