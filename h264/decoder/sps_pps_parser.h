// sps_pps_parser.h — 从 RBSP 里解析出 SPS(序列参数集) 与 PPS(图像参数集)。
//
// 本篇的核心：把 C1 切出来的、还是二进制的 SPS，用指数哥伦布逐比特啃开，
// 最终算出这段视频真正的分辨率。
//
// 揭示性时刻：分辨率不是直接写在码流里的整数。H.264 按 16x16 的宏块(Macroblock,
// MB)编码，SPS 里存的是"横向多少个宏块、纵向多少个宏块"，真实像素尺寸要用
//   width  = (pic_width_in_mbs_minus1 + 1) * 16 - 裁剪
//   height = (pic_height_in_map_units_minus1 + 1) * 16 * (2 - frame_mbs_only_flag)
//            - 裁剪
// 反推出来。因为尺寸必须凑成 16 的倍数，真实尺寸靠 frame cropping(裁剪窗口)还原。
//
// 语法参考：SPS 7.3.2.1 / PPS 7.3.2.2 / 语义 7.4.2。
// 注意：本解析器面向 baseline/main 这类基础 profile（profile_idc < 100），
// 不含 High profile 才有的 chroma_format_idc、缩放列表等扩展字段。
#ifndef CODEC_FROM_SCRATCH_H264_SPS_PPS_PARSER_H
#define CODEC_FROM_SCRATCH_H264_SPS_PPS_PARSER_H

#include <cstdint>
#include <string>
#include <vector>

namespace cfs {

// 解析出的序列参数集。字段名与标准语法元素保持一致，便于对照 spec。
struct Sps {
    bool ok = false;
    std::string error;

    // ---- 直接从码流读到的语法元素 ----
    uint8_t  profile_idc = 0;                 // u(8)  66=baseline 77=main 100=high
    bool     constraint_set0_flag = false;    // u(1)
    bool     constraint_set1_flag = false;    // u(1)
    bool     constraint_set2_flag = false;    // u(1)
    uint8_t  level_idc = 0;                    // u(8)  如 10 表示 level 1.0
    uint32_t seq_parameter_set_id = 0;         // ue(v)
    uint32_t log2_max_frame_num_minus4 = 0;    // ue(v)
    uint32_t pic_order_cnt_type = 0;           // ue(v)
    uint32_t log2_max_pic_order_cnt_lsb_minus4 = 0;  // ue(v) (仅 poc_type==0)
    uint32_t num_ref_frames = 0;               // ue(v)
    bool     gaps_in_frame_num_value_allowed_flag = false;  // u(1)
    uint32_t pic_width_in_mbs_minus1 = 0;      // ue(v)  横向宏块数 - 1
    uint32_t pic_height_in_map_units_minus1 = 0;  // ue(v) 纵向 map unit 数 - 1
    bool     frame_mbs_only_flag = false;      // u(1)   1=全逐行帧
    bool     mb_adaptive_frame_field_flag = false;  // u(1)
    bool     direct_8x8_inference_flag = false;// u(1)
    bool     frame_cropping_flag = false;      // u(1)
    uint32_t frame_crop_left_offset = 0;       // ue(v)
    uint32_t frame_crop_right_offset = 0;      // ue(v)
    uint32_t frame_crop_top_offset = 0;        // ue(v)
    uint32_t frame_crop_bottom_offset = 0;     // ue(v)
    bool     vui_parameters_present_flag = false;  // u(1)

    // ---- 由语法元素推导出来的量（7.4.2.1 语义）----
    uint32_t PicWidthInMbs() const { return pic_width_in_mbs_minus1 + 1; }
    uint32_t PicHeightInMapUnits() const {
        return pic_height_in_map_units_minus1 + 1;
    }
    // 帧高（以宏块为单位）：逐行帧时 = map units；含场时 map unit 是场高，需 x2。
    uint32_t FrameHeightInMbs() const {
        return (2u - (frame_mbs_only_flag ? 1u : 0u)) * PicHeightInMapUnits();
    }

    // 裁剪前的编码尺寸（一定是 16 的倍数）。
    uint32_t CodedWidth() const { return PicWidthInMbs() * 16; }
    uint32_t CodedHeight() const { return FrameHeightInMbs() * 16; }

    // 真实显示尺寸 = 编码尺寸 - 裁剪窗口。
    // 4:2:0 色度 + 逐行帧时，CropUnitX=2、CropUnitY=2（7.4.2.1）。
    uint32_t CropUnitX() const { return 2; }
    uint32_t CropUnitY() const { return frame_mbs_only_flag ? 2u : 4u; }
    uint32_t Width() const {
        return CodedWidth() -
               CropUnitX() * (frame_crop_left_offset + frame_crop_right_offset);
    }
    uint32_t Height() const {
        return CodedHeight() -
               CropUnitY() * (frame_crop_top_offset + frame_crop_bottom_offset);
    }
};

// 解析出的图像参数集（只取本系列后续会用到的关键字段）。
struct Pps {
    bool ok = false;
    std::string error;

    uint32_t pic_parameter_set_id = 0;         // ue(v)
    uint32_t seq_parameter_set_id = 0;         // ue(v)  指向哪个 SPS
    bool     entropy_coding_mode_flag = false; // u(1)  0=CAVLC 1=CABAC
    bool     bottom_field_pic_order_in_frame_present_flag = false;  // u(1)
    uint32_t num_slice_groups_minus1 = 0;      // ue(v)
    uint32_t num_ref_idx_l0_active_minus1 = 0; // ue(v)
    uint32_t num_ref_idx_l1_active_minus1 = 0; // ue(v)
    bool     weighted_pred_flag = false;       // u(1)
    uint8_t  weighted_bipred_idc = 0;          // u(2)
    int32_t  pic_init_qp_minus26 = 0;          // se(v)  初始量化参数(相对26)
    int32_t  pic_init_qs_minus26 = 0;          // se(v)
    int32_t  chroma_qp_index_offset = 0;       // se(v)
    bool     deblocking_filter_control_present_flag = false;  // u(1)
    bool     constrained_intra_pred_flag = false;  // u(1)
    bool     redundant_pic_cnt_present_flag = false;  // u(1)

    // 熵编码方式的可读名。
    const char* EntropyName() const {
        return entropy_coding_mode_flag ? "CABAC" : "CAVLC";
    }
    // 初始量化参数真实值。
    int32_t PicInitQp() const { return pic_init_qp_minus26 + 26; }
};

// profile_idc 的可读名。
const char* ProfileName(uint8_t profile_idc);

// 解析 SPS。rbsp 是完整的 NAL RBSP（rbsp[0] 是 NAL header 字节，
// 真正的 SPS 载荷从 rbsp[1] 开始）。
Sps ParseSps(const std::vector<uint8_t>& rbsp);

// 解析 PPS。同样 rbsp[0] 是 header 字节。
Pps ParsePps(const std::vector<uint8_t>& rbsp);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_H264_SPS_PPS_PARSER_H
