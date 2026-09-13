// frame_sequencer.cpp — 逐帧独立编码，无跨帧状态。见头文件说明。
#include "frame_sequencer.h"

#include "jpeg_encode_api.h"  // cfs::EncodeFrameToJpeg（来自 jpeg/encoder）

namespace cfs {

std::vector<JpegFrame> EncodeFrameSequence(const std::vector<RgbImage>& frames,
                                           int quality) {
    std::vector<JpegFrame> out;
    out.reserve(frames.size());
    for (const auto& frame : frames) {
        // 关键：每帧都从头编码，完全不参考相邻帧。这就是 MJPEG "逐帧独立"。
        out.push_back(EncodeFrameToJpeg(frame, quality));
    }
    return out;
}

}  // namespace cfs
