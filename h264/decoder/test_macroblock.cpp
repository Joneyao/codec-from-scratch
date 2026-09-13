// test_macroblock.cpp — 校验 slice_type 映射、宏块地址↔坐标映射、真实 slice 解析。
// 四部分：
//   1) slice_type 0..9 归一到正确类别（表 7-3）。
//   2) 宏块地址 ↔ (x,y) 光栅扫描互映射（重点：mbAddr=11 -> 第1行第0列）。
//   3) mb_type 分类（I slice / P slice 表）。
//   4) 解真实 test.h264 的第一个 IDR slice：得 I slice、99 宏块、第一个是 Intra。
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "macroblock_layout.h"
#include "nal_splitter.h"
#include "slice_header.h"
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

// 测试 1：slice_type 归一化（表 7-3）。0/5=P，1/6=B，2/7=I，3/8=SP，4/9=SI。
void TestSliceType() {
    using cfs::SliceCategory;
    CHECK(cfs::SliceTypeToCategory(0) == SliceCategory::kP);
    CHECK(cfs::SliceTypeToCategory(1) == SliceCategory::kB);
    CHECK(cfs::SliceTypeToCategory(2) == SliceCategory::kI);
    CHECK(cfs::SliceTypeToCategory(3) == SliceCategory::kSP);
    CHECK(cfs::SliceTypeToCategory(4) == SliceCategory::kSI);
    // 5..9 与 0..4 同类型。
    CHECK(cfs::SliceTypeToCategory(5) == SliceCategory::kP);
    CHECK(cfs::SliceTypeToCategory(6) == SliceCategory::kB);
    CHECK(cfs::SliceTypeToCategory(7) == SliceCategory::kI);
    CHECK(cfs::SliceTypeToCategory(8) == SliceCategory::kSP);
    CHECK(cfs::SliceTypeToCategory(9) == SliceCategory::kSI);
}

// 测试 2：宏块地址 ↔ 坐标。以 176x144 的 11x9 网格为例。
void TestAddrMapping() {
    cfs::MbGrid g;
    g.width_in_mbs = 11;
    g.height_in_mbs = 9;
    CHECK(g.Total() == 99);

    uint32_t x, y;
    // mbAddr=0 -> (0,0) 左上角。
    g.AddrToXY(0, &x, &y);
    CHECK(x == 0 && y == 0);
    // mbAddr=10 -> 第0行最后一列(10,0)。
    g.AddrToXY(10, &x, &y);
    CHECK(x == 10 && y == 0);
    // 重点：mbAddr=11 -> 第1行第0列(0,1)。光栅扫描换行到下一行行首。
    g.AddrToXY(11, &x, &y);
    CHECK(x == 0 && y == 1);
    // mbAddr=98 -> 右下角最后一个(10,8)。
    g.AddrToXY(98, &x, &y);
    CHECK(x == 10 && y == 8);

    // 反向映射自洽。
    CHECK(g.XYToAddr(0, 1) == 11);
    CHECK(g.XYToAddr(10, 8) == 98);
    for (uint32_t a = 0; a < g.Total(); ++a) {
        uint32_t xx, yy;
        g.AddrToXY(a, &xx, &yy);
        CHECK(g.XYToAddr(xx, yy) == a);
    }

    // 像素坐标：mbAddr=11 的左上角像素 = (0, 16)。
    uint32_t px, py;
    g.AddrToPixel(11, &px, &py);
    CHECK(px == 0 && py == 16);
    g.AddrToPixel(12, &px, &py);
    CHECK(px == 16 && py == 16);
}

// 测试 3：mb_type 分类。
void TestMbClassify() {
    using cfs::MbClass;
    // I slice 表 7-8：0=I_4x4，1..24=I_16x16，25=I_PCM。
    CHECK(cfs::ClassifyISliceMbType(0) == MbClass::kIntra4x4);
    CHECK(cfs::ClassifyISliceMbType(1) == MbClass::kIntra16x16);
    CHECK(cfs::ClassifyISliceMbType(24) == MbClass::kIntra16x16);
    CHECK(cfs::ClassifyISliceMbType(25) == MbClass::kIPcm);
    // P slice 表 7-10：0..4 帧间。
    CHECK(cfs::ClassifyPSliceMbType(0) == MbClass::kPInter);
    CHECK(cfs::ClassifyPSliceMbType(4) == MbClass::kPInter);
    // P 上下文里 mb_type>=5 落入帧内编号（减 5 走 I 表）。
    CHECK(cfs::ClassifyPSliceMbType(5) == MbClass::kIntra4x4);
}

// 测试 4：解真实 test.h264 的第一个 IDR slice。
void TestRealSlice(const std::string& path) {
    std::vector<uint8_t> data = cfs::ReadFileBytes(path);
    if (data.empty()) {
        std::fprintf(stderr, "跳过真实流测试：读不到 %s\n", path.c_str());
        return;
    }
    cfs::NalSplitResult r = cfs::SplitAnnexB(data);
    CHECK(r.ok);

    const cfs::NalUnit* sps_nal = nullptr;
    const cfs::NalUnit* pps_nal = nullptr;
    const cfs::NalUnit* slice_nal = nullptr;
    for (const auto& u : r.units) {
        if (!sps_nal && u.nal_unit_type == cfs::kNalSps) sps_nal = &u;
        if (!pps_nal && u.nal_unit_type == cfs::kNalPps) pps_nal = &u;
        if (!slice_nal && u.nal_unit_type == cfs::kNalSliceIdr) slice_nal = &u;
    }
    CHECK(sps_nal && pps_nal && slice_nal);
    if (!sps_nal || !pps_nal || !slice_nal) return;

    cfs::Sps sps = cfs::ParseSps(sps_nal->rbsp);
    cfs::Pps pps = cfs::ParsePps(pps_nal->rbsp);
    CHECK(sps.ok && pps.ok);

    // 网格：11x9=99，与 C2 解出的宏块宽高一致。
    cfs::MbGrid grid = cfs::MbGrid::FromSps(sps);
    CHECK(grid.width_in_mbs == 11);
    CHECK(grid.height_in_mbs == 9);
    CHECK(grid.Total() == 99);

    // slice header：IDR -> I slice。
    cfs::SliceHeader sh = cfs::ParseSliceHeader(
        slice_nal->rbsp, slice_nal->nal_unit_type, sps, pps);
    CHECK(sh.ok);
    CHECK(sh.is_idr == true);
    CHECK(sh.category == cfs::SliceCategory::kI);
    // 一个 slice 覆盖整帧时，从第 0 个宏块起。
    CHECK(sh.first_mb_in_slice == 0);

    std::printf("真实 slice：类型=%s slice first_mb=%u frame_num=%u "
                "SliceQP=%d 网格=%ux%u=%u 宏块\n",
                sh.CategoryName(), sh.first_mb_in_slice, sh.frame_num,
                sh.SliceQp(pps), grid.width_in_mbs, grid.height_in_mbs,
                grid.Total());
}

}  // namespace

int main(int argc, char** argv) {
    TestSliceType();
    TestAddrMapping();
    TestMbClassify();

    std::string h264_path =
        argc > 1 ? argv[1] : "../../samples/test.h264";
    TestRealSlice(h264_path);

    if (g_failures != 0) {
        std::fprintf(stderr, "test_macroblock: %d FAILURE(s)\n", g_failures);
        return 1;
    }
    std::printf("test_macroblock: 全部通过\n");
    return 0;
}
