// test_jfif.cpp — JFIF 标记段字节级单元测试：核对 marker、段长字段、
//                 量化表 zig-zag 展开、byte stuffing。
#include <cstdio>
#include <vector>

#include "jfif_writer.h"
#include "quantize.h"

namespace {
int g_failed = 0;
void Check(bool c, const char* m) {
    std::printf(c ? "[ok]   %s\n" : "[FAIL] %s\n", m);
    if (!c) ++g_failed;
}
// 大端读 16 bit。
uint16_t U16(const std::vector<uint8_t>& b, size_t i) {
    return static_cast<uint16_t>((b[i] << 8) | b[i + 1]);
}
}  // namespace

int main() {
    // 1. SOI / EOI 就是两字节 marker。
    {
        std::vector<uint8_t> b;
        cfs::WriteSoi(b);
        Check(b.size() == 2 && b[0] == 0xFF && b[1] == 0xD8, "SOI = FFD8");
        b.clear();
        cfs::WriteEoi(b);
        Check(b.size() == 2 && b[0] == 0xFF && b[1] == 0xD9, "EOI = FFD9");
    }

    // 2. APP0：marker FFE0，段长 16，标识串 "JFIF\0"。
    {
        std::vector<uint8_t> b;
        cfs::WriteApp0(b);
        Check(b[0] == 0xFF && b[1] == 0xE0, "APP0 marker = FFE0");
        Check(U16(b, 2) == 16, "APP0 length = 16");
        Check(b[4] == 'J' && b[5] == 'F' && b[6] == 'I' && b[7] == 'F' &&
                  b[8] == 0x00,
              "APP0 identifier = JFIF\\0");
        Check(b.size() == 2 + 16, "APP0 total bytes = marker + Lp");
    }

    // 3. DQT：marker FFDB，段长 = 2+1+64 = 67，Pq/Tq 字节正确，
    //    且表内按 zig-zag 顺序展开（第 2 个元素应是 kBaseLuma[0][1]=11）。
    {
        std::vector<uint8_t> b;
        cfs::WriteDqt(b, cfs::kBaseLuma, 0);
        Check(b[0] == 0xFF && b[1] == 0xDB, "DQT marker = FFDB");
        Check(U16(b, 2) == 67, "DQT length = 67 (2+1+64)");
        Check(b[4] == 0x00, "DQT Pq/Tq: 8-bit, table 0");
        // zig-zag: k=0 -> [0][0]=16, k=1 -> [0][1]=11, k=2 -> [1][0]=12。
        Check(b[5] == 16, "DQT[0] = luma[0][0] = 16");
        Check(b[6] == 11, "DQT[1] = luma[0][1] = 11 (zigzag order)");
        Check(b[7] == 12, "DQT[2] = luma[1][0] = 12 (zigzag order)");
        // 色度表 id=1。
        std::vector<uint8_t> c;
        cfs::WriteDqt(c, cfs::kBaseChroma, 1);
        Check(c[4] == 0x01, "chroma DQT Pq/Tq: table 1");
    }

    // 4. SOF0：marker FFC0，3 分量段长 = 2+1+2+2+1+9 = 17，宽高正确。
    {
        std::vector<uint8_t> b;
        std::vector<cfs::ComponentSpec> comps = {
            {1, 2, 2, 0, 0, 0}, {2, 1, 1, 1, 1, 1}, {3, 1, 1, 1, 1, 1}};
        cfs::WriteSof0(b, 640, 480, comps);
        Check(b[0] == 0xFF && b[1] == 0xC0, "SOF0 marker = FFC0");
        Check(U16(b, 2) == 17, "SOF0 length = 17 (3 components)");
        Check(b[4] == 8, "SOF0 precision = 8");
        Check(U16(b, 5) == 480, "SOF0 height = 480");
        Check(U16(b, 7) == 640, "SOF0 width = 640");
        Check(b[9] == 3, "SOF0 component count = 3");
        Check(b[10] == 1 && b[11] == 0x22 && b[12] == 0,
              "SOF0 Y: id=1, sampling 2x2, quant table 0");
    }

    // 5. DHT：marker FFC4，段长 = 2+1+16+sum(BITS)。用亮度 DC 表核对。
    {
        std::vector<uint8_t> b;
        cfs::WriteDht(b, 0, 0, cfs::StdLumaDcBits(), cfs::StdLumaDcVals());
        Check(b[0] == 0xFF && b[1] == 0xC4, "DHT marker = FFC4");
        size_t nval = cfs::StdLumaDcVals().size();
        Check(U16(b, 2) == 2 + 1 + 16 + nval, "DHT length = 2+1+16+HUFFVAL");
        Check(b[4] == 0x00, "DHT TcTh: DC class, table 0");
    }

    // 6. SOS：marker FFDA，3 分量段长 = 2+1+2*3+3 = 12，Ss/Se/AhAl 正确。
    {
        std::vector<uint8_t> b;
        std::vector<cfs::ComponentSpec> comps = {
            {1, 2, 2, 0, 0, 0}, {2, 1, 1, 1, 1, 1}, {3, 1, 1, 1, 1, 1}};
        cfs::WriteSos(b, comps);
        Check(b[0] == 0xFF && b[1] == 0xDA, "SOS marker = FFDA");
        Check(U16(b, 2) == 12, "SOS length = 12 (3 components)");
        Check(b[4] == 3, "SOS scan component count = 3");
        size_t n = b.size();
        Check(b[n - 3] == 0x00 && b[n - 2] == 0x3F && b[n - 1] == 0x00,
              "SOS Ss=0 Se=63 AhAl=0 (baseline)");
    }

    // 7. byte stuffing：每个 0xFF 后必须补一个 0x00。
    {
        std::vector<uint8_t> scan = {0x12, 0xFF, 0x34, 0xFF, 0xFF};
        std::vector<uint8_t> out;
        cfs::AppendStuffedScanData(out, scan);
        // 期望：12 FF 00 34 FF 00 FF 00
        std::vector<uint8_t> want = {0x12, 0xFF, 0x00, 0x34,
                                     0xFF, 0x00, 0xFF, 0x00};
        Check(out == want, "byte stuffing inserts 0x00 after every 0xFF");
    }

    // 8. AssembleJfif：整文件以 FFD8 开头、FFD9 结尾。
    {
        cfs::JfifParams p;
        p.width = 16;
        p.height = 16;
        p.luma_quant = cfs::ScaleTable(cfs::kBaseLuma, 90);
        p.chroma_quant = cfs::ScaleTable(cfs::kBaseChroma, 90);
        p.scan_data = {0xAB, 0xFF, 0xCD};
        p.components = {
            {1, 2, 2, 0, 0, 0}, {2, 1, 1, 1, 1, 1}, {3, 1, 1, 1, 1, 1}};
        std::vector<uint8_t> f = cfs::AssembleJfif(p);
        Check(f[0] == 0xFF && f[1] == 0xD8, "file starts with SOI FFD8");
        Check(f[f.size() - 2] == 0xFF && f[f.size() - 1] == 0xD9,
              "file ends with EOI FFD9");
    }

    if (g_failed == 0) {
        std::printf("\nAll JFIF writer tests passed.\n");
        return 0;
    }
    std::printf("\n%d test(s) FAILED.\n", g_failed);
    return 1;
}
