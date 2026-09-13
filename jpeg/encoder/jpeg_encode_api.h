// jpeg_encode_api.h — 把"编码一帧图像 -> 完整 JPEG 字节流"抽成一个可复用函数。
//
// A5 的 encoder_main.cpp 原来把整条流水线（色彩转换 -> DCT -> 量化 -> 熵编码
// -> JFIF 组装）全写在 main 里。MJPEG（B1）需要对一段视频的每一帧都跑一遍同样
// 的流水线，于是把这段逻辑单独抽出来：给一张 RGB 图和 quality，返回一个能被任何
// 查看器打开的 .jpg 字节流。encoder_main 和 MJPEG 逐帧编码共用它，逻辑只有一份。
//
// 这不改变任何编码结果：抽出来的函数就是原 main 里那段代码，逐行搬过来。
#ifndef CODEC_FROM_SCRATCH_JPEG_JPEG_ENCODE_API_H
#define CODEC_FROM_SCRATCH_JPEG_JPEG_ENCODE_API_H

#include <cstdint>
#include <vector>

#include "color_transform.h"  // RgbImage / ChromaSubsampling
#include "image_io.h"

namespace cfs {

// 把一张 RGB 图像编码成一个完整的 baseline JPEG（JFIF）字节流。
// quality 会被 clamp 到 [1, 100]。固定使用 4:2:0 色度抽样与标准霍夫曼表，
// 与 encoder_main 的行为完全一致。返回的字节流可直接写盘成 .jpg，
// 也可作为一帧塞进 MJPEG 的 AVI 容器。
std::vector<uint8_t> EncodeFrameToJpeg(const RgbImage& rgb, int quality);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_JPEG_JPEG_ENCODE_API_H
