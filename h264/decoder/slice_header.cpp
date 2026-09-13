// slice_header.cpp — slice 头部解析实现，逐字段对照 ITU-T H.264 7.3.3。
#include "slice_header.h"

namespace cfs {

SliceCategory SliceTypeToCategory(uint32_t slice_type) {
    // 表 7-3：0/5=P, 1/6=B, 2/7=I, 3/8=SP, 4/9=SI。5..9 与 0..4 同类型。
    switch (slice_type % 5) {
        case 0: return SliceCategory::kP;
        case 1: return SliceCategory::kB;
        case 2: return SliceCategory::kI;
        case 3: return SliceCategory::kSP;
        case 4: return SliceCategory::kSI;
        default: return SliceCategory::kUnknown;
    }
}

const char* SliceCategoryName(SliceCategory c) {
    switch (c) {
        case SliceCategory::kP:  return "P";
        case SliceCategory::kB:  return "B";
        case SliceCategory::kI:  return "I";
        case SliceCategory::kSP: return "SP";
        case SliceCategory::kSI: return "SI";
        default: return "?";
    }
}

SliceHeader ParseSliceHeader(const std::vector<uint8_t>& rbsp,
                             uint8_t nal_unit_type, const Sps& sps,
                             const Pps& pps) {
    SliceHeader sh;
    if (rbsp.size() < 2) {
        sh.error = "RBSP too short for slice header";
        return sh;
    }
    // rbsp[0] 是 NAL header 字节，slice 语法从下一字节开始。
    RbspBitReader br(rbsp.data() + 1, rbsp.size() - 1);

    sh.is_idr = (nal_unit_type == 5);

    // 7.3.3，严格按语法元素顺序。
    sh.first_mb_in_slice = br.ReadUE();       // ue(v)
    sh.slice_type = br.ReadUE();              // ue(v)
    sh.category = SliceTypeToCategory(sh.slice_type);
    sh.pic_parameter_set_id = br.ReadUE();    // ue(v)

    // frame_num 是 u(v)：位宽由 SPS 的 log2_max_frame_num_minus4+4 决定。
    int frame_num_bits = static_cast<int>(sps.log2_max_frame_num_minus4) + 4;
    sh.frame_num = br.ReadBits(frame_num_bits);   // u(v)

    // 逐行帧(frame_mbs_only_flag==1)时没有 field_pic_flag；本系列码流即如此。
    if (!sps.frame_mbs_only_flag) {
        sh.field_pic_flag = br.ReadBit();     // u(1)
        if (sh.field_pic_flag) {
            br.ReadBit();                     // bottom_field_flag，本系列不处理场
            sh.error = "field coding not supported by this minimal parser";
            return sh;
        }
    }

    if (sh.is_idr) {
        sh.idr_pic_id = br.ReadUE();          // ue(v) 仅 IDR
    }

    // 图像顺序计数(POC)相关字段，取决于 SPS 的 pic_order_cnt_type：
    //   type==0：读 pic_order_cnt_lsb（u(v)），可能再读一个 delta。
    //   type==1：读 delta_pic_order_cnt[0..1]（se(v)），需 SPS 更多字段配合。
    //   type==2：不携带任何 POC 字段，顺序直接由 frame_num 推导。
    // 本系列测试码流用 type==2（最简单）。type==1 超出最小解析器范围。
    if (sps.pic_order_cnt_type == 0) {
        int poc_bits =
            static_cast<int>(sps.log2_max_pic_order_cnt_lsb_minus4) + 4;
        sh.pic_order_cnt_lsb = br.ReadBits(poc_bits);  // u(v)
        if (pps.bottom_field_pic_order_in_frame_present_flag &&
            !sh.field_pic_flag) {
            br.ReadSE();                      // delta_pic_order_cnt_bottom
        }
    } else if (sps.pic_order_cnt_type == 2) {
        // 无 POC 字段，什么都不读。
    } else {
        sh.error = "pic_order_cnt_type == 1 not supported";
        return sh;
    }

    // redundant_pic_cnt：仅 PPS 的 redundant_pic_cnt_present_flag 为真时出现。
    if (pps.redundant_pic_cnt_present_flag) {
        br.ReadUE();                          // redundant_pic_cnt
    }

    // ref_pic_list_reordering()：I/SI slice 跳过；其余读一个标志，本系列码流
    // 的 P slice 该标志为 0（不重排），故只处理 flag==0 的情形。
    if (sh.category != SliceCategory::kI && sh.category != SliceCategory::kSI) {
        uint32_t reorder_flag_l0 = br.ReadBit();  // ref_pic_list_reordering_flag_l0
        if (reorder_flag_l0) {
            sh.error = "ref_pic_list_reordering not supported";
            return sh;
        }
    }

    // pred_weight_table()：仅加权预测开启时出现。本系列 baseline 未开启，跳过。
    bool weighted =
        (pps.weighted_pred_flag &&
         (sh.category == SliceCategory::kP || sh.category == SliceCategory::kSP)) ||
        (pps.weighted_bipred_idc == 1 && sh.category == SliceCategory::kB);
    if (weighted) {
        sh.error = "pred_weight_table not supported";
        return sh;
    }

    // dec_ref_pic_marking()：nal_ref_idc!=0 时出现。IDR 走 7.3.3.3 的 IDR 分支：
    // 两个 1 bit 标志。非 IDR 的 adaptive marking 本系列码流不用。
    if (nal_unit_type != 0 /* 简化：本系列的 slice NAL 都是被参考帧 */) {
        if (sh.is_idr) {
            br.ReadBit();  // no_output_of_prior_pics_flag
            br.ReadBit();  // long_term_reference_flag
        } else {
            uint32_t adaptive = br.ReadBit();  // adaptive_ref_pic_marking_mode_flag
            if (adaptive) {
                sh.error = "adaptive_ref_pic_marking not supported";
                return sh;
            }
        }
    }

    // entropy_coding_mode_flag && slice_type!=I/SI 时这里有 cabac_init_idc；
    // 本系列是 CAVLC(flag==0)，无此字段。
    if (pps.entropy_coding_mode_flag && sh.category != SliceCategory::kI &&
        sh.category != SliceCategory::kSI) {
        br.ReadUE();  // cabac_init_idc
    }

    sh.slice_qp_delta = br.ReadSE();          // se(v)

    if (br.Overrun()) {
        sh.error = "bit reader overrun while parsing slice header";
        return sh;
    }
    sh.header_bit_size = br.BitPos();  // slice_data() 从这里开始
    sh.ok = true;
    return sh;
}

}  // namespace cfs
