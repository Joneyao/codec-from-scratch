// huffman_decode.cpp — 从 BITS/HUFFVAL 重建解码表 + 变长解码，照 T.81 Annex C/F。
#include "huffman_decode.h"

namespace cfs {

HuffDecTable BuildDecTable(const std::array<uint8_t, 16>& bits,
                           const std::vector<uint8_t>& huffval) {
    HuffDecTable t;
    t.huffval = huffval;

    // 第一步：按 Annex C 的构码规则，为"每个码长"求出它的码字区间。
    // 构码过程和编码端一样：code 从 0 起，同一码长内每个符号 code+1，
    // 换到下一个码长时整体左移一位。我们不需要逐符号存码字，只要记住每个
    // 码长的第一个码字（mincode）、最后一个码字（maxcode）就够 DECODE 用了。
    int32_t code = 0;
    int k = 0;  // 走到 HUFFVAL 的哪个符号
    for (int len = 1; len <= 16; ++len) {
        int count = bits[len - 1];
        if (count > 0) {
            t.valptr[len] = k;               // 该码长首符号在 huffval 的下标
            t.mincode[len] = code;           // 该码长最小码字
            code += count;                   // 连续分配 count 个码字
            t.maxcode[len] = code - 1;        // 该码长最大码字
            k += count;
        } else {
            t.maxcode[len] = -1;              // 没有这个码长的码字
        }
        code <<= 1;  // 进入下一码长，整体左移（Annex C）
    }
    return t;
}

uint8_t DecodeSymbol(BitReader& br, const HuffDecTable& t, bool* ok) {
    // T.81 F.2.2.3 DECODE：逐位累积 code，凑到某个码长的合法区间就命中。
    int32_t code = 0;
    for (int len = 1; len <= 16; ++len) {
        code = (code << 1) | br.ReadBit();
        if (br.AtMarker() || br.Eof()) {
            if (ok) *ok = false;
            return 0;
        }
        // 该码长有码字，且 code 落在 [mincode, maxcode] 内 —— 命中。
        if (t.maxcode[len] >= 0 && code <= t.maxcode[len]) {
            int idx = t.valptr[len] + (code - t.mincode[len]);
            if (idx < 0 || idx >= static_cast<int>(t.huffval.size())) {
                if (ok) *ok = false;
                return 0;
            }
            if (ok) *ok = true;
            return t.huffval[idx];
        }
    }
    // 读满 16 位仍未命中：码流损坏或表不匹配。
    if (ok) *ok = false;
    return 0;
}

}  // namespace cfs
