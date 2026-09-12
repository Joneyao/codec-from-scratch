// jfif_parser.h — 解析一个真实 .jpg 的文件结构（JPEG 解码第一步）。
//
// 编码器（A5）是"往里写"：把量化表、霍夫曼表、尺寸按 JFIF 规范塞进一个个
// 标记段。解码器反过来"往外读"：扫描这些 marker，把解码所需的参数一段段抠
// 出来，重建成内存里的数据结构。这是解码的镜像起点。
//
// 但解码比编码难在一点：编码器只需产出自己那一种文件，解码器却要读懂"别人写
// 的各种文件"。相机拍的 .jpg 里会冒出编码器根本不生成的东西——EXIF（APP1）、
// 重启间隔（DRI）、非 16 倍数尺寸、和 Annex K 不同的自定义量化表。这个解析器
// 要能一视同仁地跳过不认识的段、解出认识的段。
//
// marker 结构（T.81 B.1.1.3 / B.2）：每个 marker 是 0xFF + 一字节标识。
// 除 SOI(FFD8)/EOI(FFD9)/RSTn(FFD0..7) 是"独立 marker"（后面不带段长）外，
// 其余段都是 marker(2B) + 段长 Lp(2B，大端，含自身两字节但不含 marker) + 数据。
#ifndef CODEC_FROM_SCRATCH_JPEG_DECODER_JFIF_PARSER_H
#define CODEC_FROM_SCRATCH_JPEG_DECODER_JFIF_PARSER_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "huffman_decode.h"  // DecodeHuffTable
#include "quantize.h"        // QuantTable（复用编码器的 8x8 定义）

namespace cfs {

// 一条被扫描到的 marker 记录，供调试 dump 和画"标记时间线"。
struct MarkerRecord {
    size_t offset;        // 该 marker 在文件里的字节偏移（0xFF 的位置）
    uint8_t code;         // marker 第二字节（如 0xE0=APP0, 0xDB=DQT）
    int length;           // 段长（含 Lp 自身两字节）；独立 marker 记 0
    std::string name;     // 人类可读名（SOI/APP0/DQT/...）
};

// SOF0 帧头里描述的一个分量（T.81 B.2.2）。
struct FrameComponent {
    uint8_t id;         // 分量标识（Y 常见为 1）
    uint8_t h_sample;   // 水平抽样因子 Hi
    uint8_t v_sample;   // 垂直抽样因子 Vi
    uint8_t quant_id;   // 该分量用哪张量化表（对应 DQT 的 table_id）
};

// SOS 扫描头里描述的一个分量到霍夫曼表的映射（T.81 B.2.3）。
struct ScanComponent {
    uint8_t id;         // 分量标识（对应 FrameComponent.id）
    uint8_t dc_table;   // DC 霍夫曼表号
    uint8_t ac_table;   // AC 霍夫曼表号
};

// 一个 DHT 里重建出来的霍夫曼表 + 它的身份（class/id）与原始描述。
struct ParsedHuffTable {
    uint8_t table_class;               // 0=DC, 1=AC
    uint8_t table_id;                  // 表号（0/1）
    std::array<uint8_t, 16> bits{};    // BITS：码长 1..16 各有几个符号
    std::vector<uint8_t> huffval;      // HUFFVAL：按码长升序的符号列表
    HuffDecTable table;                // 重建出的解码查表（见 huffman_decode.h）
};

// 一个 DQT 里解析出来的量化表 + 它的表号。
struct ParsedQuantTable {
    uint8_t table_id;    // 对应 FrameComponent.quant_id
    uint8_t precision;   // 0=8bit, 1=16bit（baseline 一般是 0）
    QuantTable table;    // 已反 zig-zag 回 8x8 行主序
};

// 解析一个 .jpg 得到的完整头部信息。
struct JpegHeader {
    int width = 0;
    int height = 0;
    uint8_t precision = 8;                 // 采样精度（bit），baseline=8
    int restart_interval = 0;              // DRI 段给的重启间隔（0=没有）
    bool has_exif = false;                 // 是否见到 APP1/EXIF 段
    std::vector<FrameComponent> frame_components;   // SOF0 的分量列表
    std::vector<ScanComponent> scan_components;     // SOS 的分量→表映射
    std::vector<ParsedQuantTable> quant_tables;     // 所有 DQT
    std::vector<ParsedHuffTable> huffman_tables;    // 所有 DHT
    std::vector<MarkerRecord> markers;              // 按出现顺序的全部 marker
    size_t scan_data_offset = 0;           // 熵编码数据在文件里的起始偏移
    size_t scan_data_length = 0;           // 熵编码数据长度（到 EOI 前）
};

// 解析结果：成功与否 + 出错信息（真实文件解析要能优雅报错，不能崩）。
struct ParseResult {
    bool ok = false;
    std::string error;
    JpegHeader header;
};

// 主入口：读入整个文件字节流，扫描所有 marker，解析各段，返回头部信息。
ParseResult ParseJpeg(const std::vector<uint8_t>& bytes);

// 便捷：从磁盘读文件再解析。
ParseResult ParseJpegFile(const std::string& path);

// 把 marker 标识翻译成人类可读名（供 dump 与画图）。
std::string MarkerName(uint8_t code);

// 读整个文件为字节 vector（失败返回空）。
std::vector<uint8_t> ReadWholeFile(const std::string& path);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_JPEG_DECODER_JFIF_PARSER_H
