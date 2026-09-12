// jfif_writer.cpp — 各标记段的字节级组装，照 ITU-T T.81 Annex B 与 JFIF 规范写。
#include "jfif_writer.h"

#include <fstream>

#include "zigzag_rle.h"  // kZigZagOrder

namespace cfs {

namespace {

// 大端写入一个 16 bit 值（marker、段长、宽高都是 big-endian，T.81 B.1.1.4）。
void PushU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

}  // namespace

void WriteSoi(std::vector<uint8_t>& out) {
    out.push_back(0xFF);
    out.push_back(0xD8);  // SOI
}

void WriteEoi(std::vector<uint8_t>& out) {
    out.push_back(0xFF);
    out.push_back(0xD9);  // EOI
}

// APP0：JFIF 标识段（JFIF 规范）。固定 16 字节内容：
//   "JFIF\0" + 版本(1.01) + 密度单位(0) + Xdensity/Ydensity(1) + 缩略图 0x0。
void WriteApp0(std::vector<uint8_t>& out) {
    out.push_back(0xFF);
    out.push_back(0xE0);            // APP0 marker
    PushU16(out, 16);               // Lp = 段长（含自身两字节）
    out.push_back('J');
    out.push_back('F');
    out.push_back('I');
    out.push_back('F');
    out.push_back(0x00);            // 标识串结束符
    out.push_back(0x01);            // 主版本 1
    out.push_back(0x01);            // 次版本 01
    out.push_back(0x00);            // 密度单位：0 = 无单位（纯宽高比）
    PushU16(out, 1);                // X density
    PushU16(out, 1);                // Y density
    out.push_back(0x00);            // 缩略图宽 0
    out.push_back(0x00);            // 缩略图高 0
}

// DQT：一张量化表。Pq(精度)=0 表示 8 bit，Tq=table_id。
// 表内 64 个步长必须按 zig-zag 顺序写（T.81 B.2.4.1），不是行主序！
void WriteDqt(std::vector<uint8_t>& out, const QuantTable& q,
              uint8_t table_id) {
    out.push_back(0xFF);
    out.push_back(0xDB);            // DQT marker
    PushU16(out, 2 + 1 + 64);       // Lp = 2 + 1(Pq/Tq) + 64 步长
    out.push_back(static_cast<uint8_t>((0 << 4) | (table_id & 0x0F)));
    for (int k = 0; k < 64; ++k) {
        int idx = kZigZagOrder[k];
        int row = idx / 8;
        int col = idx % 8;
        out.push_back(static_cast<uint8_t>(q[row][col]));
    }
}

// SOF0：baseline 顺序 DCT 帧头（T.81 B.2.2）。
//   段长 = 2 + 1(精度) + 2(高) + 2(宽) + 1(分量数) + 3*Nf。
// 每分量 3 字节：Ci + (Hi<<4|Vi) + Tqi。
void WriteSof0(std::vector<uint8_t>& out, int width, int height,
               const std::vector<ComponentSpec>& comps) {
    out.push_back(0xFF);
    out.push_back(0xC0);            // SOF0 marker
    uint16_t len = static_cast<uint16_t>(
        2 + 1 + 2 + 2 + 1 + 3 * comps.size());
    PushU16(out, len);
    out.push_back(8);               // 采样精度 8 bit
    PushU16(out, static_cast<uint16_t>(height));
    PushU16(out, static_cast<uint16_t>(width));
    out.push_back(static_cast<uint8_t>(comps.size()));  // 分量数 Nf
    for (const ComponentSpec& c : comps) {
        out.push_back(c.id);
        out.push_back(static_cast<uint8_t>((c.h_sample << 4) | c.v_sample));
        out.push_back(c.quant_id);
    }
}

// DHT：一张霍夫曼表（T.81 B.2.4.2）。
//   TcTh 字节：高 4 位 table_class(0=DC,1=AC)，低 4 位 table_id。
//   紧跟 16 个 BITS（各码长符号数），再跟 sum(BITS) 个 HUFFVAL。
void WriteDht(std::vector<uint8_t>& out, uint8_t table_class, uint8_t table_id,
              const std::array<uint8_t, 16>& bits,
              const std::vector<uint8_t>& huffval) {
    out.push_back(0xFF);
    out.push_back(0xC4);            // DHT marker
    uint16_t len = static_cast<uint16_t>(2 + 1 + 16 + huffval.size());
    PushU16(out, len);
    out.push_back(static_cast<uint8_t>(
        ((table_class & 0x0F) << 4) | (table_id & 0x0F)));
    for (uint8_t b : bits) out.push_back(b);
    for (uint8_t v : huffval) out.push_back(v);
}

// SOS：扫描头（T.81 B.2.3），其后紧接熵编码数据。
//   段长 = 2 + 1(Ns) + 2*Ns + 3(Ss/Se/AhAl)。
//   每个扫描分量 2 字节：Cs + (Td<<4|Ta)。
//   baseline 顺序扫描：Ss=0, Se=63, Ah=0, Al=0。
void WriteSos(std::vector<uint8_t>& out,
              const std::vector<ComponentSpec>& comps) {
    out.push_back(0xFF);
    out.push_back(0xDA);            // SOS marker
    uint16_t len = static_cast<uint16_t>(2 + 1 + 2 * comps.size() + 3);
    PushU16(out, len);
    out.push_back(static_cast<uint8_t>(comps.size()));  // Ns
    for (const ComponentSpec& c : comps) {
        out.push_back(c.id);
        out.push_back(static_cast<uint8_t>(
            ((c.dc_table & 0x0F) << 4) | (c.ac_table & 0x0F)));
    }
    out.push_back(0x00);            // Ss：谱选择起始
    out.push_back(0x3F);            // Se：谱选择结束 = 63
    out.push_back(0x00);            // Ah/Al：逐次逼近，baseline 全 0
}

// byte stuffing：熵数据里任何 0xFF 后必须补 0x00，否则解码器会把它误当 marker
// （T.81 F.1.2.3）。
void AppendStuffedScanData(std::vector<uint8_t>& out,
                           const std::vector<uint8_t>& scan_data) {
    for (uint8_t b : scan_data) {
        out.push_back(b);
        if (b == 0xFF) out.push_back(0x00);
    }
}

// 按标准顺序拼出完整文件。
std::vector<uint8_t> AssembleJfif(const JfifParams& p) {
    std::vector<uint8_t> out;
    WriteSoi(out);
    WriteApp0(out);
    // 量化表：亮度(id=0) + 色度(id=1)。
    WriteDqt(out, p.luma_quant, 0);
    WriteDqt(out, p.chroma_quant, 1);
    WriteSof0(out, p.width, p.height, p.components);
    // 霍夫曼表：亮度 DC/AC(id=0) + 色度 DC/AC(id=1)。
    WriteDht(out, 0, 0, StdLumaDcBits(), StdLumaDcVals());
    WriteDht(out, 1, 0, StdLumaAcBits(), StdLumaAcVals());
    WriteDht(out, 0, 1, StdChromaDcBits(), StdChromaDcVals());
    WriteDht(out, 1, 1, StdChromaAcBits(), StdChromaAcVals());
    WriteSos(out, p.components);
    AppendStuffedScanData(out, p.scan_data);
    WriteEoi(out);
    return out;
}

bool WriteFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

}  // namespace cfs
