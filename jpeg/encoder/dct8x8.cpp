// dct8x8.cpp — 8x8 DCT / IDCT，直接照 T.81 A.3.3 的公式写。
#include "dct8x8.h"

#include <cmath>
#include <fstream>

namespace cfs {

namespace {
constexpr double kPi = 3.14159265358979323846;

// C(u) = 1/sqrt(2) 当 u==0，否则 1。
inline double C(int u) { return u == 0 ? 1.0 / std::sqrt(2.0) : 1.0; }

// 预计算 cos((2x+1)*u*pi/16)，避免重复算三角函数。
struct CosTable {
    double t[8][8];
    CosTable() {
        for (int x = 0; x < 8; ++x)
            for (int u = 0; u < 8; ++u)
                t[x][u] = std::cos((2 * x + 1) * u * kPi / 16.0);
    }
};
const CosTable kCos;
}  // namespace

// F(u,v) = 1/4 * C(u)C(v) * sum_x sum_y f(x,y) cos(...) cos(...)
Block8d ForwardDct(const Block8u& pixels) {
    // level shift：减 128。
    double s[8][8];
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            s[y][x] = static_cast<double>(pixels[y][x]) - 128.0;

    Block8d out{};
    for (int v = 0; v < 8; ++v) {
        for (int u = 0; u < 8; ++u) {
            double sum = 0.0;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    sum += s[y][x] * kCos.t[x][u] * kCos.t[y][v];
            out[v][u] = 0.25 * C(u) * C(v) * sum;
        }
    }
    return out;
}

// f(x,y) = 1/4 * sum_u sum_v C(u)C(v) F(u,v) cos(...) cos(...)，再 +128 并 clamp。
Block8u InverseDct(const Block8d& coeffs) {
    Block8u out{};
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            double sum = 0.0;
            for (int v = 0; v < 8; ++v)
                for (int u = 0; u < 8; ++u)
                    sum += C(u) * C(v) * coeffs[v][u] * kCos.t[x][u] *
                           kCos.t[y][v];
            double val = 0.25 * sum + 128.0;
            int iv = static_cast<int>(std::lround(val));
            if (iv < 0) iv = 0;
            if (iv > 255) iv = 255;
            out[y][x] = static_cast<uint8_t>(iv);
        }
    }
    return out;
}

bool DumpBlock(const Block8d& b, const std::string& path, bool round) {
    std::ofstream os(path);
    if (!os) return false;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (round)
                os << static_cast<long>(std::lround(b[y][x]));
            else
                os << b[y][x];
            if (x < 7) os << ' ';
        }
        os << '\n';
    }
    return static_cast<bool>(os);
}

bool DumpBlockU8(const Block8u& b, const std::string& path) {
    std::ofstream os(path);
    if (!os) return false;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            os << static_cast<int>(b[y][x]);
            if (x < 7) os << ' ';
        }
        os << '\n';
    }
    return static_cast<bool>(os);
}

}  // namespace cfs
