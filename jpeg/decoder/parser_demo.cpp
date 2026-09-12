// parser_demo.cpp — 解析一张真实 .jpg，把 marker 序列、量化表、尺寸、抽样因子、
// 霍夫曼表结构全部 dump 出来，并导出配图所需的真实数据（chart_data/）。
//
// 用法：./parser_demo <input.jpg> [chart_data_dir]
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#include "jfif_parser.h"

namespace {

void DumpConsole(const std::string& path, const cfs::JpegHeader& h) {
    std::printf("=== 解析 %s ===\n", path.c_str());
    std::printf("尺寸: %d x %d, 精度: %d bit\n", h.width, h.height, h.precision);
    std::printf("重启间隔(DRI): %d %s\n", h.restart_interval,
                h.restart_interval ? "(有重启标记)" : "(无)");
    std::printf("EXIF(APP1): %s\n", h.has_exif ? "有" : "无");

    std::printf("\n-- marker 序列 (offset | marker | name | len) --\n");
    for (const auto& m : h.markers) {
        std::printf("  %6zu  FF%02X  %-12s  len=%d\n", m.offset, m.code,
                    m.name.c_str(), m.length);
    }

    std::printf("\n-- 帧分量 (SOF0)：%zu 个 --\n", h.frame_components.size());
    for (const auto& c : h.frame_components) {
        std::printf("  分量 id=%d  抽样 %dx%d  量化表#%d\n", c.id, c.h_sample,
                    c.v_sample, c.quant_id);
    }

    std::printf("\n-- 扫描分量 (SOS)：%zu 个 --\n", h.scan_components.size());
    for (const auto& c : h.scan_components) {
        std::printf("  分量 id=%d  DC表#%d  AC表#%d\n", c.id, c.dc_table,
                    c.ac_table);
    }

    std::printf("\n-- 量化表 (DQT)：%zu 张 --\n", h.quant_tables.size());
    for (const auto& q : h.quant_tables) {
        std::printf("  量化表#%d (precision=%d)：\n", q.table_id, q.precision);
        for (int r = 0; r < 8; ++r) {
            std::printf("    ");
            for (int c = 0; c < 8; ++c) std::printf("%4d", q.table[r][c]);
            std::printf("\n");
        }
    }

    std::printf("\n-- 霍夫曼表 (DHT)：%zu 张 --\n", h.huffman_tables.size());
    for (const auto& ht : h.huffman_tables) {
        std::printf("  %s 表#%d：BITS=[", ht.table_class == 0 ? "DC" : "AC",
                    ht.table_id);
        int total = 0;
        for (int i = 0; i < 16; ++i) {
            std::printf("%d%s", ht.bits[i], i == 15 ? "" : ",");
            total += ht.bits[i];
        }
        std::printf("]  共 %d 个符号\n", total);
    }

    std::printf("\n熵编码数据: 偏移 %zu, 长度 %zu 字节\n", h.scan_data_offset,
                h.scan_data_length);
}

// 导出配图数据到 chart_data/ 目录，供 gen_charts.py 读取绘图。
void ExportChartData(const std::string& dir, const std::string& tag,
                     const cfs::JpegHeader& h) {
    // 1) marker 序列（offset code name len），画时间线用。
    {
        std::ofstream f(dir + "/markers_" + tag + ".txt");
        for (const auto& m : h.markers) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%02X", m.code);
            f << m.offset << " " << buf << " " << m.name << " " << m.length
              << "\n";
        }
    }
    // 2) 第一张量化表（8x8 矩阵），画热力图用。
    if (!h.quant_tables.empty()) {
        std::ofstream f(dir + "/quant0_" + tag + ".txt");
        const auto& q = h.quant_tables[0];
        for (int r = 0; r < 8; ++r) {
            for (int c = 0; c < 8; ++c)
                f << q.table[r][c] << (c == 7 ? '\n' : ' ');
        }
    }
    // 3) 每张霍夫曼表的 BITS 数组（16 个数），画码长分布柱状图用。
    {
        std::ofstream f(dir + "/huff_bits_" + tag + ".txt");
        for (const auto& ht : h.huffman_tables) {
            f << (ht.table_class == 0 ? "DC" : "AC") << (int)ht.table_id;
            for (int i = 0; i < 16; ++i) f << " " << (int)ht.bits[i];
            f << "\n";
        }
    }
    // 4) 概要（尺寸/抽样/DRI/EXIF），文章正文与图注引用。
    {
        std::ofstream f(dir + "/summary_" + tag + ".txt");
        f << "width " << h.width << "\n";
        f << "height " << h.height << "\n";
        f << "restart_interval " << h.restart_interval << "\n";
        f << "has_exif " << (h.has_exif ? 1 : 0) << "\n";
        f << "num_components " << h.frame_components.size() << "\n";
        f << "num_quant_tables " << h.quant_tables.size() << "\n";
        f << "num_huff_tables " << h.huffman_tables.size() << "\n";
        f << "num_markers " << h.markers.size() << "\n";
        f << "scan_data_length " << h.scan_data_length << "\n";
        if (!h.frame_components.empty()) {
            const auto& c0 = h.frame_components[0];
            f << "comp0_sampling " << (int)c0.h_sample << "x"
              << (int)c0.v_sample << "\n";
        }
    }
    std::printf("\n配图数据已导出到 %s/ (tag=%s)\n", dir.c_str(), tag.c_str());
}

std::string BaseTag(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    size_t dot = path.find_last_of('.');
    size_t start = (slash == std::string::npos) ? 0 : slash + 1;
    size_t len = (dot == std::string::npos || dot < start)
                     ? std::string::npos
                     : dot - start;
    return path.substr(start, len);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "用法: %s <input.jpg> [chart_data_dir]\n", argv[0]);
        return 1;
    }
    std::string path = argv[1];
    cfs::ParseResult r = cfs::ParseJpegFile(path);
    if (!r.ok) {
        std::fprintf(stderr, "解析失败: %s\n", r.error.c_str());
        return 2;
    }
    DumpConsole(path, r.header);
    if (argc >= 3) ExportChartData(argv[2], BaseTag(path), r.header);
    return 0;
}
