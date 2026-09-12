// quantize.cpp — 量化 / 反量化实现，照 T.81 A.3.4 与 Annex K 写。
#include "quantize.h"

#include <cmath>
#include <fstream>

namespace cfs {

// Table K.1 — 亮度量化表（质量 50 基准）。T.81 Annex K。
const QuantTable kBaseLuma = {{
    {{16, 11, 10, 16, 24, 40, 51, 61}},
    {{12, 12, 14, 19, 26, 58, 60, 55}},
    {{14, 13, 16, 24, 40, 57, 69, 56}},
    {{14, 17, 22, 29, 51, 87, 80, 62}},
    {{18, 22, 37, 56, 68, 109, 103, 77}},
    {{24, 35, 55, 64, 81, 104, 113, 92}},
    {{49, 64, 78, 87, 103, 121, 120, 101}},
    {{72, 92, 95, 98, 112, 100, 103, 99}},
}};

// Table K.2 — 色度量化表（质量 50 基准）。T.81 Annex K。
const QuantTable kBaseChroma = {{
    {{17, 18, 24, 47, 99, 99, 99, 99}},
    {{18, 21, 26, 66, 99, 99, 99, 99}},
    {{24, 26, 56, 99, 99, 99, 99, 99}},
    {{47, 66, 99, 99, 99, 99, 99, 99}},
    {{99, 99, 99, 99, 99, 99, 99, 99}},
    {{99, 99, 99, 99, 99, 99, 99, 99}},
    {{99, 99, 99, 99, 99, 99, 99, 99}},
    {{99, 99, 99, 99, 99, 99, 99, 99}},
}};

QuantTable ScaleTable(const QuantTable& base, int quality) {
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    // libjpeg 经典缩放：把"质量"映射成一个百分比 scale。
    int scale = quality < 50 ? 5000 / quality : 200 - 2 * quality;

    QuantTable out{};
    for (int v = 0; v < 8; ++v)
        for (int u = 0; u < 8; ++u) {
            int step = (base[v][u] * scale + 50) / 100;
            if (step < 1) step = 1;      // 步长最小为 1（不能为 0）。
            if (step > 255) step = 255;  // 8 bit 量化表上限。
            out[v][u] = static_cast<uint16_t>(step);
        }
    return out;
}

// Sq(u,v) = round( S(u,v) / Q(u,v) )，T.81 A.3.4 式 (A.7)。
Block8d Quantize(const Block8d& coeffs, const QuantTable& table) {
    Block8d out{};
    for (int v = 0; v < 8; ++v)
        for (int u = 0; u < 8; ++u) {
            double step = table[v][u];
            out[v][u] = std::round(coeffs[v][u] / step);
        }
    return out;
}

// R(u,v) = Sq(u,v) * Q(u,v)，T.81 A.3.4 式 (A.8)。反量化只是乘回步长。
Block8d Dequantize(const Block8d& quantized, const QuantTable& table) {
    Block8d out{};
    for (int v = 0; v < 8; ++v)
        for (int u = 0; u < 8; ++u)
            out[v][u] = quantized[v][u] * static_cast<double>(table[v][u]);
    return out;
}

bool DumpQuantTable(const QuantTable& t, const std::string& path) {
    std::ofstream os(path);
    if (!os) return false;
    for (int v = 0; v < 8; ++v) {
        for (int u = 0; u < 8; ++u) {
            os << t[v][u];
            if (u < 7) os << ' ';
        }
        os << '\n';
    }
    return static_cast<bool>(os);
}

}  // namespace cfs
