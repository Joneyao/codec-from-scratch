// macroblock_demo.cpp — 把 test.h264 的第一个 IDR slice 组织成宏块网格。
//
// 做三件事：
//   1) 走通 C1(切 NAL)→C2(解 SPS/PPS)→本篇(解 slice header) 的链路。
//   2) 用 SPS 的宏块宽高建立 11x9=99 的宏块网格，打印每个格子的 mbAddr 与像素坐标。
//   3) 读出该 slice 第一个宏块的 mb_type，确认 IDR 帧起手是 I(帧内)宏块，并把
//      整帧宏块按类型统计（IDR 全帧 Intra）。
//
// 诚实说明：把 slice 里"每一个"宏块的 mb_type 都读出来，需要先解 CAVLC 残差
// 才能推进比特位置到下一个宏块——那是后续篇章的事。本篇聚焦"网格如何组织"和
// "第一个宏块的类型如何读"，整帧类型分布依据"IDR 是全帧内编码"这一语义得出
// （表 7-3 约束 IDR 的 slice_type 只能是 I）。
//
// 用法：
//   ./macroblock_demo ../../samples/test.h264 [chart_data/mb_dump.txt]
#include <cstdio>
#include <string>
#include <vector>

#include "macroblock_layout.h"
#include "nal_splitter.h"
#include "slice_header.h"
#include "sps_pps_parser.h"

