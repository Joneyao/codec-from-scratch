// test_sps_pps.cpp — 校验指数哥伦布解码与 SPS/PPS 解析。
// 三部分：
//   1) ue/se 对已知比特串解码正确（对照 ITU-T H.264 表 9-2 / 9-3）。
//   2) MSB-first 读位正确。
//   3) 解真实 test.h264 的 SPS，得 176x144、baseline profile。
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "nal_splitter.h"
#include "rbsp_bit_reader.h"
#include "sps_pps_parser.h"

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

// 把 "010011..." 这样的比特串打包成字节（MSB first，末尾补 0 到整字节）。
std::vector<uint8_t> PackBits(const std::string& bits) {
    std::vector<uint8_t> out;
    uint8_t cur = 0;
    int n = 0;
    for (char c : bits) {
        cur = static_cast<uint8_t>((cur << 1) | (c == '1' ? 1 : 0));
        if (++n == 8) {
            out.push_back(cur);
            cur = 0;
            n = 0;
        }
    }
    if (n > 0) out.push_back(static_cast<uint8_t>(cur << (8 - n)));
    return out;
}

// 测试 1：ue(v) 指数哥伦布。表 9-2：1->0, 010->1, 011->2, 00100->3 ...
void TestUE() {
    struct Case { const char* bits; uint32_t expect; };
    const Case cases[] = {
        {"1", 0}, {"010", 1}, {"011", 2}, {"00100", 3}, {"00101", 4},
        {"00110", 5}, {"00111", 6}, {"0001000", 7}, {"0001001", 8},
        {"0001010", 9},
    };
    for (const auto& c : cases) {
        auto bytes = PackBits(c.bits);
        cfs::RbspBitReader br(bytes.data(), bytes.size());
        uint32_t got = br.ReadUE();
        CHECK(got == c.expect);
        if (got != c.expect) {
            std::fprintf(stderr, "  ue('%s') = %u, expect %u\n",
                         c.bits, got, c.expect);
        }
    }
}

// 测试 2：se(v) 有符号。表 9-3：codeNum 0->0,1->+1,2->-1,3->+2,4->-2。
// 对应比特串（先 ue 得 codeNum，再映射）。
void TestSE() {
    struct Case { const char* bits; int32_t expect; };
    const Case cases[] = {
        {"1", 0},        // k=0 -> 0
        {"010", 1},      // k=1 -> +1
        {"011", -1},     // k=2 -> -1
        {"00100", 2},    // k=3 -> +2
        {"00101", -2},   // k=4 -> -2
        {"00110", 3},    // k=5 -> +3
    };
    for (const auto& c : cases) {
        auto bytes = PackBits(c.bits);
        cfs::RbspBitReader br(bytes.data(), bytes.size());
        int32_t got = br.ReadSE();
        CHECK(got == c.expect);
        if (got != c.expect) {
            std::fprintf(stderr, "  se('%s') = %d, expect %d\n",
                         c.bits, got, c.expect);
        }
    }
}

// 测试 3：MSB-first 读位。字节 0xB4 = 1011 0100。
void TestReadBits() {
    std::vector<uint8_t> bytes = {0xB4, 0x2C};  // 1011 0100 0010 1100
    cfs::RbspBitReader br(bytes.data(), bytes.size());
    CHECK(br.ReadBits(4) == 0xB);   // 1011
    CHECK(br.ReadBits(4) == 0x4);   // 0100
    CHECK(br.ReadBits(8) == 0x2C);  // 0010 1100
    CHECK(!br.Overrun());
    br.ReadBit();                   // 越界
    CHECK(br.Overrun());
}

// 测试 4：解真实 test.h264 的 SPS/PPS。
void TestRealSps(const std::string& path) {
    std::vector<uint8_t> data = cfs::ReadFileBytes(path);
    if (data.empty()) {
        std::fprintf(stderr, "跳过真实流测试：读不到 %s\n", path.c_str());
        return;
    }
    cfs::NalSplitResult r = cfs::SplitAnnexB(data);
    CHECK(r.ok);

    const cfs::NalUnit* sps_nal = nullptr;
    const cfs::NalUnit* pps_nal = nullptr;
    for (const auto& u : r.units) {
        if (!sps_nal && u.nal_unit_type == cfs::kNalSps) sps_nal = &u;
        if (!pps_nal && u.nal_unit_type == cfs::kNalPps) pps_nal = &u;
    }
    CHECK(sps_nal != nullptr);
    if (!sps_nal) return;

    cfs::Sps s = cfs::ParseSps(sps_nal->rbsp);
    CHECK(s.ok);
    // 与 ffprobe 报告一致：176x144, baseline(66)。
    CHECK(s.profile_idc == 66);
    CHECK(s.Width() == 176);
    CHECK(s.Height() == 144);
    // 176 = 11 个宏块 * 16；144 = 9 个宏块 * 16（逐行帧）。
    CHECK(s.PicWidthInMbs() == 11);
    CHECK(s.frame_mbs_only_flag == true);

    std::printf("真实 SPS：profile=%s level=%.1f 分辨率=%ux%u "
                "(%u个宏块宽 x %u个宏块高)\n",
                cfs::ProfileName(s.profile_idc), s.level_idc / 10.0,
                s.Width(), s.Height(), s.PicWidthInMbs(),
                s.FrameHeightInMbs());

    if (pps_nal) {
        cfs::Pps p = cfs::ParsePps(pps_nal->rbsp);
        CHECK(p.ok);
        // baseline profile 用 CAVLC，不是 CABAC。
        CHECK(p.entropy_coding_mode_flag == false);
        std::printf("真实 PPS：熵编码=%s 初始QP=%d\n",
                    p.EntropyName(), p.PicInitQp());
    }
}

}  // namespace

int main(int argc, char** argv) {
    TestUE();
    TestSE();
    TestReadBits();

    std::string h264_path =
        argc > 1 ? argv[1] : "../../samples/test.h264";
    TestRealSps(h264_path);

    if (g_failures != 0) {
        std::fprintf(stderr, "test_sps_pps: %d FAILURE(s)\n", g_failures);
        return 1;
    }
    std::printf("test_sps_pps: 全部通过\n");
    return 0;
}
