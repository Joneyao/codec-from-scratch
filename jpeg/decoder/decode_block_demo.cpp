// decode_block_demo.cpp — 从一张真实 .jpg 解出第一个块，把"比特流→像素块"
// 每一步的 8x8 矩阵全 dump 出来，供文章配图。
//
// 展示的接力顺序（每一步都是 A2-A4 的逆）：
//   熵解码后的量化系数(zigzag) → 反 Zig-Zag 摆回 8x8 → 反量化 → IDCT 后像素。
//
// 只解第一个 MCU 的第一个亮度块就够说明问题：它的 DC 预测值从 0 起，
// 无需先解前面的块，最适合单独拎出来讲清楚流程。
//
// 用法：./decode_block_demo <input.jpg> [chart_data_dir]
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "bitreader.h"
#include "block_reconstruct.h"
#include "entropy_decode.h"
#include "jfif_parser.h"

namespace {

// 从解析结果里找指定 class/id 的霍夫曼解码表。
const cfs::HuffDecTable* FindHuff(const cfs::JpegHeader& h, uint8_t cls,
                                  uint8_t id) {
    for (const auto& t : h.huffman_tables)
        if (t.table_class == cls && t.table_id == id) return &t.table;
    return nullptr;
}

// 从解析结果里找指定 id 的量化表。
const cfs::QuantTable* FindQuant(const cfs::JpegHeader& h, uint8_t id) {
    for (const auto& q : h.quant_tables)
        if (q.table_id == id) return &q.table;
    return nullptr;
}

void DumpIntBlock(const cfs::Block8d& b, const std::string& path) {
    std::ofstream os(path);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            os << static_cast<long>(std::lround(b[y][x])) << (x == 7 ? '\n' : ' ');
}

void DumpU8Block(const cfs::Block8u& b, const std::string& path) {
    std::ofstream os(path);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            os << static_cast<int>(b[y][x]) << (x == 7 ? '\n' : ' ');
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "用法: %s <input.jpg> [chart_data_dir]\n", argv[0]);
        return 1;
    }
    const std::string path = argv[1];

    cfs::ParseResult r = cfs::ParseJpegFile(path);
    if (!r.ok) {
        std::fprintf(stderr, "解析失败: %s\n", r.error.c_str());
        return 2;
    }
    const cfs::JpegHeader& h = r.header;

    if (h.scan_components.empty() || h.frame_components.empty()) {
        std::fprintf(stderr, "缺少扫描/帧分量信息\n");
        return 3;
    }

    // 第一个扫描分量（一般是 Y）：取它的 DC/AC 表号和对应量化表。
    const cfs::ScanComponent& sc0 = h.scan_components[0];
    const cfs::FrameComponent& fc0 = h.frame_components[0];
    const cfs::HuffDecTable* dc_tbl = FindHuff(h, 0, sc0.dc_table);
    const cfs::HuffDecTable* ac_tbl = FindHuff(h, 1, sc0.ac_table);
    const cfs::QuantTable* q_tbl = FindQuant(h, fc0.quant_id);
    if (!dc_tbl || !ac_tbl || !q_tbl) {
        std::fprintf(stderr, "找不到分量 0 需要的霍夫曼/量化表\n");
        return 4;
    }

    // 定位到熵编码数据。解析器只记了偏移，没留原始字节，这里重新读一遍文件。
    std::vector<uint8_t> bytes = cfs::ReadWholeFile(path);
    if (bytes.empty()) {
        std::fprintf(stderr, "重新读文件失败\n");
        return 5;
    }
    const uint8_t* entropy = bytes.data() + h.scan_data_offset;
    size_t entropy_len = h.scan_data_length;

    cfs::BitReader br(entropy, entropy_len);
    int dc_pred = 0;  // 第一个块的 DC 预测值从 0 起（T.81 F.2.1.3.1）。

    std::array<int, 64> zz{};
    if (!cfs::DecodeBlockZigZag(br, *dc_tbl, *ac_tbl, dc_pred, zz)) {
        std::fprintf(stderr, "熵解码第一个块失败\n");
        return 6;
    }

    // 一路还原：反 Zig-Zag -> 反量化 -> IDCT。
    cfs::Block8d quant_block = cfs::InverseZigZag(zz);
    cfs::Block8d dequant_block = cfs::DequantizeBlock(quant_block, *q_tbl);
    cfs::Block8u pixels = cfs::ReconstructPixels(dequant_block);

    // 控制台打印四个阶段，便于肉眼核对。
    std::printf("=== 解码 %s 的第一个亮度块（%dx%d 图，分量#%d）===\n",
                path.c_str(), h.width, h.height, fc0.id);

    std::printf("\n[1] 熵解码后的量化系数（zigzag 一维，前 16 个）:\n  ");
    for (int i = 0; i < 16; ++i) std::printf("%d ", zz[i]);
    std::printf("...\n");

    std::printf("\n[2] 反 Zig-Zag 摆回 8x8（量化系数）:\n");
    for (int y = 0; y < 8; ++y) {
        std::printf("  ");
        for (int x = 0; x < 8; ++x)
            std::printf("%5ld", static_cast<long>(std::lround(quant_block[y][x])));
        std::printf("\n");
    }

    std::printf("\n[3] 反量化后（系数 x 步长）:\n");
    for (int y = 0; y < 8; ++y) {
        std::printf("  ");
        for (int x = 0; x < 8; ++x)
            std::printf("%6ld", static_cast<long>(std::lround(dequant_block[y][x])));
        std::printf("\n");
    }

    std::printf("\n[4] IDCT 后的像素（0-255）:\n");
    for (int y = 0; y < 8; ++y) {
        std::printf("  ");
        for (int x = 0; x < 8; ++x) std::printf("%5d", static_cast<int>(pixels[y][x]));
        std::printf("\n");
    }
    std::printf("\n本块还原后的 DC 系数（未反量化）= %d\n", zz[0]);

    // 导出配图数据。
    if (argc >= 3) {
        const std::string dir = argv[2];
        DumpIntBlock(quant_block, dir + "/stage_quant.txt");
        DumpIntBlock(dequant_block, dir + "/stage_dequant.txt");
        DumpU8Block(pixels, dir + "/stage_pixels.txt");
        // 一维 zigzag 系数（前 24 个），画"比特流→系数"示意用。
        {
            std::ofstream f(dir + "/zigzag_coeffs.txt");
            for (int i = 0; i < 64; ++i) f << zz[i] << (i == 63 ? '\n' : ' ');
        }
        std::printf("\n配图数据已导出到 %s/\n", dir.c_str());
    }
    return 0;
}
