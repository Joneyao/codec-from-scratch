// avi_demuxer.h — 极简 AVI(RIFF) 容器解封装：把一个 .avi 文件拆回"视频信息 +
//                 一叠 JPEG 字节流"。这是 B1 avi_muxer 的镜像操作。
//
// 关键认知（本篇的揭示性时刻）：解封装(demux)和解码(decode)是两件独立的事。
// demuxer 只做"拆快递"——顺着 RIFF 的嵌套 chunk 往下走，读出宽高/帧率/帧数，
// 再从 movi 列表里把每个 '00dc' chunk 的 JPEG 字节原样抠出来。它压根不碰像素，
// 不做任何图像还原。真正把 JPEG 变回画面的，是复用的 jpeg/decoder。
//
// AVI 是微软 RIFF(Resource Interchange File Format) 的一种，整体是嵌套 chunk：
//   RIFF('AVI ')
//     LIST('hdrl')                 头部列表
//       avih                       主头 MainAVIHeader（宽高/帧率/总帧数…）
//       LIST('strl') strh/strf     流头 + BITMAPINFOHEADER（biCompression='MJPG'）
//     LIST('movi')                 数据列表
//       '00dc' + JPEG bytes        每帧一个 chunk
//     idx1                         老式索引（可选，用于快速定位）
//
// 所有多字节整数都是小端(little-endian)。每个 chunk = FOURCC(4B) + size(4B) +
// data(size B)，data 若为奇数长度后面补一个填充字节（RIFF 偶数对齐）。
// 规范参考：微软 AVI RIFF File Reference、OpenDML AVI 1.02 扩展。
#ifndef CODEC_FROM_SCRATCH_MJPEG_AVI_DEMUXER_H
#define CODEC_FROM_SCRATCH_MJPEG_AVI_DEMUXER_H

#include <cstdint>
#include <string>
#include <vector>

namespace cfs {

// 一帧从容器里抠出来的 JPEG 字节流（尚未解码，还是压缩数据）。
using JpegFrameBytes = std::vector<uint8_t>;

// 解封装结果：视频信息 + 每帧的 JPEG 字节流 + 出错信息。
struct AviDemuxResult {
    bool ok = false;
    std::string error;

    int width = 0;
    int height = 0;
    int fps = 0;               // 由 strh 的 dwRate/dwScale 得出
    int total_frames = 0;      // avih 声明的 dwTotalFrames
    bool is_mjpeg = false;     // fccHandler / biCompression 是否为 'MJPG'

    // 顺序即播放顺序：每个元素是一帧完整的 JPEG 字节流（FFD8…FFD9）。
    std::vector<JpegFrameBytes> frames;

    // 供调试与配图：每帧 '00dc' chunk 在文件里的绝对字节偏移（指向 FOURCC）
    // 及其数据长度（不含 pad）。
    std::vector<size_t> frame_offsets;
    std::vector<size_t> frame_sizes;

    size_t movi_offset = 0;    // 'movi' FOURCC 在文件里的绝对偏移
    bool used_idx1 = false;    // 本次是否用 idx1 索引定位帧
};

// 主入口：解析整段 AVI 字节流，拆出视频信息与每帧 JPEG。
AviDemuxResult DemuxMjpegAvi(const std::vector<uint8_t>& bytes);

// 便捷：从磁盘读 .avi 再解封装。
AviDemuxResult DemuxMjpegAviFile(const std::string& path);

// 读整个文件为字节 vector（失败返回空）。
std::vector<uint8_t> ReadWholeFileBytes(const std::string& path);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_MJPEG_AVI_DEMUXER_H
