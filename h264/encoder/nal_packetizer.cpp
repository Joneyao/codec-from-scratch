// nal_packetizer.cpp — H.264 封装层实现（D6）：RBSP 位写入器 + 指数哥伦布
// 正向编码 + SPS/PPS 生成 + Annex B 起始码 + emulation prevention。
//
// 全程与 decoder 侧镜像对偶：
//   RbspWriter        ↔ RbspBitReader（逐位、ue/se 互为逆）
//   BuildSpsRbsp      ↔ ParseSps
//   BuildPpsRbsp      ↔ ParsePps
//   WriteAnnexBNal    ↔ SplitAnnexB / UnescapeRbsp
#include "nal_packetizer.h"

namespace cfs {

// ── RbspWriter ──────────────────────────────────────────────────────────

void RbspWriter::WriteBit(uint32_t bit) {
    cur_ = static_cast<uint8_t>((cur_ << 1) | (bit & 1u));
    ++cur_bits_;
    ++bit_count_;
    if (cur_bits_ == 8) {
        bytes_.push_back(cur_);
        cur_ = 0;
        cur_bits_ = 0;
    }
}

void RbspWriter::WriteBits(uint32_t value, int nbits) {
    // 高位先写：从第 nbits-1 位往第 0 位逐位送出。
    for (int i = nbits - 1; i >= 0; --i) {
        WriteBit((value >> i) & 1u);
    }
}

void RbspWriter::WriteUE(uint32_t code_num) {
    // codeNum+1 的二进制长度 M+1；前面补 M 个 0，再写 codeNum+1 本身。
    // 这样 leadingZeroBits==M，读取端 2^M-1+suffix 正好还原 codeNum。
    uint64_t v = static_cast<uint64_t>(code_num) + 1;
    int len = 0;
    while ((v >> len) != 0) ++len;      // len = floor(log2(v))+1
    int leading_zeros = len - 1;
    for (int i = 0; i < leading_zeros; ++i) WriteBit(0);
    // 写 v 的低 len 位（最高位是那个 1）。
    for (int i = len - 1; i >= 0; --i) WriteBit((v >> i) & 1u);
}

void RbspWriter::WriteSE(int32_t value) {
    uint32_t code_num;
    if (value <= 0) {
        code_num = static_cast<uint32_t>(-2 * value);       // 0->0, -1->2, -2->4
    } else {
        code_num = static_cast<uint32_t>(2 * value - 1);    // +1->1, +2->3
    }
    WriteUE(code_num);
}

void RbspWriter::WriteTrailingBits() {
    WriteBit(1);                 // rbsp_stop_one_bit
    while (cur_bits_ != 0) WriteBit(0);  // 补 0 到字节边界
}

std::vector<uint8_t> RbspWriter::TakeBytes() const {
    std::vector<uint8_t> out = bytes_;
    if (cur_bits_ != 0) {
        // 最后不足一字节：左对齐（高位是有效位），低位补 0。
        out.push_back(static_cast<uint8_t>(cur_ << (8 - cur_bits_)));
    }
    return out;
}

// ── SPS 生成（spec 7.3.2.1，Baseline 路径）─────────────────────────────

std::vector<uint8_t> BuildSpsRbsp(const SpsConfig& cfg) {
    RbspWriter w;
    w.WriteBits(cfg.profile_idc, 8);          // profile_idc
    // constraint_set0..5_flag + 2 reserved bits = 1 字节。Baseline 置
    // constraint_set0_flag=1（表示遵从 Baseline 约束），其余 0。
    w.WriteBit(1);                            // constraint_set0_flag
    w.WriteBit(0);                            // constraint_set1_flag
    w.WriteBit(0);                            // constraint_set2_flag
    w.WriteBit(0);                            // constraint_set3_flag
    w.WriteBit(0);                            // constraint_set4_flag
    w.WriteBit(0);                            // constraint_set5_flag
    w.WriteBits(0, 2);                        // reserved_zero_2bits
    w.WriteBits(cfg.level_idc, 8);            // level_idc
    w.WriteUE(cfg.seq_parameter_set_id);      // seq_parameter_set_id
    // profile_idc 100/110/122/244/44/83/86/118/128 才有 chroma_format_idc 等
    // High profile 扩展字段；Baseline(66) 直接跳过（与 ParseSps 一致）。
    w.WriteUE(cfg.log2_max_frame_num_minus4); // log2_max_frame_num_minus4
    w.WriteUE(0);                             // pic_order_cnt_type = 0
    w.WriteUE(cfg.log2_max_pic_order_cnt_lsb_minus4);  // (poc_type==0 时出现)
    w.WriteUE(1);                             // max_num_ref_frames = 1
    w.WriteBit(0);                            // gaps_in_frame_num_value_allowed
    w.WriteUE(cfg.pic_width_in_mbs - 1);      // pic_width_in_mbs_minus1
    w.WriteUE(cfg.pic_height_in_mbs - 1);     // pic_height_in_map_units_minus1
    w.WriteBit(1);                            // frame_mbs_only_flag = 1（逐行）
    // frame_mbs_only_flag==1 时无 mb_adaptive_frame_field_flag。
    w.WriteBit(1);                            // direct_8x8_inference_flag
    bool crop = (cfg.frame_crop_right_offset || cfg.frame_crop_bottom_offset);
    w.WriteBit(crop ? 1 : 0);                 // frame_cropping_flag
    if (crop) {
        w.WriteUE(0);                         // frame_crop_left_offset
        w.WriteUE(cfg.frame_crop_right_offset);
        w.WriteUE(0);                         // frame_crop_top_offset
        w.WriteUE(cfg.frame_crop_bottom_offset);
    }
    w.WriteBit(0);                            // vui_parameters_present_flag = 0
    w.WriteTrailingBits();
    return w.TakeBytes();
}

// ── PPS 生成（spec 7.3.2.2）────────────────────────────────────────────

std::vector<uint8_t> BuildPpsRbsp(const PpsConfig& cfg) {
    RbspWriter w;
    w.WriteUE(cfg.pic_parameter_set_id);      // pic_parameter_set_id
    w.WriteUE(cfg.seq_parameter_set_id);      // seq_parameter_set_id
    w.WriteBit(0);                            // entropy_coding_mode_flag=0(CAVLC)
    w.WriteBit(0);                            // bottom_field_pic_order_in_frame_present
    w.WriteUE(0);                             // num_slice_groups_minus1 = 0
    w.WriteUE(0);                             // num_ref_idx_l0_default_active_minus1
    w.WriteUE(0);                             // num_ref_idx_l1_default_active_minus1
    w.WriteBit(0);                            // weighted_pred_flag
    w.WriteBits(0, 2);                        // weighted_bipred_idc
    w.WriteSE(cfg.pic_init_qp_minus26);       // pic_init_qp_minus26
    w.WriteSE(0);                             // pic_init_qs_minus26
    w.WriteSE(0);                             // chroma_qp_index_offset
    w.WriteBit(0);                            // deblocking_filter_control_present=0
    w.WriteBit(0);                            // constrained_intra_pred_flag
    w.WriteBit(0);                            // redundant_pic_cnt_present_flag
    w.WriteTrailingBits();
    return w.TakeBytes();
}

// ── Annex B 封装 + emulation prevention ────────────────────────────────

uint8_t MakeNalHeader(uint8_t nal_ref_idc, uint8_t nal_unit_type) {
    // forbidden_zero_bit(1)=0 | nal_ref_idc(2) | nal_unit_type(5)
    return static_cast<uint8_t>(((nal_ref_idc & 0x3) << 5) |
                                (nal_unit_type & 0x1F));
}

std::vector<uint8_t> EscapeRbsp(const std::vector<uint8_t>& in) {
    // 扫描：连续两个 0x00 之后，若下一字节 <= 0x03，插入 0x03 再写该字节。
    // 这样 00 00 00/01/02/03 都被打断成 00 00 03 xx，绝不与起始码撞车。
    std::vector<uint8_t> out;
    out.reserve(in.size() + in.size() / 8 + 4);
    int zeros = 0;
    for (uint8_t b : in) {
        if (zeros >= 2 && b <= 0x03) {
            out.push_back(0x03);   // emulation_prevention_three_byte
            zeros = 0;
        }
        out.push_back(b);
        if (b == 0x00) ++zeros; else zeros = 0;
    }
    return out;
}

void WriteAnnexBNal(std::vector<uint8_t>& out, uint8_t nal_header,
                    const std::vector<uint8_t>& rbsp) {
    // 4 字节起始码 00 00 00 01（也可用 3 字节，这里统一用 4 字节最稳妥）。
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0x01);
    // emulation prevention 要覆盖 header 字节 + RBSP 整体（decoder 的
    // UnescapeRbsp 也是从 header 开始扫描的）。
    std::vector<uint8_t> payload;
    payload.reserve(rbsp.size() + 1);
    payload.push_back(nal_header);
    payload.insert(payload.end(), rbsp.begin(), rbsp.end());
    std::vector<uint8_t> escaped = EscapeRbsp(payload);
    out.insert(out.end(), escaped.begin(), escaped.end());
}

}  // namespace cfs
