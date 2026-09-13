// nal_splitter.cpp — Annex B 字节流切分 + Emulation Prevention 还原。
// 实现依据：ITU-T H.264 Annex B(B.1)、7.3.1(nal_unit)、7.4.1(语义)。
#include "nal_splitter.h"

#include <cstdio>

namespace cfs {

std::string NalUnit::TypeName() const {
    switch (nal_unit_type) {
        case kNalSliceNonIdr: return "non-IDR slice";
        case kNalSlicePartA:  return "slice part A";
        case kNalSlicePartB:  return "slice part B";
        case kNalSlicePartC:  return "slice part C";
        case kNalSliceIdr:    return "IDR slice";
        case kNalSei:         return "SEI";
        case kNalSps:         return "SPS";
        case kNalPps:         return "PPS";
        case kNalAud:         return "AUD";
        default:              return "type " + std::to_string(nal_unit_type);
    }
}

// 判断 bytes[pos] 处是否为起始码前缀。返回起始码长度：
//   4 —— 命中 0x00000001（zero_byte + start_code_prefix_one_3bytes）
//   3 —— 命中 0x000001（start_code_prefix_one_3bytes）
//   0 —— 不是起始码
static size_t StartCodeLenAt(const std::vector<uint8_t>& b, size_t pos) {
    // 先看 4 字节：00 00 00 01。B.1 把它拆成 zero_byte(00) + 3 字节起始码。
    if (pos + 3 < b.size() && b[pos] == 0x00 && b[pos + 1] == 0x00 &&
        b[pos + 2] == 0x00 && b[pos + 3] == 0x01) {
        return 4;
    }
    // 再看 3 字节：00 00 01。
    if (pos + 2 < b.size() && b[pos] == 0x00 && b[pos + 1] == 0x00 &&
        b[pos + 2] == 0x01) {
        return 3;
    }
    return 0;
}

// 从 pos 起找下一个起始码首字节；找不到返回 b.size()。同时回填该起始码长度。
static size_t FindNextStartCode(const std::vector<uint8_t>& b, size_t pos,
                                size_t* code_len) {
    while (pos + 2 < b.size()) {
        size_t len = StartCodeLenAt(b, pos);
        if (len != 0) {
            *code_len = len;
            return pos;
        }
        ++pos;
    }
    *code_len = 0;
    return b.size();
}

// 7.4.1：把 NAL 载荷里的 emulation_prevention_three_byte(0x03) 吞掉。
// 规则：每当出现字节序列 00 00 03，且 03 后面跟的是 00/01/02/03 之一时，
// 这个 03 是转义字节，丢弃它（把 00 00 03 xx 还原成 00 00 xx）。
std::vector<uint8_t> UnescapeRbsp(const uint8_t* nal, size_t len, int* removed) {
    std::vector<uint8_t> rbsp;
    rbsp.reserve(len);
    int drop = 0;
    // zero_run 记录当前已连续出现了几个 0x00。
    int zero_run = 0;
    for (size_t i = 0; i < len; ++i) {
        uint8_t c = nal[i];
        if (zero_run >= 2 && c == 0x03 && i + 1 < len && nal[i + 1] <= 0x03) {
            // 命中 00 00 03，且后一字节 ∈ {00,01,02,03}：这个 03 是转义字节。
            ++drop;
            zero_run = 0;   // 03 被吞掉，00 计数清零（后续 00 重新计）
            continue;       // 不写入 rbsp
        }
        rbsp.push_back(c);
        zero_run = (c == 0x00) ? (zero_run + 1) : 0;
    }
    if (removed) *removed = drop;
    return rbsp;
}

NalSplitResult SplitAnnexB(const std::vector<uint8_t>& bytes) {
    NalSplitResult r;
    if (bytes.size() < 4) {
        r.error = "byte stream too short";
        return r;
    }

    // 1) 定位第一个起始码。B.1 允许流开头有若干 leading_zero_8bits，直接跳过。
    size_t code_len = 0;
    size_t pos = FindNextStartCode(bytes, 0, &code_len);
    if (pos == bytes.size()) {
        r.error = "no start code found (not an Annex B stream?)";
        return r;
    }

    // 2) 逐个切分：当前起始码之后到下一个起始码之前，就是一个 NAL Unit。
    while (pos < bytes.size()) {
        size_t nal_off = pos + code_len;      // NAL 首字节（header）位置
        size_t next_code_len = 0;
        size_t next_pos = FindNextStartCode(bytes, nal_off, &next_code_len);

        // NAL 载荷区间 [nal_off, nal_end)。next_pos 若是 4 字节起始码，
        // 它的第一个 00 其实是那个起始码的 zero_byte，天然不属于当前 NAL，
        // 直接用 next_pos 作为上界即可（trailing_zero 也一并交给下一段判定）。
        size_t nal_end = next_pos;
        if (nal_end <= nal_off) {
            // 起始码后没有任何数据，跳过这个空单元。
            pos = next_pos;
            code_len = next_code_len;
            continue;
        }
        size_t nal_size = nal_end - nal_off;

        NalUnit u;
        u.start_code_offset = pos;
        u.start_code_len    = code_len;
        u.nal_offset        = nal_off;
        u.nal_size          = nal_size;

        // 3) 解析 header 字节（7.3.1）：1 bit forbidden + 2 bit ref_idc + 5 bit type。
        uint8_t h = bytes[nal_off];
        u.forbidden_zero_bit = (h >> 7) & 0x01;
        u.nal_ref_idc        = (h >> 5) & 0x03;
        u.nal_unit_type      = h & 0x1F;

        // 4) 去转义，得到 RBSP（含 header 字节本身作为 rbsp[0]）。
        int removed = 0;
        u.rbsp = UnescapeRbsp(&bytes[nal_off], nal_size, &removed);
        u.emulation_bytes_removed = removed;
        r.total_emulation_bytes_removed += removed;

        r.units.push_back(std::move(u));

        pos = next_pos;
        code_len = next_code_len;
    }

    r.ok = !r.units.empty();
    if (!r.ok) r.error = "no NAL unit parsed";
    return r;
}

std::vector<uint8_t> ReadFileBytes(const std::string& path) {
    std::vector<uint8_t> data;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return data;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        data.resize(static_cast<size_t>(n));
        size_t got = std::fread(data.data(), 1, data.size(), f);
        data.resize(got);
    }
    std::fclose(f);
    return data;
}

}  // namespace cfs
