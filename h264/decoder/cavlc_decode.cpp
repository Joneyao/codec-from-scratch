// cavlc_decode.cpp — H.264 CAVLC 残差解码实现（严格照 ITU-T H.264 9.2）。
//
// 表来源：ITU-T H.264 (03/2005) Table 9-5（coeff_token）、Table 9-7/9-8
// （total_zeros，4x4）、Table 9-10（run_before）。这些是标准规定的固定码表，
// 直接硬编码。解码时逐位读入，与表里的 (length, code) 匹配。
#include "cavlc_decode.h"

#include <cstdint>

namespace cfs {

namespace {

// coeff_token 一个码字：给定 (trailing_ones, total_coeff)，它的比特长度和码值。
struct VlcCode {
    int len;   // 比特数
    int code;  // 码值（右对齐，MSB 在高位）
};

// ---- Table 9-5：coeff_token 码表（0<=nC<2 / 2<=nC<4 / 4<=nC<8 三档）----
// 索引 [trailing_ones(0..3)][total_coeff(0..16)]。len=0 表示该组合不存在。
// 值来自 ITU-T H.264 Table 9-5。为可读性按 (T1s, TotalCoeff) 排布。

// 第 0 档：0 <= nC < 2
const VlcCode kCoeffToken0[4][17] = {
    // T1s = 0
    {{1,1},{6,5},{8,7},{9,7},{10,7},{11,7},{13,15},{13,11},{13,8},
     {14,15},{14,11},{15,15},{15,11},{16,15},{16,11},{16,7},{16,4}},
    // T1s = 1
    {{0,0},{2,1},{6,4},{8,6},{9,6},{10,6},{11,6},{13,14},{13,10},
     {14,14},{14,10},{15,14},{15,10},{15,1},{16,14},{16,10},{16,6}},
    // T1s = 2
    {{0,0},{0,0},{3,1},{7,5},{8,5},{9,5},{10,5},{11,5},{13,13},
     {13,9},{14,13},{14,9},{15,13},{15,9},{16,13},{16,9},{16,5}},
    // T1s = 3
    {{0,0},{0,0},{0,0},{5,3},{6,3},{7,4},{8,4},{9,4},{10,4},
     {11,4},{13,12},{14,12},{14,8},{15,12},{15,8},{16,12},{16,8}},
};

// 第 1 档：2 <= nC < 4
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

// 第 2 档：4 <= nC < 8
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

// 第 3 档：nC >= 8，定长 6 位码。
//   code = ((total_coeff - 1) << 2) | trailing_ones，total_coeff==0 时 code=3。
CoeffToken DecodeCoeffTokenFixed(RbspBitReader& br) {
    uint32_t v = br.ReadBits(6);
    CoeffToken t;
    if (v == 3) {
        t.total_coeff = 0;
        t.trailing_ones = 0;
    } else {
        t.total_coeff = static_cast<int>((v >> 2) + 1);
        t.trailing_ones = static_cast<int>(v & 3);
    }
    return t;
}

// 在一张码表里，用“逐位读 + 前缀匹配”找到 (T1s, TotalCoeff)。
// 因为变长码是前缀码，读到的比特唯一确定一个码字。
CoeffToken MatchCoeffToken(RbspBitReader& br, const VlcCode table[4][17]) {
    int read_len = 0;
    uint32_t bits = 0;
    // 最长码字 16 位；逐位累积，直到匹配到某个 (len==read_len && code==bits)。
    for (read_len = 1; read_len <= 16; ++read_len) {
        bits = (bits << 1) | br.ReadBit();
        for (int t1 = 0; t1 < 4; ++t1) {
            for (int tc = 0; tc < 17; ++tc) {
                const VlcCode& e = table[t1][tc];
                if (e.len == read_len &&
                    static_cast<uint32_t>(e.code) == bits) {
                    CoeffToken tok;
                    tok.trailing_ones = t1;
                    tok.total_coeff = tc;
                    return tok;
                }
            }
        }
    }
    // 没匹配上（码流损坏）：返回 0，交由上层判断。
    return CoeffToken{};
}

// ---- Table 9-7 / 9-8：total_zeros 码表（4x4，maxNumCoeff=16）----
// 索引 [total_coeff-1 (0..14)][total_zeros (0..15)]。total_coeff 从 1 到 15
// 各有一列表（total_coeff==16 时 total_zeros 必为 0，不用查表）。
// 值来自 ITU-T H.264 Table 9-7（tzVlcIndex 1..7）与 Table 9-8（8..15）。
const VlcCode kTotalZeros[15][16] = {
    // tzVlcIndex = 1
    {{1,1},{3,3},{3,2},{4,3},{4,2},{5,3},{5,2},{6,3},{6,2},{7,3},
     {7,2},{8,3},{8,2},{9,3},{9,2},{9,1}},
    // 2
    {{3,7},{3,6},{3,5},{3,4},{3,3},{4,5},{4,4},{4,3},{4,2},{5,3},
     {5,2},{6,3},{6,2},{6,1},{6,0},{0,0}},
    // 3
    {{4,5},{3,7},{3,6},{3,5},{4,4},{4,3},{3,4},{3,3},{4,2},{5,3},
     {5,2},{6,1},{5,1},{6,0},{0,0},{0,0}},
    // 4
    {{5,3},{3,7},{4,5},{4,4},{3,6},{4,3},{3,5},{3,4},{4,2},{5,2},
     {6,1},{5,1},{6,0},{0,0},{0,0},{0,0}},
    // 5
    {{4,5},{4,4},{4,3},{3,7},{3,6},{3,5},{3,4},{3,3},{4,2},{5,1},
     {4,1},{5,0},{0,0},{0,0},{0,0},{0,0}},
    // 6
    {{6,1},{5,1},{3,7},{3,6},{3,5},{3,4},{3,3},{3,2},{4,1},{3,1},
     {6,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    // 7
    {{6,1},{5,1},{3,5},{3,4},{3,3},{2,3},{3,2},{4,1},{3,1},{6,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    // 8
    {{6,1},{4,1},{5,1},{3,3},{2,3},{2,2},{3,2},{3,1},{6,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    // 9
    {{6,1},{6,0},{4,1},{2,3},{2,2},{3,1},{2,1},{5,1},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    // 10
    {{5,1},{5,0},{3,1},{2,3},{2,2},{2,1},{4,1},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    // 11
    {{4,0},{4,1},{3,1},{3,2},{1,1},{3,3},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    // 12
    {{4,0},{4,1},{2,1},{1,1},{3,1},{0,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    // 13
    {{3,0},{3,1},{1,1},{2,1},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    // 14
    {{2,0},{2,1},{1,1},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    // 15
    {{1,0},{1,1},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
};

// ---- Table 9-10：run_before 码表 ----
// 索引 [min(zerosLeft,7)-1 (0..6)][run_before (0..14)]。
// zerosLeft>=7 都用第 6 行（>6 的档共用）。
const VlcCode kRunBefore[7][15] = {
    // zerosLeft = 1
    {{1,1},{1,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0}},
    // 2
    {{1,1},{2,1},{2,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0}},
    // 3
    {{2,3},{2,2},{2,1},{2,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0}},
    // 4
    {{2,3},{2,2},{2,1},{3,1},{3,0},{0,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0}},
    // 5
    {{2,3},{2,2},{3,3},{3,2},{3,1},{3,0},{0,0},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0}},
    // 6
    {{2,3},{3,0},{3,1},{3,3},{3,2},{3,5},{3,4},{0,0},{0,0},{0,0},
     {0,0},{0,0},{0,0},{0,0},{0,0}},
    // zerosLeft > 6
    {{3,7},{3,6},{3,5},{3,4},{3,3},{3,2},{3,1},{4,1},{5,1},{6,1},
     {7,1},{8,1},{9,1},{10,1},{11,1}},
};

// 在一张“行 -> 若干码字”的表里逐位匹配，返回列号（即语法元素的值）。
// row 指向 table 的一整行，valid_cols 是这一行有效列数。
int MatchInRow(RbspBitReader& br, const VlcCode* row, int valid_cols) {
    int read_len = 0;
    uint32_t bits = 0;
    for (read_len = 1; read_len <= 16; ++read_len) {
        bits = (bits << 1) | br.ReadBit();
        for (int col = 0; col < valid_cols; ++col) {
            if (row[col].len == read_len &&
                static_cast<uint32_t>(row[col].code) == bits) {
                return col;
            }
        }
        if (br.Overrun()) break;
    }
    return 0;
}

// 解 total_zeros（spec 9.2.3）：由 total_coeff 选行，逐位匹配得到值。
int DecodeTotalZeros(RbspBitReader& br, int total_coeff) {
    if (total_coeff <= 0 || total_coeff >= 16) return 0;
    const VlcCode* row = kTotalZeros[total_coeff - 1];
    // 有效列数：total_zeros 最大 = 16 - total_coeff。
    int valid = 16 - total_coeff + 1;
    if (valid > 16) valid = 16;
    return MatchInRow(br, row, valid);
}

// 解 run_before（spec 9.2.3）：由剩余可分配的 0 的个数 zeros_left 选行。
int DecodeRunBefore(RbspBitReader& br, int zeros_left) {
    if (zeros_left <= 0) return 0;
    int idx = (zeros_left > 6) ? 6 : (zeros_left - 1);
    const VlcCode* row = kRunBefore[idx];
    // 第 6 行（zeros_left>6）最多 15 列；其余行最多 zeros_left+1 列。
    int valid = (zeros_left > 6) ? 15 : (zeros_left + 1);
    return MatchInRow(br, row, valid);
}

}  // namespace

CoeffToken DecodeCoeffToken(RbspBitReader& br, int nc) {
    if (nc >= 8) return DecodeCoeffTokenFixed(br);
    if (nc >= 4) return MatchCoeffToken(br, kCoeffToken2);
    if (nc >= 2) return MatchCoeffToken(br, kCoeffToken1);
    return MatchCoeffToken(br, kCoeffToken0);
}

int DeriveNc(int nnz_left, bool available_a, int nnz_top, bool available_b) {
    if (available_a && available_b) return (nnz_left + nnz_top + 1) >> 1;
    if (available_a) return nnz_left;
    if (available_b) return nnz_top;
    return 0;
}

namespace {

// 解一个非零系数的幅值 level（spec 9.2.2）。
//   level_prefix：前导 0 的个数（直到第一个 1），是一元码。
//   suffixLength：后缀位数，随已解出的幅值自适应增长。
//   level_suffix：读 suffixLength 位。
// 由 prefix / suffix / suffixLength 合成带符号 level。这里返回“未定符号的
// 幅值编码值 levelCode”，符号在调用处按奇偶决定。
int ReadLevelPrefix(RbspBitReader& br) {
    int leading_zeros = 0;
    while (br.ReadBit() == 0 && !br.Overrun()) ++leading_zeros;
    return leading_zeros;
}

}  // namespace

CavlcResult DecodeResidual4x4(RbspBitReader& br, int nc, int max_num_coeff) {
    CavlcResult r;
    r.nc = nc;
    size_t start_bit = br.BitPos();

    // 1) coeff_token：一次拿到 TotalCoeff 与 TrailingOnes。
    r.token = DecodeCoeffToken(br, nc);
    const int total_coeff = r.token.total_coeff;
    const int t1s = r.token.trailing_ones;

    if (total_coeff == 0) {
        r.bits_used = br.BitPos() - start_bit;
        return r;  // 整块全 0。
    }

    // 2) 解各非零系数的 level（顺序：从最高频的非零系数往低频走）。
    //    先是 TrailingOnes 个 ±1（只读 1 位符号），再是其余幅值。
    int suffix_length = (total_coeff > 10 && t1s < 3) ? 1 : 0;

    for (int i = 0; i < total_coeff; ++i) {
        if (i < t1s) {
            // 尾部 ±1：读 1 位符号，0->+1，1->-1（spec 9.2.2）。
            int sign = static_cast<int>(br.ReadBit());
            r.levels[i] = sign ? -1 : 1;
            continue;
        }
        // 其余非零系数：level_prefix + level_suffix。
        int level_prefix = ReadLevelPrefix(br);
        int level_suffix_size = suffix_length;
        if (level_prefix == 14 && suffix_length == 0) level_suffix_size = 4;
        else if (level_prefix >= 15) level_suffix_size = level_prefix - 3;

        int level_suffix = 0;
        if (level_suffix_size > 0)
            level_suffix = static_cast<int>(br.ReadBits(level_suffix_size));

        int level_code = (level_prefix << suffix_length) + level_suffix;
        if (level_prefix >= 15 && suffix_length == 0) level_code += 15;
        if (level_prefix >= 16)
            level_code += (1 << (level_prefix - 3)) - 4096;
        // 第一个“非尾部 ±1”的系数：若前面恰好有 3 个尾部 ±1，幅值要 +2。
        if (i == t1s && t1s < 3) level_code += 2;

        int value;
        if ((level_code & 1) == 0)
            value = (level_code + 2) >> 1;   // 偶 -> 正
        else
            value = (-level_code - 1) >> 1;   // 奇 -> 负
        r.levels[i] = value;

        // 自适应：suffixLength 随幅值增长（spec 9.2.2）。
        if (suffix_length == 0) suffix_length = 1;
        int abs_level = value < 0 ? -value : value;
        if (abs_level > (3 << (suffix_length - 1)) && suffix_length < 6)
            ++suffix_length;
    }

    // 3) total_zeros：所有非零系数之前/之间的 0 总数。
    int total_zeros = 0;
    if (total_coeff < max_num_coeff)
        total_zeros = DecodeTotalZeros(br, total_coeff);
    r.total_zeros = total_zeros;

    // 4) run_before：把 total_zeros 分配到各非零系数“前面”。
    int zeros_left = total_zeros;
    for (int i = 0; i < total_coeff - 1; ++i) {
        int run = (zeros_left > 0) ? DecodeRunBefore(br, zeros_left) : 0;
        r.runs[i] = run;
        zeros_left -= run;
    }
    // 最后一个（最低频那个非零系数）前面的 0 是剩下的全部。
    r.runs[total_coeff - 1] = zeros_left;

    // 5) 组合还原：把 level 和 run 摆回扫描顺序的 16 个位置。
    //    levels[0] 是最高频的非零系数，coeff 索引 0 是最低频（DC）。
    //    从高频往低频填：位置指针从 (total_coeff-1 个非零 + total_zeros) 处倒着走。
    Cavlc4x4 coeff{};
    int pos = total_coeff + total_zeros - 1;  // 最高扫描位置索引
    for (int i = 0; i < total_coeff; ++i) {
        coeff[pos] = r.levels[i];
        pos -= 1;              // 跳过这个非零系数本身
        pos -= r.runs[i];      // 再跳过它前面的 run 个 0
    }
    r.coeff = coeff;
    r.bits_used = br.BitPos() - start_bit;
    return r;
}

// zigzag 反扫描（spec 8.5.6 Table 8-13，frame 扫描）：
// 扫描序号 k -> 二维 (row, col)。
std::array<std::array<int, 4>, 4> InverseZigzag4x4(const Cavlc4x4& coeff) {
    // 4x4 zigzag：每个扫描位置对应的 (row, col)。
    static const int kRow[16] = {0, 0, 1, 2, 1, 0, 0, 1, 2, 3, 3, 2, 1, 2, 3, 3};
    static const int kCol[16] = {0, 1, 0, 0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 3, 2, 3};
    std::array<std::array<int, 4>, 4> out{};
    for (int k = 0; k < 16; ++k) out[kRow[k]][kCol[k]] = coeff[k];
    return out;
}

}  // namespace cfs
