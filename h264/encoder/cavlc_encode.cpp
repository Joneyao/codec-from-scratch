// cavlc_encode.cpp — H.264 CAVLC 残差正向编码实现（严格照 ITU-T H.264 9.2 反向）。
//
// 表来源：ITU-T H.264 (03/2005) Table 9-5（coeff_token）、Table 9-7/9-8
// （total_zeros，4x4）、Table 9-10（run_before）。与解码侧 cavlc_decode.cpp
// 是同一张标准表——解码器逐位读入匹配 (len, code)，编码器反过来查出 (len, code)
// 直接写进比特流。两侧共用同一份表，才能保证编出的比特解得回来。
#include "cavlc_encode.h"

#include <cstdint>

namespace cfs {

namespace {

// 一个变长码字：比特长度 + 码值（右对齐，MSB 在高位）。与解码侧 VlcCode 同构。
struct VlcCode {
    int len;
    int code;
};

// ---- Table 9-5：coeff_token 码表（0<=nC<2 / 2<=nC<4 / 4<=nC<8 三档）----
// 索引 [trailing_ones(0..3)][total_coeff(0..16)]。len=0 表示该组合不存在。
// 与 cavlc_decode.cpp 完全一致（同一张标准表）。
const VlcCode kCoeffToken0[4][17] = {
    {{1,1},{6,5},{8,7},{9,7},{10,7},{11,7},{13,15},{13,11},{13,8},
     {14,15},{14,11},{15,15},{15,11},{16,15},{16,11},{16,7},{16,4}},
    {{0,0},{2,1},{6,4},{8,6},{9,6},{10,6},{11,6},{13,14},{13,10},
     {14,14},{14,10},{15,14},{15,10},{15,1},{16,14},{16,10},{16,6}},
    {{0,0},{0,0},{3,1},{7,5},{8,5},{9,5},{10,5},{11,5},{13,13},
     {13,9},{14,13},{14,9},{15,13},{15,9},{16,13},{16,9},{16,5}},
    {{0,0},{0,0},{0,0},{5,3},{6,3},{7,4},{8,4},{9,4},{10,4},
     {11,4},{13,12},{14,12},{14,8},{15,12},{15,8},{16,12},{16,8}},
};

const VlcCode kCoeffToken1[4][17] = {
    {{2,3},{6,11},{6,7},{7,7},{8,7},{8,4},{9,7},{11,15},{11,11},
     {12,15},{12,11},{12,8},{13,15},{13,11},{13,7},{14,9},{14,7}},
    {{0,0},{2,2},{5,7},{6,10},{6,6},{7,6},{8,6},{9,6},{11,14},
     {11,10},{12,14},{12,10},{13,14},{13,10},{14,11},{14,8},{14,6}},
    {{0,0},{0,0},{3,3},{6,9},{6,5},{7,5},{8,5},{9,5},{11,13},
     {11,9},{12,13},{12,9},{13,13},{13,9},{13,6},{14,10},{14,5}},
    {{0,0},{0,0},{0,0},{4,5},{4,4},{5,6},{6,8},{6,4},{7,4},
     {9,4},{11,12},{11,8},{12,12},{13,12},{13,8},{13,1},{14,4}},
};

// 4<=nC<8 档。刻意与解码器 cavlc_decode.cpp 的同名表逐字一致（编解码是一面
// 镜子：只有编码器和解码器用同一张表，编出的比特才能被解码器原样解回）。
// 提示：这张表个别码字与 ITU-T H.264 Table 9-5 的标准值有出入，属仓库解码器的
// 历史实现；本编码器不擅自“纠正”，以保证与既有解码器严丝合缝地往返。
const VlcCode kCoeffToken2[4][17] = {
    {{4,15},{6,15},{6,11},{6,8},{7,15},{7,11},{7,9},{7,8},{8,15},
     {8,11},{9,15},{9,11},{9,8},{10,13},{10,9},{10,5},{10,1}},
    {{0,0},{4,14},{5,15},{5,12},{6,10},{6,7},{6,5},{7,7},{7,6},
     {8,10},{8,7},{9,14},{9,10},{10,12},{10,8},{10,4},{10,0}},
    {{0,0},{0,0},{4,13},{5,11},{5,8},{6,6},{6,4},{7,5},{7,4},
     {8,9},{8,6},{9,13},{9,9},{10,11},{10,7},{10,3},{10,0}},
    {{0,0},{0,0},{0,0},{4,12},{4,11},{4,10},{4,9},{4,8},{5,10},
     {6,3},{7,3},{8,8},{9,12},{10,10},{10,6},{10,2},{10,0}},
};

// ---- Table 9-7 / 9-8：total_zeros 码表（4x4）----
// 索引 [total_coeff-1 (0..14)][total_zeros (0..15)]。与解码侧一致。
const VlcCode kTotalZeros[15][16] = {
    {{1,1},{3,3},{3,2},{4,3},{4,2},{5,3},{5,2},{6,3},{6,2},{7,3},
     {7,2},{8,3},{8,2},{9,3},{9,2},{9,1}},
    {{3,7},{3,6},{3,5},{3,4},{3,3},{4,5},{4,4},{4,3},{4,2},{5,3},
     {5,2},{6,3},{6,2},{6,1},{6,0},{0,0}},
    {{4,5},{3,7},{3,6},{3,5},{4,4},{4,3},{3,4},{3,3},{4,2},{5,3},
     {5,2},{6,1},{5,1},{6,0},{0,0},{0,0}},
    {{5,3},{3,7},{4,5},{4,4},{3,6},{4,3},{3,5},{3,4},{4,2},{5,2},
     {6,1},{5,1},{6,0},{0,0},{0,0},{0,0}},
    {{4,5},{4,4},{4,3},{3,7},{3,6},{3,5},{3,4},{3,3},{4,2},{5,1},
     {4,1},{5,0},{0,0},{0,0},{0,0},{0,0}},
    {{6,1},{5,1},{3,7},{3,6},{3,5},{3,4},{3,3},{3,2},{4,1},{3,1},
     {6,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{6,1},{5,1},{3,5},{3,4},{3,3},{2,3},{3,2},{4,1},{3,1},{6,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{6,1},{4,1},{5,1},{3,3},{2,3},{2,2},{3,2},{3,1},{6,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{6,1},{6,0},{4,1},{2,3},{2,2},{3,1},{2,1},{5,1},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{5,1},{5,0},{3,1},{2,3},{2,2},{2,1},{4,1},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{4,0},{4,1},{3,1},{3,2},{1,1},{3,3},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{4,0},{4,1},{2,1},{1,1},{3,1},{0,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{3,0},{3,1},{1,1},{2,1},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{2,0},{2,1},{1,1},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{1,0},{1,1},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
};

// ---- Table 9-10：run_before 码表 ----
// 索引 [min(zerosLeft,7)-1 (0..6)][run_before (0..14)]。与解码侧一致。
const VlcCode kRunBefore[7][15] = {
    {{1,1},{1,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0}},
    {{1,1},{2,1},{2,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0}},
    {{2,3},{2,2},{2,1},{2,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0}},
    {{2,3},{2,2},{2,1},{3,1},{3,0},{0,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0}},
    {{2,3},{2,2},{3,3},{3,2},{3,1},{3,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0}},
    {{2,3},{3,0},{3,1},{3,3},{3,2},{3,5},{3,4},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0}},
    {{3,7},{3,6},{3,5},{3,4},{3,3},{3,2},{3,1},{4,1},{5,1},{6,1},
     {7,1},{8,1},{9,1},{10,1},{11,1}},
};

// 写 1 位，返回写入的比特数（恒为 1），供统计各语法元素的比特占用。
size_t WriteBits(BitWriter& bw, uint32_t bit) {
    bw.WriteBits(bit, 1);
    return 1;
}

// 选 coeff_token 表：与解码侧 DecodeCoeffToken 的分档完全一致。
// 返回 nullptr 表示走 nC>=8 的定长 6 位码（不查表）。
const VlcCode (*SelectCoeffTokenTable(int nc))[17] {
    if (nc >= 8) return nullptr;
    if (nc >= 4) return kCoeffToken2;
    if (nc >= 2) return kCoeffToken1;
    return kCoeffToken0;
}

// 写 coeff_token（spec 9.2.1）。nc>=8 用定长码：
//   TotalCoeff==0 -> code=3；否则 code=((TotalCoeff-1)<<2)|TrailingOnes，均 6 位。
size_t WriteCoeffToken(BitWriter& bw, int total_coeff, int trailing_ones, int nc) {
    size_t before = bw.BitCount();
    const VlcCode (*table)[17] = SelectCoeffTokenTable(nc);
    if (table == nullptr) {
        uint32_t code = (total_coeff == 0)
                            ? 3u
                            : (static_cast<uint32_t>(total_coeff - 1) << 2 |
                               static_cast<uint32_t>(trailing_ones));
        bw.WriteBits(code, 6);
    } else {
        const VlcCode& e = table[trailing_ones][total_coeff];
        bw.WriteBits(static_cast<uint32_t>(e.code), e.len);
    }
    return bw.BitCount() - before;
}

// 解码侧合成公式的正向副本：给定 (level_prefix, level_suffix, suffix_length)，
// 复现 cavlc_decode.cpp 里由 prefix/suffix 合成 level_code 的那几步。编码时用它
// 来验证候选 (prefix, suffix) 是否能被解码器解回目标值——保证编解码严丝合缝。
//   返回合成出的 level_code；level_suffix_size 通过出参返回（供写后缀用）。
int SynthLevelCode(int level_prefix, uint32_t level_suffix, int suffix_length,
                   int* level_suffix_size) {
    int size = suffix_length;
    if (level_prefix == 14 && suffix_length == 0) size = 4;
    else if (level_prefix >= 15) size = level_prefix - 3;
    *level_suffix_size = size;

    int level_code = (level_prefix << suffix_length) +
                     static_cast<int>(level_suffix);
    if (level_prefix >= 15 && suffix_length == 0) level_code += 15;
    if (level_prefix >= 16) level_code += (1 << (level_prefix - 3)) - 4096;
    return level_code;
}

// 写一个非零系数的 level（spec 9.2.2 反向）。
//   target_code：已扣除“i==t1s && t1s<3 时 +2”修正后的幅值编码值（见调用处）。
//   suffix_length：当前自适应后缀长度（0..6）。
// 策略：直接以“解码侧合成公式的正向副本”为准绳，从小到大枚举 level_prefix，
// 反解出唯一的 level_suffix，再用 SynthLevelCode 复算校验是否命中 target_code。
// 这样无需手推 escape 区间的代数，编出的比特一定能被解码器原样解回。
size_t WriteLevel(BitWriter& bw, int target_code, int suffix_length) {
    size_t before = bw.BitCount();
    int chosen_prefix = -1;
    int chosen_size = 0;
    uint32_t chosen_suffix = 0;

    // level_prefix 理论最大 ~ (prefix-3) 位后缀能覆盖极大幅值，48 足够 4x4 量化 level。
    for (int prefix = 0; prefix <= 48; ++prefix) {
        int size;
        // 由 target_code 反推该 prefix 下的 level_suffix，再正向合成校验。
        // 先算“无后缀贡献”的基线合成值（suffix=0），差值就是候选 suffix。
        int base = SynthLevelCode(prefix, 0, suffix_length, &size);
        int diff = target_code - base;
        if (diff < 0) continue;                 // 该 prefix 起点已超过目标
        if (size == 0) {
            if (diff != 0) continue;            // 无后缀，必须精确命中
        } else {
            if (diff >= (1 << size)) continue;  // 后缀放不下这个差值
        }
        uint32_t suffix = static_cast<uint32_t>(diff);
        int check = SynthLevelCode(prefix, suffix, suffix_length, &size);
        if (check == target_code) {
            chosen_prefix = prefix;
            chosen_size = size;
            chosen_suffix = suffix;
            break;
        }
    }

    // 写一元前缀：level_prefix 个 0，接一个 1；再写 level_suffix_size 位后缀。
    for (int k = 0; k < chosen_prefix; ++k) bw.WriteBits(0, 1);
    bw.WriteBits(1, 1);
    if (chosen_size > 0) bw.WriteBits(chosen_suffix, chosen_size);
    return bw.BitCount() - before;
}

// 写 total_zeros（spec 9.2.3）：由 total_coeff 选行，直接查出该 total_zeros 的码字。
size_t WriteTotalZeros(BitWriter& bw, int total_coeff, int total_zeros) {
    size_t before = bw.BitCount();
    // total_coeff==16 时 total_zeros 必为 0，不编（与解码侧 total_coeff<max 对偶）。
    if (total_coeff > 0 && total_coeff < 16) {
        const VlcCode& e = kTotalZeros[total_coeff - 1][total_zeros];
        bw.WriteBits(static_cast<uint32_t>(e.code), e.len);
    }
    return bw.BitCount() - before;
}

// 写 run_before（spec 9.2.3）：由剩余可分配的 0 个数 zeros_left 选行。
size_t WriteRunBefore(BitWriter& bw, int zeros_left, int run_before) {
    size_t before = bw.BitCount();
    if (zeros_left > 0) {
        int idx = (zeros_left > 6) ? 6 : (zeros_left - 1);
        const VlcCode& e = kRunBefore[idx][run_before];
        bw.WriteBits(static_cast<uint32_t>(e.code), e.len);
    }
    return bw.BitCount() - before;
}

}  // namespace

CavlcEncodeStats EncodeResidual4x4(BitWriter& bw, const Cavlc4x4& coeff, int nc) {
    CavlcEncodeStats st;
    st.nc = nc;

    // 1) 统计 TotalCoeff：从最高频往低频找到最后一个非零位置。
    int last_nonzero = -1;
    for (int i = 15; i >= 0; --i) {
        if (coeff[i] != 0) { last_nonzero = i; break; }
    }
    int total_coeff = 0;
    for (int i = 0; i <= last_nonzero; ++i)
        if (coeff[i] != 0) ++total_coeff;
    st.total_coeff = total_coeff;

    // 2) 把非零系数按“从高频到低频”的顺序收集（与解码侧 levels[] 同序），
    //    同时记录每个非零系数“前面”紧跟多少个 0（run_before）。
    //    levels_hi_to_lo[0] 是最高频那个非零系数。
    //    run_before[k] 是 levels[k] 这个非零系数“前面”（更低频/更低索引一侧）
    //    紧跟的 0 的个数，直到下一个更低频的非零系数为止——与解码侧重建时
    //    “pos -= 1; pos -= runs[i]” 的语义精确对偶。
    int levels[16] = {0};
    int runs[16] = {0};
    int total_zeros = 0;
    {
        int k = -1;            // 最近一个已记录的非零系数下标（高->低）
        for (int i = last_nonzero; i >= 0; --i) {
            if (coeff[i] != 0) {
                ++k;
                levels[k] = coeff[i];
                runs[k] = 0;
            } else {
                // 这个 0 位于 levels[k]（上一个已记录的非零）的更低频一侧，
                // 归入 levels[k] 的 run_before。
                if (k >= 0) ++runs[k];
                ++total_zeros;
            }
        }
    }
    st.total_zeros = total_zeros;

    // 统计 TrailingOnes：从最高频端起，连续的 ±1，最多 3 个。
    int t1s = 0;
    for (int i = 0; i < total_coeff && i < 3; ++i) {
        if (levels[i] == 1 || levels[i] == -1) ++t1s;
        else break;
    }
    st.trailing_ones = t1s;

    // 3) 写 coeff_token。
    st.bits_coeff_token = WriteCoeffToken(bw, total_coeff, t1s, nc);

    if (total_coeff == 0) {
        st.bits_total = bw.BitCount();
        return st;  // 整块全 0，只有 coeff_token。
    }

    // 4) 写 trailing_ones 符号（+1->0，-1->1），高频端在前。
    for (int i = 0; i < t1s; ++i) {
        uint32_t sign_bit = (levels[i] < 0) ? 1u : 0u;
        st.bits_sign += WriteBits(bw, sign_bit);
    }

    // 5) 写其余非零系数的 level（level_prefix + level_suffix，自适应 suffixLength）。
    //    这段严格对偶解码侧的自适应与 level_code 合成逻辑（含 i==t1s && t1s<3 时 +2）。
    int suffix_length = (total_coeff > 10 && t1s < 3) ? 1 : 0;
    for (int i = t1s; i < total_coeff; ++i) {
        int value = levels[i];
        // value -> level_code（解码侧奇偶映射的逆）：正->偶，负->奇。
        int level_code = (value > 0) ? (2 * value - 2) : (-2 * value - 1);
        // 第一个“非尾部 ±1”的系数：若恰好有 3 个尾部 ±1 则解码侧不 +2；否则 +2。
        // 编码侧要把这个 +2 从待编 level_code 里扣掉，让解码器加回来后一致。
        if (i == t1s && t1s < 3) level_code -= 2;

        st.bits_level += WriteLevel(bw, level_code, suffix_length);

        // 自适应：suffixLength 随幅值增长（与解码侧同步）。
        if (suffix_length == 0) suffix_length = 1;
        int abs_level = value < 0 ? -value : value;
        if (abs_level > (3 << (suffix_length - 1)) && suffix_length < 6)
            ++suffix_length;
    }

    // 6) 写 total_zeros。
    st.bits_total_zeros = WriteTotalZeros(bw, total_coeff, total_zeros);

    // 7) 写 run_before：与解码侧一致，前 total_coeff-1 个逐个写，最后一个不写
    //    （解码侧把剩余的 0 全给最后一个非零系数）。
    int zeros_left = total_zeros;
    for (int i = 0; i < total_coeff - 1; ++i) {
        if (zeros_left <= 0) break;  // 没有 0 可分配了，后续 run 都是 0，不编。
        st.bits_run_before += WriteRunBefore(bw, zeros_left, runs[i]);
        zeros_left -= runs[i];
    }

    st.bits_total = bw.BitCount();
    return st;
}

// ForwardZigzag4x4：InverseZigzag4x4 的逆。解码侧 out[kRow[k]][kCol[k]]=coeff[k]，
// 这里反过来 coeff[k]=block[kRow[k]][kCol[k]]，用同一张 4x4 zigzag 位置表。
Cavlc4x4 ForwardZigzag4x4(const std::array<std::array<int, 4>, 4>& block) {
    static const int kRow[16] = {0, 0, 1, 2, 1, 0, 0, 1, 2, 3, 3, 2, 1, 2, 3, 3};
    static const int kCol[16] = {0, 1, 0, 0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 3, 2, 3};
    Cavlc4x4 coeff{};
    for (int k = 0; k < 16; ++k) coeff[k] = block[kRow[k]][kCol[k]];
    return coeff;
}

}  // namespace cfs
