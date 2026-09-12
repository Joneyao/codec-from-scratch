// jfif_writer.h — 把量化表 / 霍夫曼表 / 帧头 / 熵编码数据组装成合法的 .jpg 文件
//                 （JPEG 编码第五步，也是收尾的一步）。
//
// 前四步（A1-A4）只是把像素揉成了一串 bit：RGB→YCbCr→DCT→量化→熵编码。
// 但这串 bit 直接存盘，任何查看器都打不开——它不知道图多大、用了哪张量化表、
// 哪张霍夫曼表。JFIF 文件格式就是给这串 bit 加上"说明书"：一段段带标记(marker)
// 的头部，把解码所需的全部参数写清楚。
//
// 文件骨架（baseline / 顺序 DCT，见 ITU-T T.81 B.2 与 JFIF 规范）：
//   SOI  (FFD8)                 文件开始
//   APP0 (FFE0)  JFIF 标识/版本/密度
//   DQT  (FFDB)  量化表（亮度 + 色度，表内按 zig-zag 顺序存）
//   SOF0 (FFC0)  帧头：精度/宽高/分量数/每分量的抽样因子与量化表号
//   DHT  (FFC4)  霍夫曼表（DC/AC × 亮度/色度 共 4 张，BITS + HUFFVAL 格式）
//   SOS  (FFDA)  扫描头 + 紧接其后的熵编码数据（0xFF 要做 byte stuffing）
//   EOI  (FFD9)                 文件结束
//
// 每个段（除 SOI/EOI）都是：marker(2B) + 段长 Lp(2B，含自身但不含 marker) + 数据。
// marker 与段长都是大端（big-endian，T.81 B.1.1.4）。
#ifndef CODEC_FROM_SCRATCH_JPEG_JFIF_WRITER_H
#define CODEC_FROM_SCRATCH_JPEG_JFIF_WRITER_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "huffman_encode.h"  // HuffTable / BITS+HUFFVAL 访问器
#include "quantize.h"        // QuantTable

namespace cfs {

// 描述一个图像分量在帧头/扫描头里需要的参数（T.81 B.2.2 / B.2.3）。
struct ComponentSpec {
    uint8_t id;          // 分量标识（Y=1, Cb=2, Cr=3，JFIF 惯例）
    uint8_t h_sample;    // 水平抽样因子 Hi
    uint8_t v_sample;    // 垂直抽样因子 Vi
    uint8_t quant_id;    // 该分量用哪张量化表（0=亮度表, 1=色度表）
    uint8_t dc_table;    // DC 霍夫曼表号（低 4 位有效）
    uint8_t ac_table;    // AC 霍夫曼表号
};

// 组装一个完整 JFIF 文件所需的全部信息。
struct JfifParams {
    int width = 0;
    int height = 0;
    std::vector<ComponentSpec> components;   // 顺序即 SOF/SOS 里的写入顺序
    QuantTable luma_quant;                    // 已按 quality 缩放好的量化表
    QuantTable chroma_quant;
    std::vector<uint8_t> scan_data;           // 熵编码后的原始 bit 流（未做 stuffing）
};

// 单独写各标记段的低层函数（供单元测试逐段核对）。写入到 out 尾部。
void WriteSoi(std::vector<uint8_t>& out);
void WriteEoi(std::vector<uint8_t>& out);
void WriteApp0(std::vector<uint8_t>& out);
// 写一张 DQT：table_id 0/1，量化表按 zig-zag 顺序展开成 64 字节。
void WriteDqt(std::vector<uint8_t>& out, const QuantTable& q, uint8_t table_id);
// 写 SOF0：baseline 帧头。
void WriteSof0(std::vector<uint8_t>& out, int width, int height,
               const std::vector<ComponentSpec>& comps);
// 写一张 DHT：class(0=DC,1=AC) / id / BITS(16) / HUFFVAL。
void WriteDht(std::vector<uint8_t>& out, uint8_t table_class, uint8_t table_id,
              const std::array<uint8_t, 16>& bits,
              const std::vector<uint8_t>& huffval);
// 写 SOS 扫描头（不含其后的熵数据）。
void WriteSos(std::vector<uint8_t>& out,
              const std::vector<ComponentSpec>& comps);

// 把熵数据里的每个 0xFF 后面塞一个 0x00（byte stuffing，T.81 F.1.2.3），
// 追加到 out 尾部。
void AppendStuffedScanData(std::vector<uint8_t>& out,
                           const std::vector<uint8_t>& scan_data);

// 一步到位：按标准顺序拼出整个文件字节流。
std::vector<uint8_t> AssembleJfif(const JfifParams& p);

// 便捷：把字节流写盘。
bool WriteFile(const std::string& path, const std::vector<uint8_t>& bytes);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_JPEG_JFIF_WRITER_H
