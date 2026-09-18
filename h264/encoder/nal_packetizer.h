// nal_packetizer.h — H.264 编码器的「封装层」(D6)：把参数集和 slice 载荷
//                    组装成 NAL Unit，再用 Annex B 起始码拼成 .h264 字节流。
//
// 这是编解码器最外面的一层，也是 D6 收官篇的主角。前面 D1-D5 把「怎么把一帧
// 像素榨成比特」的活儿干完了（帧内模式决策、正变换量化、CAVLC 熵编码……），
// 但那些比特还只是散落的 RBSP(Raw Byte Sequence Payload，原始字节序列载荷)。
// 要让别的解码器（我们自己的 decoder、ffmpeg）认得，还差两件事：
//
//   1) 告诉解码器「这段视频长什么样」——分辨率、profile、量化初值。
//      这就是 SPS(序列参数集) 和 PPS(图像参数集)，它们本身也是 RBSP，
//      用指数哥伦布(Exp-Golomb) ue(v)/se(v) 把一串语法元素编进去。
//   2) 把每段 RBSP 包成 NAL Unit：前面加 1 字节 NAL header，外面套上
//      Annex B 起始码 00 00 00 01 当分隔符。RBSP 里若凑巧出现 00 00 00/01/02/03
//      这种会和起始码撞车的序列，要插入 emulation_prevention_three_byte(0x03)
//      打断它——这正是 decoder 里 nal_splitter 反过来吞掉的那个 03。
//
// 揭示性时刻：编解码是一面镜子。这里正向写进去的 SPS，decoder 的
// sps_pps_parser 必须原样解回来；这里做的 emulation prevention，nal_splitter
// 必须精确还原。自己编的码流，自己的 decoder 能认、ffmpeg 也能认——这是编解
// 码器最基本的自洽性(self-consistency)。本模块和 test 就是把这条镜像关系钉死。
//
// 规范参考：Annex B(字节流)、7.3.1(nal_unit)、7.4.1(emulation prevention)、
// 7.3.2.1(SPS 语法)、7.3.2.2(PPS 语法)、9.1(指数哥伦布)。
// 命名空间 cfs，C++17。本模块聚焦 Baseline profile(profile_idc=66) + CAVLC。
#ifndef CODEC_FROM_SCRATCH_H264_ENCODER_NAL_PACKETIZER_H
#define CODEC_FROM_SCRATCH_H264_ENCODER_NAL_PACKETIZER_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cfs {

// ── RBSP 写入器 ─────────────────────────────────────────────────────────
// 一个 MSB-first 的按位写入器，专为写 H.264 RBSP 设计。它是 decoder 里
// RbspBitReader 的镜像：Reader 逐位读 u(n)/ue(v)/se(v)，Writer 逐位写回去。
//
// 与 common/BitWriter 的区别：common 版服务 JPEG，结尾用 1 补齐（T.81 规定），
// 且不做 H.264 的 trailing bits 语义。RBSP 结尾要写 rbsp_stop_one_bit(1) 再
// 用 0 补齐到字节边界(spec 7.3.2.11)，所以这里单独写一个，语义才对得上。
class RbspWriter {
 public:
    // 写 value 的低 nbits 位（nbits 0..32），高位先出。u(n)。
    void WriteBits(uint32_t value, int nbits);

    // 写 1 位。
    void WriteBit(uint32_t bit);

    // ue(v)：无符号指数哥伦布编码（spec 9.1）。
    //   codeNum -> 前导 (M) 个 0 + 1 + M 位后缀，其中 M=floor(log2(codeNum+1))，
    //   写入的值是 codeNum+1 的二进制。是 RbspBitReader::ReadUE 的逆。
    void WriteUE(uint32_t code_num);

    // se(v)：有符号指数哥伦布编码（spec 9.1.1）。
    //   value -> codeNum：0->0, +1->1, -1->2, +2->3, -2->4 ...
    //   即 codeNum = 2|value|-(value>0?1:0)，再走 ue(v)。是 ReadSE 的逆。
    void WriteSE(int32_t value);

