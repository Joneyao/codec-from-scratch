// avi_muxer.cpp — 极简 AVI 封装实现。见 avi_muxer.h 的结构说明。
#include "avi_muxer.h"

#include <cstdio>
#include <cstring>

namespace cfs {

namespace {

// ---- 小端写入辅助：把整数按小端追加到字节缓冲 ----
void PutU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}
void PutU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}
// FOURCC：4 个 ASCII 字符按原顺序写入（不做字节序反转）。
void PutFourCC(std::vector<uint8_t>& b, const char* cc) {
    b.push_back(static_cast<uint8_t>(cc[0]));
    b.push_back(static_cast<uint8_t>(cc[1]));
    b.push_back(static_cast<uint8_t>(cc[2]));
    b.push_back(static_cast<uint8_t>(cc[3]));
}
// 在 buf 的 pos 处（4 字节）回填一个小端 u32（用于事后补 chunk 长度）。
void PatchU32(std::vector<uint8_t>& b, size_t pos, uint32_t v) {
    b[pos + 0] = static_cast<uint8_t>(v & 0xFF);
    b[pos + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[pos + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[pos + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

// idx1 索引项里的标志位：本帧是关键帧。MJPEG 每帧都是独立完整的 JPEG，
// 所以每一帧都是关键帧（AVIIF_KEYFRAME = 0x10）。
constexpr uint32_t kAviifKeyframe = 0x00000010u;

}  // namespace

std::vector<uint8_t> MuxMjpegAvi(const AviParams& params,
                                 const std::vector<JpegFrame>& frames) {
    std::vector<uint8_t> out;
    out.reserve(4096);

    const uint32_t num_frames = static_cast<uint32_t>(frames.size());
    const uint32_t fps = params.fps > 0 ? static_cast<uint32_t>(params.fps) : 25;
    // 每帧时长，单位微秒（MainAVIHeader.dwMicroSecPerFrame）。
    const uint32_t usec_per_frame = 1000000u / fps;
    // 最大帧字节数（dwSuggestedBufferSize / avih.dwSuggestedBufferSize）。
    uint32_t max_frame_bytes = 0;
    for (const auto& f : frames) {
        if (f.size() > max_frame_bytes)
            max_frame_bytes = static_cast<uint32_t>(f.size());
    }

    // ============ RIFF('AVI ') ============
    PutFourCC(out, "RIFF");
    const size_t riff_size_pos = out.size();
    PutU32(out, 0);  // 占位，最后回填 = 文件总长 - 8
    PutFourCC(out, "AVI ");

    // ---------- LIST('hdrl') ----------
    PutFourCC(out, "LIST");
    const size_t hdrl_size_pos = out.size();
    PutU32(out, 0);  // 占位，回填 = hdrl 内容长度（含 'hdrl' 这 4 字节）
    const size_t hdrl_start = out.size();
    PutFourCC(out, "hdrl");

    // avih —— MainAVIHeader，固定 56 字节数据。
    PutFourCC(out, "avih");
    PutU32(out, 56);
    PutU32(out, usec_per_frame);       // dwMicroSecPerFrame
    PutU32(out, max_frame_bytes * fps);  // dwMaxBytesPerSec（粗略估算即可）
    PutU32(out, 0);                    // dwPaddingGranularity
    PutU32(out, 0x00000010);           // dwFlags = AVIF_HASINDEX（有 idx1）
    PutU32(out, num_frames);           // dwTotalFrames
    PutU32(out, 0);                    // dwInitialFrames
    PutU32(out, 1);                    // dwStreams（一条视频流）
    PutU32(out, max_frame_bytes);      // dwSuggestedBufferSize
    PutU32(out, static_cast<uint32_t>(params.width));   // dwWidth
    PutU32(out, static_cast<uint32_t>(params.height));  // dwHeight
    PutU32(out, 0);                    // dwReserved[0]
    PutU32(out, 0);                    // dwReserved[1]
    PutU32(out, 0);                    // dwReserved[2]
    PutU32(out, 0);                    // dwReserved[3]

    // LIST('strl') —— 视频流列表。
    PutFourCC(out, "LIST");
    const size_t strl_size_pos = out.size();
    PutU32(out, 0);
    const size_t strl_start = out.size();
    PutFourCC(out, "strl");

    // strh —— AVIStreamHeader，56 字节数据。
    PutFourCC(out, "strh");
    PutU32(out, 56);
    PutFourCC(out, "vids");   // fccType：视频流
    PutFourCC(out, "MJPG");   // fccHandler：MJPEG 解码器
    PutU32(out, 0);           // dwFlags
    PutU16(out, 0);           // wPriority
    PutU16(out, 0);           // wLanguage
    PutU32(out, 0);           // dwInitialFrames
    PutU32(out, 1);           // dwScale
    PutU32(out, fps);         // dwRate → 帧率 = dwRate/dwScale
    PutU32(out, 0);           // dwStart
    PutU32(out, num_frames);  // dwLength（帧数）
    PutU32(out, max_frame_bytes);  // dwSuggestedBufferSize
    PutU32(out, 0xFFFFFFFFu);  // dwQuality（-1 = 默认）
    PutU32(out, 0);           // dwSampleSize（0 = 每帧一个样本，变长）
    // rcFrame：left, top, right, bottom（各 16 位）
    PutU16(out, 0);
    PutU16(out, 0);
    PutU16(out, static_cast<uint16_t>(params.width));
    PutU16(out, static_cast<uint16_t>(params.height));

    // strf —— BITMAPINFOHEADER，40 字节。
    PutFourCC(out, "strf");
    PutU32(out, 40);
    PutU32(out, 40);          // biSize
    PutU32(out, static_cast<uint32_t>(params.width));   // biWidth
    PutU32(out, static_cast<uint32_t>(params.height));  // biHeight
    PutU16(out, 1);           // biPlanes
    PutU16(out, 24);          // biBitCount
    PutFourCC(out, "MJPG");   // biCompression = 'MJPG'
    PutU32(out, static_cast<uint32_t>(params.width * params.height * 3));  // biSizeImage
    PutU32(out, 0);           // biXPelsPerMeter
    PutU32(out, 0);           // biYPelsPerMeter
    PutU32(out, 0);           // biClrUsed
    PutU32(out, 0);           // biClrImportant

    // 回填 strl / hdrl 长度。
    PatchU32(out, strl_size_pos,
             static_cast<uint32_t>(out.size() - strl_start));
    PatchU32(out, hdrl_size_pos,
             static_cast<uint32_t>(out.size() - hdrl_start));

    // ---------- LIST('movi') ----------
    PutFourCC(out, "LIST");
    const size_t movi_size_pos = out.size();
    PutU32(out, 0);
    const size_t movi_start = out.size();
    PutFourCC(out, "movi");
    // movi 内每个 chunk 的偏移，idx1 里要用（相对 movi 起始的 'movi' 之后）。
    const size_t movi_data_base = out.size();  // 指向 'movi' 之后第一个 chunk

    struct IndexEntry {
        uint32_t offset;  // 相对 movi_data_base 的偏移（指向 chunk 的 FOURCC）
        uint32_t size;    // chunk 数据长度（不含 pad）
    };
    std::vector<IndexEntry> index;
    index.reserve(frames.size());

    for (const auto& f : frames) {
        const uint32_t off =
            static_cast<uint32_t>(out.size() - movi_data_base);
        PutFourCC(out, "00dc");  // 流 00 的 compressed video 数据
        PutU32(out, static_cast<uint32_t>(f.size()));
        out.insert(out.end(), f.begin(), f.end());
        if (f.size() & 1u) out.push_back(0);  // 奇数长度补齐
        index.push_back({off, static_cast<uint32_t>(f.size())});
    }
    PatchU32(out, movi_size_pos,
             static_cast<uint32_t>(out.size() - movi_start));

    // ---------- idx1 ----------
    // 老式索引：每帧一项，16 字节：ckid(4) + dwFlags(4) + dwOffset(4) + dwSize(4)。
    // dwOffset 是相对 movi 列表里 'movi' FOURCC 之后的偏移（含 chunk 头）。
    PutFourCC(out, "idx1");
    PutU32(out, static_cast<uint32_t>(index.size() * 16));
    for (const auto& e : index) {
        PutFourCC(out, "00dc");
        PutU32(out, kAviifKeyframe);
        PutU32(out, e.offset);
        PutU32(out, e.size);
    }

    // 回填最外层 RIFF 长度。
    PatchU32(out, riff_size_pos, static_cast<uint32_t>(out.size() - 8));
    return out;
}

bool WriteAviFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return false;
    size_t n = std::fwrite(bytes.data(), 1, bytes.size(), fp);
    std::fclose(fp);
    return n == bytes.size();
}

}  // namespace cfs