namespace {

void PrintGrid(const cfs::MbGrid& g) {
    std::printf("宏块网格：%u 列 x %u 行 = %u 个宏块（每个 16x16 像素）\n\n",
                g.width_in_mbs, g.height_in_mbs, g.Total());
    // 用 mbAddr 编号画出光栅扫描顺序。
    std::printf("光栅扫描编号（mbAddr，从左到右、从上到下）：\n");
    for (uint32_t y = 0; y < g.height_in_mbs; ++y) {
        std::printf("  行%u |", y);
        for (uint32_t x = 0; x < g.width_in_mbs; ++x) {
            std::printf(" %3u", g.XYToAddr(x, y));
        }
        std::printf("\n");
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s <input.h264> [dump.txt]\n", argv[0]);
        return 1;
    }
    const std::string in_path = argv[1];
    std::vector<uint8_t> bytes = cfs::ReadFileBytes(in_path);
    if (bytes.empty()) {
        std::printf("error: cannot read %s\n", in_path.c_str());
        return 1;
    }
    std::printf("input: %s  (%zu bytes)\n\n", in_path.c_str(), bytes.size());

    cfs::NalSplitResult r = cfs::SplitAnnexB(bytes);
    if (!r.ok) {
        std::printf("split failed: %s\n", r.error.c_str());
        return 1;
    }

    // 取第一个 SPS/PPS，以及第一个 IDR slice。
    const cfs::NalUnit* sps_nal = nullptr;
    const cfs::NalUnit* pps_nal = nullptr;
    const cfs::NalUnit* slice_nal = nullptr;
    for (const auto& u : r.units) {
        if (!sps_nal && u.nal_unit_type == cfs::kNalSps) sps_nal = &u;
        if (!pps_nal && u.nal_unit_type == cfs::kNalPps) pps_nal = &u;
        if (!slice_nal && u.nal_unit_type == cfs::kNalSliceIdr) slice_nal = &u;
    }
    if (!sps_nal || !pps_nal || !slice_nal) {
        std::printf("need SPS+PPS+IDR slice; missing one of them\n");
        return 1;
    }

    cfs::Sps sps = cfs::ParseSps(sps_nal->rbsp);
    cfs::Pps pps = cfs::ParsePps(pps_nal->rbsp);
    if (!sps.ok || !pps.ok) {
        std::printf("parse SPS/PPS failed\n");
        return 1;
    }

    // ---- 解 slice header ----
    cfs::SliceHeader sh = cfs::ParseSliceHeader(
        slice_nal->rbsp, slice_nal->nal_unit_type, sps, pps);
    if (!sh.ok) {
        std::printf("parse slice header failed: %s\n", sh.error.c_str());
        return 1;
    }
    std::printf("==== Slice header ====\n");
    std::printf("first_mb_in_slice     = %u\n", sh.first_mb_in_slice);
    std::printf("slice_type            = %u (%s slice)\n",
                sh.slice_type, sh.CategoryName());
    std::printf("pic_parameter_set_id  = %u\n", sh.pic_parameter_set_id);
    std::printf("frame_num             = %u\n", sh.frame_num);
    std::printf("is_idr                = %s\n", sh.is_idr ? "yes" : "no");
    if (sh.is_idr) std::printf("idr_pic_id            = %u\n", sh.idr_pic_id);
    std::printf("pic_order_cnt_lsb     = %u\n", sh.pic_order_cnt_lsb);
    std::printf("slice_qp_delta        = %d  -> SliceQP = %d\n",
                sh.slice_qp_delta, sh.SliceQp(pps));

    // ---- 建立宏块网格 ----
    cfs::MbGrid grid = cfs::MbGrid::FromSps(sps);
    std::printf("\n==== 宏块网格 ====\n");
    PrintGrid(grid);

    // ---- 读第一个宏块的 mb_type ----
    // slice header 结束处紧跟 slice_data() 的第一个宏块。IDR/I slice、CAVLC 下，
    // 第一个语法元素就是 mb_type = ue(v)（无 mb_skip_run，那是 P/B 才有的）。
    // ParseSliceHeader 已经把结束比特位记在 header_bit_size 里，直接跳到那里读。
    cfs::RbspBitReader br(slice_nal->rbsp.data() + 1,
                          slice_nal->rbsp.size() - 1);
    br.SkipBits(sh.header_bit_size);               // 对齐到 slice_data 起点
    uint32_t first_mb_type = br.ReadUE();          // slice_data() 第一个 mb_type
    cfs::MbClass first_class = cfs::ClassifyISliceMbType(first_mb_type);
    std::printf("\n==== 第一个宏块（mbAddr=%u）====\n", sh.first_mb_in_slice);
    std::printf("mb_type = %u  -> %s\n", first_mb_type,
                cfs::MbClassName(first_class));

    // ---- 整帧类型分布（IDR = 全帧内）----
    // 依据 slice_type 语义：IDR 只能是 I slice，其宏块全部是 Intra 类。
    uint32_t total = grid.Total();
    std::printf("\n==== 整帧宏块类型分布 ====\n");
    std::printf("slice 类型 = %s，IDR 帧全部为帧内(Intra)宏块\n",
                sh.CategoryName());
    std::printf("Intra 宏块 = %u / %u (100%%)\n", total, total);
    std::printf("P(帧间)宏块 = 0，skip 宏块 = 0（本帧无帧间预测）\n");

    // ---- 可选 dump 供 gen_charts.py ----
    if (argc >= 3) {
        FILE* out = std::fopen(argv[2], "w");
        if (out) {
            std::fprintf(out, "# key value\n");
            std::fprintf(out, "width_in_mbs %u\n", grid.width_in_mbs);
            std::fprintf(out, "height_in_mbs %u\n", grid.height_in_mbs);
            std::fprintf(out, "total_mbs %u\n", grid.Total());
            std::fprintf(out, "slice_type %u\n", sh.slice_type);
            std::fprintf(out, "slice_category %s\n", sh.CategoryName());
            std::fprintf(out, "is_idr %d\n", sh.is_idr ? 1 : 0);
            std::fprintf(out, "first_mb_in_slice %u\n", sh.first_mb_in_slice);
            std::fprintf(out, "frame_num %u\n", sh.frame_num);
            std::fprintf(out, "slice_qp %d\n", sh.SliceQp(pps));
            std::fprintf(out, "first_mb_type %u\n", first_mb_type);
            std::fprintf(out, "first_mb_class %s\n",
                         cfs::MbClassName(first_class));
            std::fprintf(out, "intra_mbs %u\n", total);
            std::fprintf(out, "inter_mbs 0\n");
            std::fprintf(out, "skip_mbs 0\n");
            std::fclose(out);
            std::printf("\ndumped macroblock layout -> %s\n", argv[2]);
        }
    }
    return 0;
}
