// slice_header.h — 解析一个 slice(切片) 的头部（ITU-T H.264 7.3.3）。
//
// 层级关系（本篇的核心认知）：
//   序列(sequence) → 图像/帧(picture) → 切片(slice) → 宏块(macroblock,16x16) → 子块
// 一帧画面可以只有一个 slice，也可以切成好几个。切成多个不是画面本身的属性，
// 而是为了容错(丢一个 slice 只坏一块，不会整帧崩)和并行(多线程各解一个 slice)。
// slice header 就是这一刀的"说明书"：它是第几刀、什么类型(I/P/B)、用哪个参数集、
// 初始量化参数偏移多少。
//
// 本解析器只覆盖本系列测试码流用到的路径：baseline profile、CAVLC、逐行帧、
// pic_order_cnt_type==0、无 slice group(FMO)、无加权预测。遇到不支持的组合时
// 报错而不是静默错读。
#ifndef CODEC_FROM_SCRATCH_H264_SLICE_HEADER_H
#define CODEC_FROM_SCRATCH_H264_SLICE_HEADER_H

#include <cstdint>
#include <string>

#include "rbsp_bit_reader.h"
#include "sps_pps_parser.h"

namespace cfs {

// slice_type 的五种编码类型（ITU-T H.264 表 7-3，语义 7.4.3）。
// 取值 0..4 与 5..9 语义相同，后者额外约束"整帧所有 slice 同类型"。
enum class SliceCategory { kP, kB, kI, kSP, kSI, kUnknown };

// 把原始 slice_type(0..9) 归一化到类别。
SliceCategory SliceTypeToCategory(uint32_t slice_type);

// 类别可读名，如 "I"、"P"、"B"。
const char* SliceCategoryName(SliceCategory c);

// 解析出来的 slice 头部（只保留本系列关心的字段）。
struct SliceHeader {
    bool ok = false;
    std::string error;

    // ---- 直接读到的语法元素（7.3.3）----
    uint32_t first_mb_in_slice = 0;    // ue(v) 本 slice 第一个宏块的地址(mbAddr)
    uint32_t slice_type = 0;           // ue(v) 原始值 0..9
    uint32_t pic_parameter_set_id = 0; // ue(v) 用哪个 PPS
    uint32_t frame_num = 0;            // u(v)  位宽 = log2_max_frame_num_minus4+4
    bool     field_pic_flag = false;   // u(1)  仅 !frame_mbs_only_flag 时出现
    uint32_t idr_pic_id = 0;           // ue(v) 仅 IDR(nal_unit_type==5) 时出现
    uint32_t pic_order_cnt_lsb = 0;    // u(v)  仅 pic_order_cnt_type==0 时出现
    int32_t  slice_qp_delta = 0;       // se(v) 相对 PPS 初始 QP 的偏移

    // ---- 派生量 ----
    SliceCategory category = SliceCategory::kUnknown;
    bool is_idr = false;               // 由 nal_unit_type==5 传入

    // header 解析结束时的比特位置（相对 rbsp[1] 起始）。紧接着就是 slice_data()
    // 的第一个宏块，供上层读取 mb_type 而无需重放各字段。
    size_t header_bit_size = 0;

    // 本 slice 实际用的量化参数：PPS 初始 QP + slice_qp_delta。
    int32_t SliceQp(const Pps& pps) const {
        return pps.PicInitQp() + slice_qp_delta;
    }
    const char* CategoryName() const { return SliceCategoryName(category); }
};

// 解析 slice header。
//   rbsp            slice NAL 的 RBSP（rbsp[0] 是 header 字节）。
//   nal_unit_type   来自 NAL header（5=IDR slice，1=非 IDR slice）。
//   sps / pps       已解析好的参数集（frame_num 位宽、poc_type 等要从这里取）。
// 解析停在 slice_qp_delta——再往后是 deblocking 参数与宏块数据，交给后续步骤。
SliceHeader ParseSliceHeader(const std::vector<uint8_t>& rbsp,
                             uint8_t nal_unit_type, const Sps& sps,
                             const Pps& pps);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_H264_SLICE_HEADER_H
