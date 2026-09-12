// jfif_parser.cpp — 扫描 marker、解析各段，把真实 .jpg 拆成内存结构。
// 照 T.81 Annex B（marker 与段结构）。
#include "jfif_parser.h"

#include <cstdio>
#include <fstream>

#include "zigzag_rle.h"  // kZigZagOrder，用来把 DQT 的 zig-zag 字节反排回 8x8

namespace cfs {

// --- marker 常量（T.81 B.1.1.3 / B.2）---
namespace {
constexpr uint8_t kMarkerPrefix = 0xFF;
constexpr uint8_t kSOI = 0xD8;
constexpr uint8_t kEOI = 0xD9;
constexpr uint8_t kSOS = 0xDA;
constexpr uint8_t kDQT = 0xDB;
constexpr uint8_t kDHT = 0xC4;
constexpr uint8_t kDRI = 0xDD;
constexpr uint8_t kSOF0 = 0xC0;   // baseline 顺序 DCT
constexpr uint8_t kAPP1 = 0xE1;   // EXIF 常驻此段

// 读大端 16 位。
uint16_t Be16(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<uint16_t>((b[off] << 8) | b[off + 1]);
}

// 独立 marker（后面不带段长）：SOI/EOI/RSTn/TEM。
bool IsStandaloneMarker(uint8_t code) {
    if (code == kSOI || code == kEOI) return true;
    if (code >= 0xD0 && code <= 0xD7) return true;  // RST0..RST7
    if (code == 0x01) return true;                   // TEM
    return false;
}
}  // namespace

std::string MarkerName(uint8_t code) {
    switch (code) {
        case 0xD8: return "SOI";
        case 0xD9: return "EOI";
        case 0xDA: return "SOS";
        case 0xDB: return "DQT";
        case 0xC4: return "DHT";
        case 0xDD: return "DRI";
        case 0xC0: return "SOF0";
        case 0xC1: return "SOF1";
        case 0xC2: return "SOF2";
        case 0xE0: return "APP0";
        case 0xE1: return "APP1(EXIF)";
        case 0xFE: return "COM";
        default: break;
    }
    if (code >= 0xE0 && code <= 0xEF) return "APPn";
    if (code >= 0xD0 && code <= 0xD7) return "RSTn";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%02X", code);
    return std::string(buf);
}

std::vector<uint8_t> ReadWholeFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

// --- 各段解析：从 seg 指向的段数据体（跳过 marker 与 Lp）解析出结构 ---
namespace {

// DQT（T.81 B.2.4.1）：一或多张量化表连排。每张 = 1 字节(Pq<<4|Tq) + 64 或
// 128 字节表数据（按 zig-zag 顺序）。反 zig-zag 回 8x8 行主序存入 header。
void ParseDqt(const std::vector<uint8_t>& b, size_t body, size_t body_len,
              JpegHeader& h) {
    size_t p = body;
    size_t end = body + body_len;
    while (p < end) {
        uint8_t pqtq = b[p++];
        uint8_t precision = pqtq >> 4;     // 0=8bit, 1=16bit
        uint8_t table_id = pqtq & 0x0F;
        ParsedQuantTable q;
        q.table_id = table_id;
        q.precision = precision;
        for (int k = 0; k < 64; ++k) {
            int val;
            if (precision == 0) {
                val = b[p++];
            } else {
                val = Be16(b, p);
                p += 2;
            }
            int idx = kZigZagOrder[k];      // zig-zag 第 k 个 → 行主序位置
            q.table[idx / 8][idx % 8] = static_cast<uint16_t>(val);
        }
        h.quant_tables.push_back(std::move(q));
    }
}

// DHT（T.81 B.2.4.2）：一或多张霍夫曼表连排。每张 = 1 字节(Tc<<4|Th) +
// 16 字节 BITS + sum(BITS) 字节 HUFFVAL。重建成解码表。
void ParseDht(const std::vector<uint8_t>& b, size_t body, size_t body_len,
              JpegHeader& h) {
    size_t p = body;
    size_t end = body + body_len;
    while (p < end) {
        uint8_t tcth = b[p++];
        ParsedHuffTable ht;
        ht.table_class = tcth >> 4;        // 0=DC, 1=AC
        ht.table_id = tcth & 0x0F;
        int total = 0;
        for (int i = 0; i < 16; ++i) {
            ht.bits[i] = b[p++];
            total += ht.bits[i];
        }
        ht.huffval.reserve(total);
        for (int i = 0; i < total; ++i) ht.huffval.push_back(b[p++]);
        ht.table = BuildDecTable(ht.bits, ht.huffval);
        h.huffman_tables.push_back(std::move(ht));
    }
}

// SOF0（T.81 B.2.2）：精度(1) + 高(2) + 宽(2) + 分量数 Nf(1)，
// 之后每分量 = id(1) + (Hi<<4|Vi)(1) + Tqi(1)。
void ParseSof0(const std::vector<uint8_t>& b, size_t body, JpegHeader& h) {
    size_t p = body;
    h.precision = b[p++];
    h.height = Be16(b, p); p += 2;
    h.width = Be16(b, p); p += 2;
    int nf = b[p++];
    for (int i = 0; i < nf; ++i) {
        FrameComponent c;
        c.id = b[p++];
        uint8_t hv = b[p++];
        c.h_sample = hv >> 4;
        c.v_sample = hv & 0x0F;
        c.quant_id = b[p++];
        h.frame_components.push_back(c);
    }
}

// SOS（T.81 B.2.3）：分量数 Ns(1)，之后每分量 = id(1) + (Td<<4|Ta)(1)，
// 尾部 3 字节 Ss/Se/(Ah<<4|Al) 是渐进式参数，baseline 固定 0/63/0，跳过即可。
void ParseSos(const std::vector<uint8_t>& b, size_t body, JpegHeader& h) {
    size_t p = body;
    int ns = b[p++];
    for (int i = 0; i < ns; ++i) {
        ScanComponent c;
        c.id = b[p++];
        uint8_t tdta = b[p++];
        c.dc_table = tdta >> 4;
        c.ac_table = tdta & 0x0F;
        h.scan_components.push_back(c);
    }
    // 后面 3 字节谱选择参数对 baseline 无意义，跳过。
}

}  // namespace

ParseResult ParseJpeg(const std::vector<uint8_t>& bytes) {
    ParseResult r;
    JpegHeader& h = r.header;
    const size_t n = bytes.size();

    if (n < 2 || bytes[0] != kMarkerPrefix || bytes[1] != kSOI) {
        r.error = "不是合法 JPEG：文件开头不是 SOI(FFD8)";
        return r;
    }

    size_t i = 0;
    while (i + 1 < n) {
        // 找到一个 marker 前缀。真实文件里段与段之间应紧挨，但容错扫过填充的 0xFF。
        if (bytes[i] != kMarkerPrefix) { ++i; continue; }
        // 连续 0xFF 是允许的填充（T.81 B.1.1.2），跳到最后一个 0xFF。
        while (i + 1 < n && bytes[i + 1] == kMarkerPrefix) ++i;
        if (i + 1 >= n) break;
        uint8_t code = bytes[i + 1];
        if (code == 0x00) { i += 2; continue; }  // 不是 marker（数据里的 0xFF00）

        MarkerRecord rec;
        rec.offset = i;
        rec.code = code;
        rec.name = MarkerName(code);

        if (IsStandaloneMarker(code)) {
            rec.length = 0;
            h.markers.push_back(rec);
            if (code == kEOI) break;
            i += 2;
            continue;
        }

        // 带段长的 marker：读 Lp（大端，含自身两字节）。
        if (i + 3 >= n) { r.error = "段长越界"; return r; }
        int seg_len = Be16(bytes, i + 2);
        rec.length = seg_len;
        h.markers.push_back(rec);

        size_t body = i + 4;                 // 段数据体起点（跳过 marker+Lp）
        size_t body_len = static_cast<size_t>(seg_len) - 2;
        if (body + body_len > n) { r.error = "段数据越界"; return r; }

        switch (code) {
            case kDQT: ParseDqt(bytes, body, body_len, h); break;
            case kDHT: ParseDht(bytes, body, body_len, h); break;
            case kSOF0: ParseSof0(bytes, body, h); break;
            case kDRI: h.restart_interval = Be16(bytes, body); break;
            case kAPP1: h.has_exif = true; break;   // EXIF 内容本篇不深挖
            case kSOS: {
                ParseSos(bytes, body, h);
                // SOS 段之后紧跟熵编码数据，一直到 EOI 前。
                h.scan_data_offset = body + body_len;
                // 从熵数据起点扫到 EOI，确定数据长度。
                size_t s = h.scan_data_offset;
                while (s + 1 < n) {
                    if (bytes[s] == kMarkerPrefix && bytes[s + 1] != 0x00 &&
                        !(bytes[s + 1] >= 0xD0 && bytes[s + 1] <= 0xD7)) {
                        break;  // 撞上非 RST 的 marker（通常是 EOI）
                    }
                    ++s;
                }
                h.scan_data_length = s - h.scan_data_offset;
                // 记录 EOI（若存在）后收尾。
                if (s + 1 < n && bytes[s + 1] == kEOI) {
                    MarkerRecord eoi;
                    eoi.offset = s;
                    eoi.code = kEOI;
                    eoi.length = 0;
                    eoi.name = "EOI";
                    h.markers.push_back(eoi);
                }
                r.ok = true;
                return r;  // 头部解析到 SOS 即完成，熵数据留给 A7
            }
            default: break;  // APP0/APPn/COM 等不认识或不需要的段直接跳过
        }
        i = body + body_len;
    }

    // 没走到 SOS 就结束了。
    if (!r.ok) r.error = r.error.empty() ? "未找到 SOS 段" : r.error;
    return r;
}

ParseResult ParseJpegFile(const std::string& path) {
    std::vector<uint8_t> bytes = ReadWholeFile(path);
    if (bytes.empty()) {
        ParseResult r;
        r.error = "无法读取文件：" + path;
        return r;
    }
    return ParseJpeg(bytes);
}

}  // namespace cfs
