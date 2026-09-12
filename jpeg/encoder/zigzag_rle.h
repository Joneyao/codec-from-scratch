// zigzag_rle.h — Zig-Zag 扫描 + DC 差分 + AC 游程编码（JPEG 熵编码前半段）
//
// 量化（上一篇）把 8x8 块里的大量高频系数压成了 0，但这些 0 散落在矩阵右下角。
// Zig-Zag 扫描（ITU-T T.81 Figure A.6 / 8.5.8）按"之字形"从低频走到高频，
// 把这些 0 全部排到一维序列的尾部——连成一长串，游程编码才好偷懒。
//
// 拉直成 64 元素一维序列后：
//   - 第 0 个是 DC 系数，做差分（DPCM）：当前块 DC 减前一块 DC（T.81 F.1.2.1）。
//   - 后 63 个是 AC 系数，做游程编码：记 (前面连续 0 的个数 run, 非零值 value)。
//     整块尾部若全是 0，用一个 EOB（End Of Block）标记提前结束（T.81 F.1.2.2）。
#ifndef CODEC_FROM_SCRATCH_JPEG_ZIGZAG_RLE_H
#define CODEC_FROM_SCRATCH_JPEG_ZIGZAG_RLE_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "dct8x8.h"  // Block8d

namespace cfs {

// T.81 Figure A.6 的 Zig-Zag 扫描顺序：zigzag[k] = 一维序列第 k 个位置
// 对应的"行主序索引"(row*8+col)。zigzag[0]=0（即 [0][0]，DC）。
extern const std::array<int, 64> kZigZagOrder;

// 把量化后的 8x8 块按 Zig-Zag 顺序拉成 64 元素一维序列。
// 输入系数已是整数（用 Block8d 存放），输出取整。
std::array<int, 64> ZigZagScan(const Block8d& quantized);

// 一个 AC 游程符号：run 个 0 之后跟一个非零系数 value。
// 特殊符号：{0,0} 表示 EOB（块尾全 0）；{15,0} 表示 ZRL（16 连 0，见 F.1.2.2）。
struct RleSymbol {
    int run;    // 前导 0 的个数（0..15）
    int value;  // 非零系数值（EOB/ZRL 时为 0）
};

// 对 63 个 AC 系数（zigzag[1..63]）做游程编码。
// 规则（T.81 F.1.2.2）：
//   - 累计前导 0；遇非零则吐出 {run, value}，run 清零。
//   - run 达到 16 先吐一个 ZRL{15,0}（因为 run 字段只有 4 bit）。
//   - 扫描到末尾若仍有未输出的尾部 0，吐一个 EOB{0,0}。
std::vector<RleSymbol> RunLengthEncodeAc(const std::array<int, 64>& zz);

// 从 (run,value) 序列 + 首个 DC 值重建原始 64 元素 zigzag 序列，
// 供单元测试验证编码可逆。dc 是该块真实 DC（非差分）。
std::array<int, 64> RebuildZigZag(int dc, const std::vector<RleSymbol>& ac);

// 导出一维 zigzag 序列（64 个整数，一行）供画图。
bool DumpZigZag(const std::array<int, 64>& zz, const std::string& path);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_JPEG_ZIGZAG_RLE_H
