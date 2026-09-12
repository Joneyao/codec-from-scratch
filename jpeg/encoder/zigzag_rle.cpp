// zigzag_rle.cpp — Zig-Zag 扫描 + AC 游程编码实现，照 T.81 F.1.2 写。
#include "zigzag_rle.h"

#include <cmath>
#include <fstream>

namespace cfs {

// T.81 Figure A.6 的之字形顺序（行主序索引 row*8+col）。
// 从 0(DC) 出发，沿反对角线来回走，最终把右下角高频排到队尾。
const std::array<int, 64> kZigZagOrder = {
    0,  1,  8,  16, 9,  2,  3,  10,
    17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63};

std::array<int, 64> ZigZagScan(const Block8d& quantized) {
    std::array<int, 64> zz{};
    for (int k = 0; k < 64; ++k) {
        int idx = kZigZagOrder[k];
        int row = idx / 8;
        int col = idx % 8;
        zz[k] = static_cast<int>(std::lround(quantized[row][col]));
    }
    return zz;
}

std::vector<RleSymbol> RunLengthEncodeAc(const std::array<int, 64>& zz) {
    std::vector<RleSymbol> out;
    int run = 0;  // 当前累计的前导 0 个数
    for (int k = 1; k < 64; ++k) {
        if (zz[k] == 0) {
            ++run;
            continue;
        }
        // 遇到非零：run 若 >= 16，先补足 ZRL（每个代表 16 连 0）。
        while (run >= 16) {
            out.push_back({15, 0});  // ZRL
            run -= 16;
        }
        out.push_back({run, zz[k]});
        run = 0;
    }
    // 扫描到底仍有尾部 0 未输出 -> EOB。
    if (run > 0) out.push_back({0, 0});  // EOB
    return out;
}

std::array<int, 64> RebuildZigZag(int dc, const std::vector<RleSymbol>& ac) {
    std::array<int, 64> zz{};
    zz[0] = dc;
    int k = 1;
    for (const RleSymbol& s : ac) {
        if (s.run == 0 && s.value == 0) break;   // EOB：其余全 0
        if (s.run == 15 && s.value == 0) {        // ZRL：跳过 16 个 0
            k += 16;
            continue;
        }
        k += s.run;       // 跳过 run 个 0
        if (k >= 64) break;
        zz[k] = s.value;  // 放置非零值
        ++k;
    }
    return zz;
}

bool DumpZigZag(const std::array<int, 64>& zz, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    for (int k = 0; k < 64; ++k) {
        f << zz[k];
        f << (k + 1 == 64 ? '\n' : ' ');
    }
    return true;
}

}  // namespace cfs