    // 写 rbsp_trailing_bits(spec 7.3.2.11)：先写一个 1(rbsp_stop_one_bit)，
    // 再用 0 补齐到字节边界。调用后 RBSP 即完整。
    void WriteTrailingBits();

    // 已写入的比特数（未对齐）。
    size_t BitCount() const { return bit_count_; }

    // 取出已写字节。若最后一字节不满 8 位，用 0 填充低位（调用方通常应先
    // WriteTrailingBits 保证语法完整）。
    std::vector<uint8_t> TakeBytes() const;

 private:
    std::vector<uint8_t> bytes_;
    uint8_t cur_ = 0;      // 当前正在拼的字节
    int cur_bits_ = 0;     // cur_ 里已有的有效位数(0..7)
    size_t bit_count_ = 0;
};

// ── SPS/PPS 参数 ────────────────────────────────────────────────────────
// 生成 SPS/PPS 时需要的最小配置。字段名对齐 spec 语法元素。
struct SpsConfig {
    uint8_t  profile_idc = 66;   // 66 = Baseline
    uint8_t  level_idc = 30;     // 例：level 3.0
    uint32_t seq_parameter_set_id = 0;
    uint32_t log2_max_frame_num_minus4 = 0;         // frame_num 位宽 = 值+4
    uint32_t log2_max_pic_order_cnt_lsb_minus4 = 0; // poc_lsb 位宽 = 值+4
    uint32_t pic_width_in_mbs = 0;      // 横向宏块数（>=1）
    uint32_t pic_height_in_mbs = 0;     // 纵向宏块数（逐行帧，>=1）
    // 帧裁剪：真实尺寸不是 16 倍数时，用裁剪窗口去掉右/下多余像素。
    uint32_t frame_crop_right_offset = 0;   // 单位：CropUnitX(4:2:0 逐行=2 像素)
    uint32_t frame_crop_bottom_offset = 0;  // 单位：CropUnitY(4:2:0 逐行=2 像素)
};

struct PpsConfig {
    uint32_t pic_parameter_set_id = 0;
    uint32_t seq_parameter_set_id = 0;
    int32_t  pic_init_qp_minus26 = 0;   // 初始 QP = 值 + 26
    // entropy_coding_mode_flag 固定为 0(CAVLC)；本系列不产 CABAC。
    // deblocking_filter_control_present_flag 固定为 0，slice header 后无去块字段。
};

// 生成 SPS 的 RBSP（不含 NAL header，不含起始码，不含 emulation prevention；
// 已写好 rbsp_trailing_bits）。喂给 WriteAnnexBNal 时再加 header 和转义。
std::vector<uint8_t> BuildSpsRbsp(const SpsConfig& cfg);

// 生成 PPS 的 RBSP（同上）。
std::vector<uint8_t> BuildPpsRbsp(const PpsConfig& cfg);

// ── Annex B 封装 ────────────────────────────────────────────────────────
// 把一段 RBSP 封成一个 Annex B NAL Unit，追加到 out：
//   起始码 00 00 00 01 + nal_header(1 字节) + emulation-prevented(RBSP)。
// nal_header 的低 5 位是 nal_unit_type，中间 2 位是 nal_ref_idc，最高位=0。
// emulation prevention：扫描 (header ++ rbsp)，每当出现 00 00 后面跟
// 00/01/02/03 时，在其间插入 0x03（spec 7.4.1）。这是 decoder UnescapeRbsp 的逆。
void WriteAnnexBNal(std::vector<uint8_t>& out, uint8_t nal_header,
                    const std::vector<uint8_t>& rbsp);

// 组一个 nal_header 字节：forbidden_zero_bit=0，nal_ref_idc、nal_unit_type。
uint8_t MakeNalHeader(uint8_t nal_ref_idc, uint8_t nal_unit_type);

// 工具：对一段字节做 emulation prevention（插入 0x03），单独暴露供测试。
std::vector<uint8_t> EscapeRbsp(const std::vector<uint8_t>& in);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_H264_ENCODER_NAL_PACKETIZER_H
