// entropy_decode.cpp — 熵解码实现，照 T.81 F.2.1.2 / F.2.2 / F.2.2.1 逐条落地。
#include "entropy_decode.h"

namespace cfs {

int DecodeMagnitude(uint32_t bits, int size) {
    if (size == 0) return 0;
    // 阈值 = 2^(size-1)。>= 阈值最高位是 1，是正数；否则是负数（T.81 F.2.2.1）。
    int32_t threshold = 1 << (size - 1);
    int32_t v = static_cast<int32_t>(bits);
    if (v >= threshold) {
        return v;  // 正数：紧凑表示就是它本身。
    }
    // 负数：value = bits - (2^size - 1) = bits - 2^size + 1。
    return v - (1 << size) + 1;
}

// 从比特流解一个符号，再读它带的 size 位幅值，返回还原后的有符号系数。
// size 通过出参 out_size 带回（AC 解码要用它区分 EOB/ZRL）。ok 报告是否成功。
static int DecodeCoeff(BitReader& br, const HuffDecTable& tbl, int& out_size,
                       bool* ok) {
    bool sym_ok = false;
    uint8_t sym = DecodeSymbol(br, tbl, &sym_ok);
    if (!sym_ok) {
        if (ok) *ok = false;
        out_size = 0;
        return 0;
    }
    out_size = sym & 0x0F;  // 低 4 位是 size（DC 时整个字节就是 size）。
    if (out_size == 0) {
        if (ok) *ok = true;
        return 0;
    }
    uint32_t bits = br.ReadBits(out_size);
    if (br.Eof() && !br.AtMarker()) {
        // 幅值还没读满就 EOF —— 码流被截断。marker 是正常边界，不算错。
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = true;
    return DecodeMagnitude(bits, out_size);
}

bool DecodeBlockZigZag(BitReader& br, const HuffDecTable& dc_tbl,
                       const HuffDecTable& ac_tbl, int& dc_pred,
                       std::array<int, 64>& out) {
    out.fill(0);

    // --- DC：解 category -> 读幅值 -> 差分还原（本块 DC = 前块 DC + diff）---
    // DC 表里符号本身就是 category（SSSS），直接当 size 用。
    {
        bool ok = false;
        int size = 0;
        int diff = DecodeCoeff(br, dc_tbl, size, &ok);
        if (!ok) return false;
        dc_pred += diff;   // DPCM 逆运算：累加差分（T.81 F.2.1.2）。
        out[0] = dc_pred;
    }

    // --- AC：从 zigzag[1] 开始，循环解 (run,size) 填系数 ---
    int k = 1;
    while (k < 64) {
        bool sym_ok = false;
        uint8_t sym = DecodeSymbol(br, ac_tbl, &sym_ok);
        if (!sym_ok) return false;

        int run = (sym >> 4) & 0x0F;   // 高 4 位：前导 0 的个数。
        int size = sym & 0x0F;         // 低 4 位：非零系数的幅值位数。

        if (size == 0) {
            if (run == 15) {
                // ZRL(0xF0)：填 16 个 0，继续（run 字段放不下 >15 的连 0）。
                k += 16;
                continue;
            }
            // EOB(0x00)：本块剩余系数全是 0，解码提前结束（T.81 F.2.2.2）。
            break;
        }

        // 先跳过 run 个 0（out 已清零，直接推进下标），再放一个非零系数。
        k += run;
        if (k >= 64) return false;  // run 越界：码流损坏或表不匹配。

        uint32_t bits = br.ReadBits(size);
        if (br.Eof() && !br.AtMarker()) return false;  // 幅值被截断。
        out[k] = DecodeMagnitude(bits, size);
        ++k;
    }
    return true;
}

}  // namespace cfs
