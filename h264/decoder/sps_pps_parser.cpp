// sps_pps_parser.cpp — SPS/PPS 解析实现，逐字段对照 ITU-T H.264 7.3.2。
#include "sps_pps_parser.h"

#include "rbsp_bit_reader.h"

namespace cfs {

const char* ProfileName(uint8_t profile_idc) {
    switch (profile_idc) {
        case 66:  return "Baseline";
        case 77:  return "Main";
        case 88:  return "Extended";
        case 100: return "High";
        case 110: return "High 10";
        case 122: return "High 4:2:2";
        case 244: return "High 4:4:4";
        default:  return "Unknown";
    }
}

Sps ParseSps(const std::vector<uint8_t>& rbsp) {
    Sps s;
    // rbsp[0] 是 NAL header 字节，SPS 语法从下一个字节开始。
    if (rbsp.size() < 2) {
        s.error = "RBSP too short for SPS";
        return s;
    }
    RbspBitReader br(rbsp.data() + 1, rbsp.size() - 1);

    // 7.3.2.1，严格按语法元素顺序读。
    s.profile_idc = static_cast<uint8_t>(br.ReadBits(8));   // u(8)
    s.constraint_set0_flag = br.ReadBit();                  // u(1)
    s.constraint_set1_flag = br.ReadBit();                  // u(1)
    s.constraint_set2_flag = br.ReadBit();                  // u(1)
    br.ReadBits(5);                                         // reserved_zero_5bits
    s.level_idc = static_cast<uint8_t>(br.ReadBits(8));     // u(8)
    s.seq_parameter_set_id = br.ReadUE();                   // ue(v)

    // 本解析器只覆盖 profile_idc < 100 的基础 profile。High profile 在这里
    // 会插入 chroma_format_idc / 缩放列表等字段，超出本篇范围。
    if (s.profile_idc >= 100) {
        s.error = "High profile (>=100) not supported by this minimal parser";
        return s;
    }

    s.log2_max_frame_num_minus4 = br.ReadUE();              // ue(v)
    s.pic_order_cnt_type = br.ReadUE();                     // ue(v)
    if (s.pic_order_cnt_type == 0) {
        s.log2_max_pic_order_cnt_lsb_minus4 = br.ReadUE();  // ue(v)
    } else if (s.pic_order_cnt_type == 1) {
        // 本系列的测试码流用 poc_type=0，这里对 type=1 做最小跳读以保证健壮。
        br.ReadBit();                                       // delta_pic_order_always_zero_flag
        br.ReadSE();                                        // offset_for_non_ref_pic
        br.ReadSE();                                        // offset_for_top_to_bottom_field
        uint32_t n = br.ReadUE();                           // num_ref_frames_in_poc_cycle
        for (uint32_t i = 0; i < n; ++i) br.ReadSE();       // offset_for_ref_frame[i]
    }

    s.num_ref_frames = br.ReadUE();                         // ue(v)
    s.gaps_in_frame_num_value_allowed_flag = br.ReadBit();  // u(1)
    s.pic_width_in_mbs_minus1 = br.ReadUE();                // ue(v)  <-- 横向宏块数
    s.pic_height_in_map_units_minus1 = br.ReadUE();         // ue(v)  <-- 纵向
    s.frame_mbs_only_flag = br.ReadBit();                   // u(1)
    if (!s.frame_mbs_only_flag) {
        s.mb_adaptive_frame_field_flag = br.ReadBit();      // u(1)
    }
    s.direct_8x8_inference_flag = br.ReadBit();             // u(1)
    s.frame_cropping_flag = br.ReadBit();                   // u(1)
    if (s.frame_cropping_flag) {
        s.frame_crop_left_offset = br.ReadUE();             // ue(v)
        s.frame_crop_right_offset = br.ReadUE();            // ue(v)
        s.frame_crop_top_offset = br.ReadUE();              // ue(v)
        s.frame_crop_bottom_offset = br.ReadUE();           // ue(v)
    }
    s.vui_parameters_present_flag = br.ReadBit();           // u(1)
    // vui_parameters() 与 rbsp_trailing_bits() 后续篇章再处理。

    if (br.Overrun()) {
        s.error = "bit reader overrun while parsing SPS";
        return s;
    }
    s.ok = true;
    return s;
}

Pps ParsePps(const std::vector<uint8_t>& rbsp) {
    Pps p;
    if (rbsp.size() < 2) {
        p.error = "RBSP too short for PPS";
        return p;
    }
    RbspBitReader br(rbsp.data() + 1, rbsp.size() - 1);

    // 7.3.2.2
    p.pic_parameter_set_id = br.ReadUE();                   // ue(v)
    p.seq_parameter_set_id = br.ReadUE();                   // ue(v)
    p.entropy_coding_mode_flag = br.ReadBit();              // u(1)  0=CAVLC 1=CABAC
    p.bottom_field_pic_order_in_frame_present_flag = br.ReadBit();  // u(1)
    p.num_slice_groups_minus1 = br.ReadUE();                // ue(v)
    if (p.num_slice_groups_minus1 > 0) {
        // FMO(灵活宏块排序) 的 slice group 映射，本系列的测试码流不用，
        // 出现时报错而非静默错读，避免误导后续字段。
        p.error = "num_slice_groups_minus1 > 0 (FMO) not supported";
        return p;
    }
    p.num_ref_idx_l0_active_minus1 = br.ReadUE();           // ue(v)
    p.num_ref_idx_l1_active_minus1 = br.ReadUE();           // ue(v)
    p.weighted_pred_flag = br.ReadBit();                    // u(1)
    p.weighted_bipred_idc = static_cast<uint8_t>(br.ReadBits(2));  // u(2)
    p.pic_init_qp_minus26 = br.ReadSE();                    // se(v)
    p.pic_init_qs_minus26 = br.ReadSE();                    // se(v)
    p.chroma_qp_index_offset = br.ReadSE();                 // se(v)
    p.deblocking_filter_control_present_flag = br.ReadBit();// u(1)
    p.constrained_intra_pred_flag = br.ReadBit();           // u(1)
    p.redundant_pic_cnt_present_flag = br.ReadBit();        // u(1)

    if (br.Overrun()) {
        p.error = "bit reader overrun while parsing PPS";
        return p;
    }
    p.ok = true;
    return p;
}

}  // namespace cfs
