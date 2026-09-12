// test_jpeg_parse.cpp — JFIF 解析 + 霍夫曼解码表重建的最小单元测试。
//
// 用编码器 A5 产出的 out.jpg 做解析对照（宽高/分量/抽样），再验证：
//   1) 从 BITS/HUFFVAL 建的解码表，能把已知码字解回正确符号；
//   2) 编码表(符号→码字) 与 解码表(码字→符号) 严丝合缝自洽——
//      随便挑符号，用编码端码字喂给解码端，必须解回同一个符号。
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "bitreader.h"
#include "huffman_decode.h"
#include "huffman_encode.h"  // 复用编码端标准表与 BuildHuffTable
#include "jfif_parser.h"

namespace {
int g_failed = 0;
void Check(bool c, const char* m) {
    std::printf(c ? "[ok]   %s\n" : "[FAIL] %s\n", m);
    if (!c) ++g_failed;
}

// 用编码表的 (码字,码长) 造一段比特流（MSB first），喂给解码端 DecodeSymbol。
// 这里手动拼 bit，不走 byte stuffing（测试用的都是短码，不会出现 0xFF）。
uint8_t RoundTripSymbol(const cfs::HuffTable& enc, const cfs::HuffDecTable& dec,
                        uint8_t sym, bool* ok) {
    uint32_t code = enc.code[sym];
    int len = enc.len[sym];
    // 把 len 位码字（右对齐）放进一个字节缓冲的高位区，凑够再补 1 填充。
    std::vector<uint8_t> buf;
    uint32_t acc = 0;
    int accbits = 0;
    for (int i = len - 1; i >= 0; --i) {
        acc = (acc << 1) | ((code >> i) & 1u);
        if (++accbits == 8) { buf.push_back(acc & 0xFF); acc = 0; accbits = 0; }
    }
    if (accbits > 0) {  // 末尾补 1（和 T.81 F.1.2.3 一致，避免误当 marker）
        acc = (acc << (8 - accbits)) | ((1u << (8 - accbits)) - 1u);
        buf.push_back(acc & 0xFF);
    }
    // 真实 JFIF 熵数据里 0xFF 会被塞一个 0x00（byte stuffing，T.81 F.1.2.3），
    // 否则解码端会把它误判成 marker。测试要模拟这一步，才和真实码流一致。
    std::vector<uint8_t> stuffed;
    for (uint8_t byte : buf) {
        stuffed.push_back(byte);
        if (byte == 0xFF) stuffed.push_back(0x00);
    }
    cfs::BitReader br(stuffed.data(), stuffed.size());
    return cfs::DecodeSymbol(br, dec, ok);
}
}  // namespace

int main(int argc, char** argv) {
    // 默认路径：从 build 目录相对定位到编码器产出的 out.jpg。
    std::string jpg = (argc >= 2) ? argv[1] : "../../encoder/out.jpg";

    // === 1. 解析 A5 的 out.jpg，核对基本参数 ===
    {
        cfs::ParseResult r = cfs::ParseJpegFile(jpg);
        Check(r.ok, "parse encoder out.jpg succeeds");
        if (r.ok) {
            const cfs::JpegHeader& h = r.header;
            Check(h.width == 256, "width == 256");
            Check(h.height == 256, "height == 256");
            Check(h.frame_components.size() == 3, "component count == 3");
            if (!h.frame_components.empty()) {
                // Y 分量（第一个）4:2:0 时抽样应为 2x2。
                const auto& y = h.frame_components[0];
                Check(y.h_sample == 2 && y.v_sample == 2,
                      "Y sampling factor == 2x2 (4:2:0)");
            }
            // A5 写 4 张霍夫曼表、2 张量化表。
            Check(h.huffman_tables.size() == 4, "4 Huffman tables (DHT)");
            Check(h.quant_tables.size() == 2, "2 quant tables (DQT)");
        }
    }

    // === 2. 解码表重建：用标准亮度 DC 表验证已知码字 ===
    {
        // 标准亮度 DC 表：category 0 的码字是 '00'(len2)，category 1 是 '010'(len3)。
        cfs::HuffDecTable dec =
            cfs::BuildDecTable(cfs::StdLumaDcBits(), cfs::StdLumaDcVals());
        // 手拼比特流 "00......"，应解出符号 0。
        {
            std::vector<uint8_t> buf = {0x3F};  // 00111111
            cfs::BitReader br(buf.data(), buf.size());
            bool ok = false;
            uint8_t s = cfs::DecodeSymbol(br, dec, &ok);
            Check(ok && s == 0, "luma DC code '00' decodes to symbol 0");
        }
        // "010....." 应解出符号 1。
        {
            std::vector<uint8_t> buf = {0x5F};  // 01011111
            cfs::BitReader br(buf.data(), buf.size());
            bool ok = false;
            uint8_t s = cfs::DecodeSymbol(br, dec, &ok);
            Check(ok && s == 1, "luma DC code '010' decodes to symbol 1");
        }
    }

    // === 3. 编解码自洽：编码表的每个码字都能被解码表解回原符号 ===
    {
        const cfs::HuffTable& enc = cfs::StdLumaAcTable();
        cfs::HuffDecTable dec =
            cfs::BuildDecTable(cfs::StdLumaAcBits(), cfs::StdLumaAcVals());
        bool all_match = true;
        int checked = 0;
        for (int sym = 0; sym < 256; ++sym) {
            if (enc.len[sym] == 0) continue;  // 该符号未定义
            bool ok = false;
            uint8_t got = RoundTripSymbol(enc, dec, (uint8_t)sym, &ok);
            if (!ok || got != sym) { all_match = false; break; }
            ++checked;
        }
        Check(all_match && checked > 100,
              "every luma-AC codeword round-trips enc->dec correctly");
    }

    // === 4. 解析真实相机风格图（若提供第二个路径）：EXIF/非16倍数尺寸 ===
    if (argc >= 3) {
        cfs::ParseResult r = cfs::ParseJpegFile(argv[2]);
        Check(r.ok, "parse real camera-style jpg succeeds");
        if (r.ok) {
            Check(r.header.has_exif, "real jpg has EXIF (APP1)");
            Check(r.header.width % 16 != 0 || r.header.height % 16 != 0,
                  "real jpg dimension not a multiple of 16");
        }
    }

    std::printf(g_failed == 0 ? "ALL TESTS PASSED\n"
                              : "%d TEST(S) FAILED\n",
                g_failed);
    return g_failed == 0 ? 0 : 1;
}
