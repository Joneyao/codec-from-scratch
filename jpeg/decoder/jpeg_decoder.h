// jpeg_decoder.h — 把 A6/A7 的零件拼成一台完整的 baseline JPEG 解码器。
//
// A6 解析出了头（尺寸、量化表、霍夫曼表、抽样因子）。A7 能把比特流里的一个
// 8x8 块还原成像素。A8 要做的是"调度"：按 MCU 顺序把整张图的所有块解出来，
// 再处理三件真实世界的脏活，最后拼成一张 RGB 图。
//
// 三件脏活（编码器 A5 没暴露、但解码真实文件躲不掉）：
//   1. 交错扫描顺序：一个 MCU 里 Hi*Vi 个亮度块 + 各 1 个色度块，严格按
//      T.81 A.2.3 的交错次序读，DC 差分按分量各自累加（Y/Cb/Cr 三条独立链）。
//   2. 色度上采样：4:2:0 的 Cb/Cr 只有 Y 的四分之一分辨率，要放大回全分辨率
//      才能和 Y 对齐做颜色合成（T.871 只定义了转换公式，不规定上采样算法，
//      这里用最近邻——libjpeg 默认是三角形/平滑滤波，误差主要来自这里）。
//   3. 重启标记 + 尺寸裁剪：遇到 DRI 声明的 restart interval，每隔若干 MCU
//      有一个 RSTn，此时 DC 预测清零、比特流按字节对齐（T.81 F.2.1.3.1）；
//      非 16 倍数尺寸的图按 MCU 对齐解码（补到整块），输出时裁回真实宽高。
//
// 依据：解码总流程 T.81 F.2.1；色彩转换 JFIF / ITU-T T.871。
#ifndef CODEC_FROM_SCRATCH_JPEG_DECODER_JPEG_DECODER_H
#define CODEC_FROM_SCRATCH_JPEG_DECODER_JPEG_DECODER_H

#include <string>

#include "image_io.h"    // RgbImage（复用 common 的 PPM 定义）
#include "jfif_parser.h"

namespace cfs {

// 解码结果：成功与否 + 出错信息 + 还原出的 RGB 图。
struct DecodeResult {
    bool ok = false;
    std::string error;
    RgbImage image;
    // 解码过程中记录的几个关键量，供调试与配图。
    int mcu_cols = 0;         // 横向 MCU 个数
    int mcu_rows = 0;         // 纵向 MCU 个数
    int max_h = 1;            // 各分量里最大的水平抽样因子（决定 MCU 宽）
    int max_v = 1;            // 各分量里最大的垂直抽样因子（决定 MCU 高）
    int restart_interval = 0; // 实际用到的重启间隔（0=没有）
};

// 主入口：给一个已解析好的头 + 原始文件字节，解出整张 RGB 图。
// 字节流单独传入是因为解析器只记了熵数据的偏移，没留原始字节（见 A6 设计）。
DecodeResult DecodeJpeg(const JpegHeader& header,
                        const std::vector<uint8_t>& file_bytes);

// 便捷：从磁盘读一个 .jpg，一路解到 RGB。
DecodeResult DecodeJpegFile(const std::string& path);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_JPEG_DECODER_JPEG_DECODER_H
