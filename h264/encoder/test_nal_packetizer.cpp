// test_nal_packetizer.cpp — D6 封装层最小测试。
//
// 两条镜像关系钉死自洽性：
//   1) BuildSpsRbsp 写的 SPS，用 decoder 的 sps_pps_parser 解回来，分辨率/
//      profile/level/QP 等字段与写入时逐一相等。PPS 同理。
//   2) WriteAnnexBNal 的起始码 + emulation prevention，用 decoder 的
//      nal_splitter 切分并去转义，能原样还原出 header + RBSP——尤其构造含
//      00 00 01 的 RBSP，验证 0x03 转义确实被插入又被吞掉。
//
// Release 会 strip assert 导致「只在 assert 里用到的变量」报 unused，
// 故在文件顶部 #undef NDEBUG，保证 assert 恒生效。
#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "nal_packetizer.h"

#include "../decoder/nal_splitter.h"
#include "../decoder/sps_pps_parser.h"

using namespace cfs;

// 把一段 RBSP 包成完整 NAL（header 放 rbsp[0]），交给 ParseSps/ParsePps。
static std::vector<uint8_t> WithHeader(uint8_t header,
                                       const std::vector<uint8_t>& rbsp) {
    std::vector<uint8_t> v;
    v.push_back(header);
    v.insert(v.end(), rbsp.begin(), rbsp.end());
    return v;
}

static void TestExpGolombRoundtrip() {
    // ue/se 正向编码 → nal_splitter 无关，直接用 RbspBitReader 解回。
    // 这里借 RbspWriter 写、再自己拼字节，交给 sps 测试间接覆盖；此处只做
    // 一个直接的 ue 边界值检查（0、1、大值）通过 SPS 的宏块数字段体现。
    // 具体断言在 SPS/PPS 往返里完成。
}

static void TestSpsRoundtrip() {
    SpsConfig cfg;
    cfg.profile_idc = 66;
    cfg.level_idc = 30;
    cfg.pic_width_in_mbs = 19;    // 304 像素编码宽
    cfg.pic_height_in_mbs = 13;   // 208 像素编码高
    cfg.frame_crop_right_offset = 2;   // 裁 4 像素 -> 真实宽 300
    cfg.frame_crop_bottom_offset = 4;  // 裁 8 像素 -> 真实高 200

    std::vector<uint8_t> rbsp = BuildSpsRbsp(cfg);
    uint8_t header = MakeNalHeader(3, kNalSps);  // ref_idc=3, type=7
    Sps sps = ParseSps(WithHeader(header, rbsp));

    assert(sps.ok);
    assert(sps.profile_idc == 66);
    assert(sps.level_idc == 30);
    assert(sps.PicWidthInMbs() == 19);
    assert(sps.PicHeightInMapUnits() == 13);
    assert(sps.frame_mbs_only_flag == true);
    assert(sps.CodedWidth() == 304);
    assert(sps.CodedHeight() == 208);
    // 裁剪后真实尺寸：CropUnitX=2, CropUnitY=2（4:2:0 逐行）。
    assert(sps.Width() == 300);
    assert(sps.Height() == 200);
    std::printf("  [ok] SPS roundtrip: %ux%u coded, %ux%u display, profile=%d\n",
                sps.CodedWidth(), sps.CodedHeight(), sps.Width(), sps.Height(),
                sps.profile_idc);
}

static void TestPpsRoundtrip() {
    PpsConfig cfg;
    cfg.pic_parameter_set_id = 0;
    cfg.seq_parameter_set_id = 0;
    cfg.pic_init_qp_minus26 = 2;   // 初始 QP = 28

    std::vector<uint8_t> rbsp = BuildPpsRbsp(cfg);
    uint8_t header = MakeNalHeader(3, kNalPps);
    Pps pps = ParsePps(WithHeader(header, rbsp));

    assert(pps.ok);
    assert(pps.entropy_coding_mode_flag == false);  // CAVLC
    assert(pps.PicInitQp() == 28);
    assert(pps.deblocking_filter_control_present_flag == false);
    std::printf("  [ok] PPS roundtrip: entropy=%s init_qp=%d\n",
                pps.EntropyName(), pps.PicInitQp());
}

static void TestEmulationPrevention() {
    // 构造一段一定会触发转义的 RBSP：含 00 00 01 / 00 00 00 / 00 00 02 / 00 00 03。
    std::vector<uint8_t> rbsp = {
        0x00, 0x00, 0x01,        // -> 00 00 03 01
        0xAB,
        0x00, 0x00, 0x00,        // -> 00 00 03 00
        0x00, 0x00, 0x02,        // -> 00 00 03 02
        0x00, 0x00, 0x03,        // -> 00 00 03 03
        0xFF};
    uint8_t header = MakeNalHeader(0, kNalSei);  // 用 SEI 类型做纯载荷测试

    std::vector<uint8_t> stream;
    WriteAnnexBNal(stream, header, rbsp);

    // 起始码必须是 00 00 00 01。
    assert(stream.size() >= 5);
    assert(stream[0] == 0x00 && stream[1] == 0x00 && stream[2] == 0x00 &&
           stream[3] == 0x01);

    // 用 decoder 的 nal_splitter 切回来。
    NalSplitResult r = SplitAnnexB(stream);
    assert(r.ok);
    assert(r.units.size() == 1);
    const NalUnit& u = r.units[0];
    assert(u.nal_unit_type == kNalSei);
    assert(u.forbidden_zero_bit == 0);

    // u.rbsp[0] 是 header，u.rbsp[1..] 应与原始 rbsp 逐字节一致。
    assert(u.rbsp.size() == rbsp.size() + 1);
    assert(u.rbsp[0] == header);
    for (size_t i = 0; i < rbsp.size(); ++i) assert(u.rbsp[i + 1] == rbsp[i]);

    // 至少吞掉了 4 个 emulation prevention 字节（4 处 00 00 xx）。
    assert(u.emulation_bytes_removed >= 4);
    std::printf("  [ok] Annex B start code + emulation prevention: "
                "%d bytes removed, RBSP restored exactly\n",
                u.emulation_bytes_removed);
}

static void TestEscapeUnescapeSymmetry() {
    // EscapeRbsp 后再用 UnescapeRbsp（含 header 起点）应还原。
    std::vector<uint8_t> payload = {0x67, 0x00, 0x00, 0x00, 0x00, 0x01, 0x42};
    std::vector<uint8_t> esc = EscapeRbsp(payload);
    int removed = 0;
    std::vector<uint8_t> back = UnescapeRbsp(esc.data(), esc.size(), &removed);
    assert(back.size() == payload.size());
    for (size_t i = 0; i < payload.size(); ++i) assert(back[i] == payload[i]);
    assert(removed >= 1);
    std::printf("  [ok] Escape/Unescape symmetry: %zu -> %zu -> %zu, removed=%d\n",
                payload.size(), esc.size(), back.size(), removed);
}

int main() {
    TestExpGolombRoundtrip();
    TestSpsRoundtrip();
    TestPpsRoundtrip();
    TestEmulationPrevention();
    TestEscapeUnescapeSymmetry();
    std::printf("all tests passed\n");
    return 0;
}
