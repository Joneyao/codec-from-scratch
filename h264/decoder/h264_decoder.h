// h264_decoder.h — 把前面几篇的模块串成一个最小的 H.264/AVC 解码器。
//
// 这是"从0到1手搓编解码器"H.264 解码系列的收尾。前面几篇各自实现了一环：
//   Annex B 切 NAL → SPS/PPS 参数 → slice header → 宏块网格 → 帧内预测 →
//   整数反变换/反量化 → CAVLC 残差 → 运动补偿 → 去块滤波。
// 每一环单独看都对，但要拼成一个"喂进 .h264、吐出逐像素和 ffmpeg 一致的 YUV"
// 的解码器，中间还差大量胶水和边界处理：CBP(coded_block_pattern) 的 me(v) 映射、
// Intra_4x4 预测模式的差分信令、色度帧内预测、Intra_16x16 的亮度 DC 哈达玛变换、
// 色度 DC 变换、以及贯穿全帧的"每个 4x4 块非零系数个数"上下文。
//
// 诚实边界（本篇如实说明的范围，均以真实运行结果为准，不夸大）：
//   - 解析层完全打通：Annex B 切 NAL、SPS/PPS、slice header、宏块类型
//     (I_4x4/I_16x16/I_PCM)、帧内预测模式信令、CBP、CAVLC 残差语法元素，
//     都能逐个解析出来，宏块网格与 ffmpeg 报告的结构一致。
//   - 像素重建的"最后一公里"仍有偏差：把残差 CAVLC 一路解到像素时，比特级
//     对齐在复杂宏块上还存在错位，实测第一帧与 ffmpeg 的 PSNR 远未达到"逐像素
//     一致"的水平。这正是"从能跑到能用隔着成千上万个边界"的真实写照——
//     解析框架对了，不等于每个语法元素的条件分支、每处舍入都抠对了。
//   - P 帧的运动补偿模块已具备(见前面几篇)，但完整帧间解码未打通，故跳过 P 帧，
//     不产生误导性的"看似解码"输出。
//   - 不支持 CABAC、B 帧、多参考帧、加权预测、FMO/ASO、场编码——生产级特性，
//     超出"最小可跑通"的范围。
//
// 命名空间 cfs，C++17。
#ifndef CODEC_FROM_SCRATCH_H264_H264_DECODER_H
#define CODEC_FROM_SCRATCH_H264_H264_DECODER_H

#include <cstdint>
#include <string>
#include <vector>

#include "sps_pps_parser.h"

namespace cfs {

// 一帧解码结果：YUV 4:2:0 平面（Y 全分辨率，Cb/Cr 各 1/4）。
struct DecodedFrame {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> y;   // width*height
    std::vector<uint8_t> cb;  // (width/2)*(height/2)
    std::vector<uint8_t> cr;  // (width/2)*(height/2)
    bool is_idr = false;      // 是否 IDR/I 帧
    char slice_type = '?';    // 'I' / 'P'
};

// 解码统计（供文章与配图用真实数据）。
struct DecodeStats {
    int frames = 0;
    int idr_frames = 0;
    int p_frames = 0;
    int total_mbs = 0;
    int intra4x4_mbs = 0;
    int intra16x16_mbs = 0;
    int ipcm_mbs = 0;
    int inter_mbs = 0;
    int skip_mbs = 0;
    long luma_coeffs = 0;     // 累计解出的非零亮度系数个数
};

// 解码器主体。喂进整段 Annex B 字节流，逐帧解出 YUV。
class H264Decoder {
 public:
    // 解码整段码流。成功返回 true，frames 里按显示顺序装好每一帧。
    bool DecodeAnnexB(const std::vector<uint8_t>& bytes,
                      std::vector<DecodedFrame>* frames, std::string* err);

    const DecodeStats& stats() const { return stats_; }

 private:
    Sps sps_;
    Pps pps_;
    bool have_sps_ = false;
    bool have_pps_ = false;
    DecodeStats stats_;
};

// 把一帧 4:2:0 追加写入 YUV 文件（planar：Y 全部、Cb 全部、Cr 全部）。
bool AppendFrameToYuv(const DecodedFrame& f, std::FILE* fp);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_H264_H264_DECODER_H
