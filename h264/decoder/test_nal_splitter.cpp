// test_nal_splitter.cpp — 校验 Annex B 切分与 Emulation Prevention 还原。
// 分两部分：
//   1) 构造字节流的单元测试（不依赖外部文件）：边界切分、3/4 字节起始码、
//      00 00 03 00 → 00 00 00 的转义还原。
//   2) 解析真实 test.h264：第一个 NAL 是 SPS(type7)，流中能找到 IDR(type5)。
//
// 用自定义 CHECK 而非 assert（Release 下 NDEBUG 会消掉 assert）。
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "nal_splitter.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "CHECK failed: %s (line %d)\n", #cond, \
                         __LINE__);                                     \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

// 测试 1：单纯的 emulation prevention 还原。
// 输入 header(0x67=SPS) + 00 00 03 00 + 00 00 03 01，
// 还原后应为 67 + 00 00 00 + 00 00 01，吞掉 2 个 03。
void TestUnescape() {
    std::vector<uint8_t> nal = {0x67, 0x00, 0x00, 0x03, 0x00,
                                0x00, 0x00, 0x03, 0x01};
    int removed = 0;
    std::vector<uint8_t> rbsp =
        cfs::UnescapeRbsp(nal.data(), nal.size(), &removed);

    std::vector<uint8_t> expect = {0x67, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x01};
    CHECK(removed == 2);
    CHECK(rbsp == expect);

    // 边界：00 00 03 后面若跟 > 0x03 的字节，则 03 不是转义字节，保留。
    // 这种序列在合法码流里不该出现，但还原逻辑要按 7.4.1 忠实处理。
    std::vector<uint8_t> nal2 = {0x00, 0x00, 0x03, 0xFF};
    int removed2 = 0;
    std::vector<uint8_t> rbsp2 =
        cfs::UnescapeRbsp(nal2.data(), nal2.size(), &removed2);
    CHECK(removed2 == 0);
    CHECK(rbsp2 == nal2);
}

// 测试 2：起始码切分。构造三个 NAL：
//   [00 00 01] 67 ...        3 字节起始码 + SPS
//   [00 00 00 01] 68 ...     4 字节起始码 + PPS
//   [00 00 01] 65 ...        3 字节起始码 + IDR
// 验证切出 3 个单元、类型正确、起始码长度识别正确、payload 里的转义被还原。
void TestSplit() {
    std::vector<uint8_t> stream = {
        0x00, 0x00, 0x01, 0x67, 0x42, 0x00,              // SPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xCE,              // PPS(4字节起始码)
        0x00, 0x00, 0x01, 0x65, 0x00, 0x00, 0x03, 0x00,  // IDR + 一个转义字节
    };
    cfs::NalSplitResult r = cfs::SplitAnnexB(stream);
    CHECK(r.ok);
    CHECK(r.units.size() == 3);
    if (r.units.size() == 3) {
        CHECK(r.units[0].nal_unit_type == cfs::kNalSps);
        CHECK(r.units[0].start_code_len == 3);
        CHECK(r.units[0].nal_ref_idc == 3);      // 0x67 >> 5 = 011b = 3
        CHECK(r.units[0].forbidden_zero_bit == 0);

        CHECK(r.units[1].nal_unit_type == cfs::kNalPps);
        CHECK(r.units[1].start_code_len == 4);

        CHECK(r.units[2].nal_unit_type == cfs::kNalSliceIdr);
        CHECK(r.units[2].emulation_bytes_removed == 1);
    }
    CHECK(r.total_emulation_bytes_removed == 1);
}

// 测试 3：解析真实 test.h264。
void TestRealStream(const std::string& path) {
    std::vector<uint8_t> bytes = cfs::ReadFileBytes(path);
    if (bytes.empty()) {
        std::fprintf(stderr, "跳过真实流测试：读不到 %s\n", path.c_str());
        return;
    }
    cfs::NalSplitResult r = cfs::SplitAnnexB(bytes);
    CHECK(r.ok);
    CHECK(!r.units.empty());

    // 第一个 NAL 应该是 SPS（ffmpeg 生成的 Annex B 流以 SPS 打头）。
    CHECK(r.units.front().nal_unit_type == cfs::kNalSps);
    CHECK(r.units.front().forbidden_zero_bit == 0);

    // 流中应出现 IDR slice(5)、PPS(8)、非 IDR slice(1)。
    bool has_idr = false, has_pps = false, has_nonidr = false;
    for (const auto& u : r.units) {
        if (u.nal_unit_type == cfs::kNalSliceIdr) has_idr = true;
        if (u.nal_unit_type == cfs::kNalPps) has_pps = true;
        if (u.nal_unit_type == cfs::kNalSliceNonIdr) has_nonidr = true;
        // 所有 NAL 的 forbidden_zero_bit 必须为 0（否则说明切分错位）。
        CHECK(u.forbidden_zero_bit == 0);
    }
    CHECK(has_idr);
    CHECK(has_pps);
    CHECK(has_nonidr);

    std::printf("真实流 %s：%zu 个 NAL，吞掉 %d 个转义字节，首个=%s\n",
                path.c_str(), r.units.size(),
                r.total_emulation_bytes_removed,
                r.units.front().TypeName().c_str());
}

}  // namespace

int main(int argc, char** argv) {
    TestUnescape();
    TestSplit();

    // 真实流路径可用命令行覆盖，默认取 samples/test.h264（相对 build 目录）。
    std::string h264_path =
        argc > 1 ? argv[1] : "../../samples/test.h264";
    TestRealStream(h264_path);

    if (g_failures != 0) {
        std::fprintf(stderr, "test_nal_splitter: %d FAILURE(s)\n", g_failures);
        return 1;
    }
    std::printf("test_nal_splitter: 全部通过\n");
    return 0;
}
