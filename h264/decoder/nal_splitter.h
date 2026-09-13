// nal_splitter.h — H.264 Annex B 字节流切分器：把一整段 .h264 裸流(byte stream)
//                  切成一个个 NAL Unit(网络抽象层单元)，并还原出每个单元的 RBSP。
//
// 关键认知（本篇的揭示性时刻）：H.264 的位流不是"文件格式"，它本身是为网络传输
// 设计的一串比特。存到文件里时，用 Annex B 的"起始码前缀"0x000001 当分隔符，把
// 一个个 NAL Unit 首尾相连拼起来。但压缩数据里完全可能凑巧出现 0x000001，
// 于是编码器插入一个 emulation_prevention_three_byte(0x03) 把它打断
// (00 00 01 → 00 00 03 01)，解码器再把这个 03 吞掉，还原出真正的载荷。
// 这一层"防冲突转义"就是本篇要亲手实现的东西。
//
// 层次关系（ITU-T H.264 第 7 章）：
//   byte stream (Annex B)         整段 .h264 文件
//     ├─ start code 0x000001      分隔符，不属于任何 NAL
//     └─ NAL unit                 一个网络抽象层单元
//          ├─ header (1 byte)     forbidden_zero_bit(1)+nal_ref_idc(2)+nal_unit_type(5)
//          └─ RBSP                去掉 03 转义字节后的"原始字节序列载荷"
//
// 规范参考：Annex B(B.1 字节流)、7.3.1(nal_unit 语法)、7.4.1(emulation prevention)。
#ifndef CODEC_FROM_SCRATCH_H264_NAL_SPLITTER_H
#define CODEC_FROM_SCRATCH_H264_NAL_SPLITTER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cfs {

// nal_unit_type 常见取值（ITU-T H.264 表 7-1，本系列只关心其中几个）。
enum NalUnitType : uint8_t {
    kNalUnspecified0 = 0,
    kNalSliceNonIdr  = 1,   // 非 IDR 图像的编码 slice（P 帧承载在这里）
    kNalSlicePartA   = 2,   // 数据划分 A（本系列不处理）
    kNalSlicePartB   = 3,
    kNalSlicePartC   = 4,
    kNalSliceIdr     = 5,   // IDR 图像的编码 slice（关键帧）
    kNalSei          = 6,   // 补充增强信息 SEI
    kNalSps          = 7,   // 序列参数集 SPS
    kNalPps          = 8,   // 图像参数集 PPS
    kNalAud          = 9,   // 访问单元分隔符
};

// 一个切分并解析出来的 NAL Unit。
struct NalUnit {
    // ---- 来自 header 的 3 个语法元素（7.3.1）----
    uint8_t forbidden_zero_bit = 0;  // 必须为 0，非 0 说明码流损坏
    uint8_t nal_ref_idc        = 0;  // 0..3，非 0 表示该单元被后续帧参考
    uint8_t nal_unit_type      = 0;  // 见 NalUnitType

    // ---- 在原始字节流里的定位（供调试与配图）----
    size_t start_code_offset = 0;    // 起始码首字节在文件中的偏移
    size_t start_code_len    = 0;    // 起始码长度：3(0x000001) 或 4(0x00000001)
    size_t nal_offset        = 0;    // NAL 首字节(header)在文件中的偏移
    size_t nal_size          = 0;    // NAL 单元长度（含 header，不含起始码，含转义字节）

    // ---- 去转义后的载荷 ----
    // rbsp[0] 是 header 字节本身；rbsp[1..] 是吞掉 03 后的 RBSP 数据。
    std::vector<uint8_t> rbsp;

    // 本 NAL 内被吞掉的 emulation_prevention_three_byte 个数（配图统计用）。
    int emulation_bytes_removed = 0;

    // nal_unit_type 的可读名字（如 "SPS"、"IDR slice"）。
    std::string TypeName() const;
};

// 切分结果。
struct NalSplitResult {
    bool ok = false;
    std::string error;
    std::vector<NalUnit> units;
    int total_emulation_bytes_removed = 0;  // 全流累计吞掉的 03 个数
};

// 主入口：扫描整段 Annex B 字节流，切出所有 NAL Unit 并还原 RBSP。
NalSplitResult SplitAnnexB(const std::vector<uint8_t>& bytes);

// 工具：把 NAL 载荷（含 header 字节）去掉 emulation_prevention_three_byte，
// 得到 RBSP。removed 返回吞掉的 03 个数。单独暴露出来便于单元测试。
std::vector<uint8_t> UnescapeRbsp(const uint8_t* nal, size_t len, int* removed);

// 工具：读整个文件为字节 vector（失败返回空）。
std::vector<uint8_t> ReadFileBytes(const std::string& path);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_H264_NAL_SPLITTER_H
