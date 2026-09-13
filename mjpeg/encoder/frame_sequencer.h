// frame_sequencer.h — MJPEG 的"心脏"，但其实没什么料：把一串帧逐个丢给
//                     jpeg/encoder 的 EncodeFrameToJpeg，收集每帧的 JPEG 字节流。
//
// 这正是 MJPEG(Motion JPEG) 的全部秘密：没有运动补偿、没有帧间预测、没有 P/B 帧。
// 每一帧都当成一张孤立的静态图，从头独立编码。sequencer 不看前一帧、不看后一帧，
// 循环体里就是"编码这一帧"，仅此而已。名字里的 "Motion" 有点名不副实。
#ifndef CODEC_FROM_SCRATCH_MJPEG_FRAME_SEQUENCER_H
#define CODEC_FROM_SCRATCH_MJPEG_FRAME_SEQUENCER_H

#include <cstdint>
#include <vector>

#include "avi_muxer.h"        // JpegFrame
#include "color_transform.h"  // RgbImage

namespace cfs {

// 逐帧独立编码：对 frames 里的每张 RGB 图调用 EncodeFrameToJpeg(quality)，
// 返回等长的 JPEG 字节流序列（顺序不变）。这里没有任何跨帧状态。
std::vector<JpegFrame> EncodeFrameSequence(const std::vector<RgbImage>& frames,
                                           int quality);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_MJPEG_FRAME_SEQUENCER_H
