// sps_pps_demo.cpp — 从真实 test.h264 里取出 SPS/PPS，用指数哥伦布解出所有字段，
// 重点展示 176x144 是怎么从 pic_width_in_mbs 反推出来的。
//
// 用法：
//   ./sps_pps_demo ../../samples/test.h264 [chart_data/sps_dump.txt]
#include <cstdio>
#include <string>

#include "nal_splitter.h"
#include "sps_pps_parser.h"

namespace {

void PrintSps(const cfs::Sps& s) {
    std::printf("==== SPS (序列参数集) ====\n");
    std::printf("profile_idc                    = %u (%s)\n",
                s.profile_idc, cfs::ProfileName(s.profile_idc));
    std::printf("constraint_set0/1/2            = %d/%d/%d\n",
                s.constraint_set0_flag, s.constraint_set1_flag,
                s.constraint_set2_flag);
    std::printf("level_idc                      = %u (level %.1f)\n",
                s.level_idc, s.level_idc / 10.0);
    std::printf("seq_parameter_set_id           = %u\n",
                s.seq_parameter_set_id);
    std::printf("log2_max_frame_num_minus4      = %u\n",
                s.log2_max_frame_num_minus4);
    std::printf("pic_order_cnt_type             = %u\n", s.pic_order_cnt_type);
    std::printf("num_ref_frames                 = %u\n", s.num_ref_frames);
    std::printf("pic_width_in_mbs_minus1        = %u  -> 横向 %u 个宏块\n",
                s.pic_width_in_mbs_minus1, s.PicWidthInMbs());
    std::printf("pic_height_in_map_units_minus1 = %u  -> 纵向 %u 个 map unit\n",
                s.pic_height_in_map_units_minus1, s.PicHeightInMapUnits());
    std::printf("frame_mbs_only_flag            = %d\n", s.frame_mbs_only_flag);
    std::printf("frame_cropping_flag            = %d\n", s.frame_cropping_flag);
    std::printf("  crop L/R/T/B                 = %u/%u/%u/%u\n",
                s.frame_crop_left_offset, s.frame_crop_right_offset,
                s.frame_crop_top_offset, s.frame_crop_bottom_offset);
    std::printf("vui_parameters_present_flag    = %d\n",
                s.vui_parameters_present_flag);

    std::printf("\n-- 分辨率反推（本篇高潮）--\n");
    std::printf("编码宽 = PicWidthInMbs * 16 = %u * 16 = %u\n",
                s.PicWidthInMbs(), s.CodedWidth());
    std::printf("编码高 = FrameHeightInMbs * 16 = %u * 16 = %u\n",
                s.FrameHeightInMbs(), s.CodedHeight());
    std::printf("裁剪   = 横 -%u, 纵 -%u\n",
                s.CropUnitX() *
                    (s.frame_crop_left_offset + s.frame_crop_right_offset),
                s.CropUnitY() *
                    (s.frame_crop_top_offset + s.frame_crop_bottom_offset));
    std::printf("真实分辨率 = %u x %u\n", s.Width(), s.Height());
}

void PrintPps(const cfs::Pps& p) {
    std::printf("\n==== PPS (图像参数集) ====\n");
    std::printf("pic_parameter_set_id           = %u\n",
                p.pic_parameter_set_id);
    std::printf("seq_parameter_set_id           = %u\n",
                p.seq_parameter_set_id);
    std::printf("entropy_coding_mode_flag       = %d (%s)\n",
                p.entropy_coding_mode_flag, p.EntropyName());
    std::printf("num_slice_groups_minus1        = %u\n",
                p.num_slice_groups_minus1);
    std::printf("num_ref_idx_l0_active_minus1   = %u\n",
                p.num_ref_idx_l0_active_minus1);
    std::printf("pic_init_qp_minus26            = %d (QP=%d)\n",
                p.pic_init_qp_minus26, p.PicInitQp());
    std::printf("deblocking_filter_ctrl_present = %d\n",
                p.deblocking_filter_control_present_flag);
    std::printf("constrained_intra_pred_flag    = %d\n",
                p.constrained_intra_pred_flag);
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

    // 取第一个 SPS 与第一个 PPS。
    const cfs::NalUnit* sps_nal = nullptr;
    const cfs::NalUnit* pps_nal = nullptr;
    for (const auto& u : r.units) {
        if (!sps_nal && u.nal_unit_type == cfs::kNalSps) sps_nal = &u;
        if (!pps_nal && u.nal_unit_type == cfs::kNalPps) pps_nal = &u;
    }
    if (!sps_nal) {
        std::printf("no SPS found in stream\n");
        return 1;
    }

    cfs::Sps sps = cfs::ParseSps(sps_nal->rbsp);
    if (!sps.ok) {
        std::printf("parse SPS failed: %s\n", sps.error.c_str());
        return 1;
    }
    PrintSps(sps);

    cfs::Pps pps;
    if (pps_nal) {
        pps = cfs::ParsePps(pps_nal->rbsp);
        if (pps.ok) {
            PrintPps(pps);
        } else {
            std::printf("\nparse PPS failed: %s\n", pps.error.c_str());
        }
    }

    // 可选：dump 关键字段供 gen_charts.py 读取（真实数据配图）。
    if (argc >= 3) {
        FILE* out = std::fopen(argv[2], "w");
        if (out) {
            std::fprintf(out, "# key value\n");
            std::fprintf(out, "profile_idc %u\n", sps.profile_idc);
            std::fprintf(out, "level_idc %u\n", sps.level_idc);
            std::fprintf(out, "pic_width_in_mbs_minus1 %u\n",
                         sps.pic_width_in_mbs_minus1);
            std::fprintf(out, "pic_height_in_map_units_minus1 %u\n",
                         sps.pic_height_in_map_units_minus1);
            std::fprintf(out, "frame_mbs_only_flag %d\n",
                         sps.frame_mbs_only_flag);
            std::fprintf(out, "frame_cropping_flag %d\n",
                         sps.frame_cropping_flag);
            std::fprintf(out, "crop_lrtb %u %u %u %u\n",
                         sps.frame_crop_left_offset, sps.frame_crop_right_offset,
                         sps.frame_crop_top_offset, sps.frame_crop_bottom_offset);
            std::fprintf(out, "pic_width_in_mbs %u\n", sps.PicWidthInMbs());
            std::fprintf(out, "frame_height_in_mbs %u\n", sps.FrameHeightInMbs());
            std::fprintf(out, "coded_w %u\n", sps.CodedWidth());
            std::fprintf(out, "coded_h %u\n", sps.CodedHeight());
            std::fprintf(out, "width %u\n", sps.Width());
            std::fprintf(out, "height %u\n", sps.Height());
            std::fprintf(out, "num_ref_frames %u\n", sps.num_ref_frames);
            std::fprintf(out, "entropy %s\n", pps.ok ? pps.EntropyName() : "?");
            std::fprintf(out, "pic_init_qp %d\n", pps.ok ? pps.PicInitQp() : 0);
            std::fclose(out);
            std::printf("\ndumped SPS/PPS fields -> %s\n", argv[2]);
        }
    }
    return 0;
}
