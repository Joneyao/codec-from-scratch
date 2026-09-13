// avi_muxer.h — 极简 AVI(RIFF) 容器封装：把一串已编码好的 JPEG 帧拼成一个
//               能被 ffmpeg / VLC / ffprobe 正确识别的 .avi 文件。
//
// 关键认知：容器和编码是两回事。前面 jpeg/encoder 干的是"把一帧像素压成 JPEG
// 字节流"；这里干的是"把 N 段 JPEG 字节流按 AVI 规范排好、加上索引和头部，让
// 播放器知道这是一段 MJPEG 视频、多大、多少帧、多少 fps"。muxer 完全不碰像素。
//
// AVI 是微软 RIFF(Resource Interchange File Format) 的一种。整体是嵌套的 chunk：
//   RIFF('AVI ')
//     LIST('hdrl')                 头部列表：描述整个文件
//       avih                       主头 MainAVIHeader（宽高/帧率/总帧数…）
//       LIST('strl')               流列表（本文件只有一条视频流）
//         strh                     流头 AVIStreamHeader（fccHandler='MJPG'…）
//         strf                     流格式 BITMAPINFOHEADER（biCompression='MJPG'）
//     LIST('movi')                 数据列表：真正的帧数据
//       '00dc' + JPEG bytes        每帧一个 chunk（00=流号, dc=compressed video）
//       ...
//     idx1                         老式索引：每帧在 movi 里的偏移与大小
//
// 所有多字节整数都是小端(little-endian)。每个 chunk = FOURCC(4B) + size(4B) +
// data(size B)，data 若为奇数长度要补一个 0 填充字节（RIFF 要求 chunk 按偶数对齐）。
// 规范参考：微软 AVI RIFF File Reference、OpenDML AVI 1.02 扩展。
#ifndef CODEC_FROM_SCRATCH_MJPEG_AVI_MUXER_H
#define CODEC_FROM_SCRATCH_MJPEG_AVI_MUXER_H

#include <cstdint>
#include <string>
#include <vector>

namespace cfs {

// 一帧已编码好的 JPEG 字节流。
using JpegFrame = std::vector<uint8_t>;

// 组装 AVI 所需参数。
struct AviParams {
    int width = 0;
    int height = 0;
    int fps = 25;  // 帧率（整数即可，内部用 微秒/帧 表示）
};

// 把若干帧 JPEG 数据封装成完整 AVI 字节流。frames 顺序即播放顺序。
std::vector<uint8_t> MuxMjpegAvi(const AviParams& params,
                                 const std::vector<JpegFrame>& frames);

// 便捷：直接写盘。
bool WriteAviFile(const std::string& path,
                  const std::vector<uint8_t>& bytes);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_MJPEG_AVI_MUXER_H
