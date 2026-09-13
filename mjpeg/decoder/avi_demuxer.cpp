// avi_demuxer.cpp — 极简 AVI 解封装实现。见 avi_demuxer.h 的结构说明。
//
// 解析策略：递归下降式地遍历 RIFF chunk 树。
//   1. 读最外层 RIFF('AVI ')。
//   2. 在 hdrl 里读 avih（宽高/帧数/帧率）、strh（fccHandler）、strf（biCompression）。
//   3. 定位 movi 列表，顺序遍历里面的 '00dc' chunk，把每帧 JPEG 原样收下。
//   4. idx1 存在时，同时按索引核对一遍（本实现以顺序遍历为准，idx1 仅作交叉校验）。
// 对不认识的 chunk 一律按长度跳过，做到"读懂认识的、跳过不认识的"。
#include "avi_demuxer.h"

#include <cstdio>
#include <cstring>

namespace cfs {

namespace {

// ---- 小端读取辅助（越界返回 0，调用方另有边界检查）----
uint32_t GetU32(const std::vector<uint8_t>& b, size_t p) {
    if (p + 4 > b.size()) return 0;
    return static_cast<uint32_t>(b[p]) |
           (static_cast<uint32_t>(b[p + 1]) << 8) |
           (static_cast<uint32_t>(b[p + 2]) << 16) |
           (static_cast<uint32_t>(b[p + 3]) << 24);
}
// 比较 b[p..p+4) 是否等于四字符码 cc（不越界）。
bool FourCCEq(const std::vector<uint8_t>& b, size_t p, const char* cc) {
    if (p + 4 > b.size()) return false;
    return b[p] == static_cast<uint8_t>(cc[0]) &&
           b[p + 1] == static_cast<uint8_t>(cc[1]) &&
           b[p + 2] == static_cast<uint8_t>(cc[2]) &&
           b[p + 3] == static_cast<uint8_t>(cc[3]);
}

}  // namespace

AviDemuxResult DemuxMjpegAvi(const std::vector<uint8_t>& b) {
    AviDemuxResult r;

    // ---- 1. 最外层 RIFF('AVI ') ----
    if (b.size() < 12 || !FourCCEq(b, 0, "RIFF") || !FourCCEq(b, 8, "AVI ")) {
        r.error = "不是合法的 RIFF/AVI 文件（缺 RIFF 或 'AVI ' 标记）";
        return r;
    }
    const uint32_t riff_size = GetU32(b, 4);
    // RIFF size 是 "文件总长 - 8"；容错：取声明值与实际长度的较小者做上界。
    size_t file_end = 8 + static_cast<size_t>(riff_size);
    if (file_end > b.size()) file_end = b.size();

    // ---- 2. 遍历 RIFF 内的顶层 chunk：hdrl / movi / idx1 ----
    size_t p = 12;  // 跳过 'RIFF' size 'AVI '
    bool found_hdrl = false, found_movi = false;

    while (p + 8 <= file_end) {
        const size_t ck_start = p;               // 指向本 chunk 的 FOURCC
        const uint32_t ck_size = GetU32(b, p + 4);
        const size_t body = p + 8;               // chunk 数据起点
        size_t body_end = body + ck_size;
        if (body_end > b.size()) body_end = b.size();

        if (FourCCEq(b, ck_start, "LIST")) {
            // LIST 的前 4 字节 body 是子列表类型（hdrl / movi / …）。
            if (FourCCEq(b, body, "hdrl")) {
                found_hdrl = true;
                // 在 hdrl 内部遍历 avih / LIST('strl') 等。
                size_t q = body + 4;
                while (q + 8 <= body_end) {
                    const size_t sub = q;
                    const uint32_t sub_size = GetU32(b, q + 4);
                    const size_t sub_body = q + 8;

                    if (FourCCEq(b, sub, "avih")) {
                        // MainAVIHeader：本实现关心 dwTotalFrames / dwWidth / dwHeight。
                        // 字段顺序见 avi_muxer.cpp：
                        //   +0 usecPerFrame, +4 maxBytesPerSec, +8 padding,
                        //   +12 flags, +16 totalFrames, +20 initialFrames,
                        //   +24 streams, +28 suggBuf, +32 width, +36 height
                        r.total_frames =
                            static_cast<int>(GetU32(b, sub_body + 16));
                        if (r.width == 0)
                            r.width = static_cast<int>(GetU32(b, sub_body + 32));
                        if (r.height == 0)
                            r.height =
                                static_cast<int>(GetU32(b, sub_body + 36));
                    } else if (FourCCEq(b, sub, "LIST") &&
                               FourCCEq(b, sub_body, "strl")) {
                        // 在 strl 内找 strh / strf。
                        size_t s = sub_body + 4;
                        const size_t strl_end = sub_body + sub_size;
                        while (s + 8 <= strl_end && s + 8 <= body_end) {
                            const size_t e = s;
                            const uint32_t e_size = GetU32(b, s + 4);
                            const size_t e_body = s + 8;
                            if (FourCCEq(b, e, "strh")) {
                                // AVIStreamHeader（字段偏移，body 起点为 0）：
                                //   +0 fccType('vids'), +4 fccHandler,
                                //   +8 dwFlags, +12 wPriority, +14 wLanguage,
                                //   +16 dwInitialFrames, +20 dwScale,
                                //   +24 dwRate → 帧率 = dwRate/dwScale
                                if (FourCCEq(b, e_body + 4, "MJPG"))
                                    r.is_mjpeg = true;
                                const uint32_t scale = GetU32(b, e_body + 20);
                                const uint32_t rate = GetU32(b, e_body + 24);
                                if (scale != 0)
                                    r.fps =
                                        static_cast<int>(rate / scale);
                            } else if (FourCCEq(b, e, "strf")) {
                                // BITMAPINFOHEADER：+0 biSize, +4 biWidth,
                                //   +8 biHeight, +16 biCompression(FOURCC)
                                if (r.width == 0)
                                    r.width =
                                        static_cast<int>(GetU32(b, e_body + 4));
                                if (r.height == 0)
                                    r.height =
                                        static_cast<int>(GetU32(b, e_body + 8));
                                if (FourCCEq(b, e_body + 16, "MJPG"))
                                    r.is_mjpeg = true;
                            }
                            // chunk 按偶数对齐推进。
                            size_t adv = 8 + e_size + (e_size & 1u);
                            if (adv < 8) break;
                            s += adv;
                        }
                    }
                    size_t adv = 8 + sub_size + (sub_size & 1u);
                    if (adv < 8) break;
                    q += adv;
                }
            } else if (FourCCEq(b, body, "movi")) {
                found_movi = true;
                r.movi_offset = body;  // 指向 'movi' FOURCC
                // 顺序遍历 movi 内的每个 chunk，收 '00dc'（压缩视频帧）。
                size_t q = body + 4;   // 跳过 'movi'
                while (q + 8 <= body_end) {
                    const size_t ce = q;
                    const uint32_t ce_size = GetU32(b, q + 4);
                    const size_t ce_body = q + 8;
                    // '00dc'=流00压缩视频；有的封装用 '00db'(未压缩)，一并接受。
                    if (FourCCEq(b, ce, "00dc") || FourCCEq(b, ce, "00db")) {
                        size_t take = ce_size;
                        if (ce_body + take > b.size())
                            take = b.size() - ce_body;
                        JpegFrameBytes frame(b.begin() + ce_body,
                                             b.begin() + ce_body + take);
                        r.frame_offsets.push_back(ce);
                        r.frame_sizes.push_back(take);
                        r.frames.push_back(std::move(frame));
                    }
                    size_t adv = 8 + ce_size + (ce_size & 1u);
                    if (adv < 8) break;
                    q += adv;
                }
            }
        } else if (FourCCEq(b, ck_start, "idx1")) {
            // idx1 存在即说明有老式索引；本实现以顺序遍历结果为准，
            // 仅标记"存在索引"供交叉校验。
            r.used_idx1 = true;
        }

        // 顶层 chunk 按偶数对齐推进。
        size_t adv = 8 + ck_size + (ck_size & 1u);
        if (adv < 8) break;  // 防御：size 异常时避免死循环
        p += adv;
    }

    if (!found_hdrl) {
        r.error = "未找到 hdrl 头部列表";
        return r;
    }
    if (!found_movi) {
        r.error = "未找到 movi 数据列表";
        return r;
    }
    if (r.frames.empty()) {
        r.error = "movi 里没有找到任何 '00dc' 帧数据";
        return r;
    }
    // 若 avih 没给出总帧数（某些封装写 0），以实际抠出的帧数为准。
    if (r.total_frames <= 0)
        r.total_frames = static_cast<int>(r.frames.size());

    r.ok = true;
    return r;
}

std::vector<uint8_t> ReadWholeFileBytes(const std::string& path) {
    std::vector<uint8_t> data;
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return data;
    std::fseek(fp, 0, SEEK_END);
    long n = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (n > 0) {
        data.resize(static_cast<size_t>(n));
        size_t got = std::fread(data.data(), 1, data.size(), fp);
        data.resize(got);
    }
    std::fclose(fp);
    return data;
}

AviDemuxResult DemuxMjpegAviFile(const std::string& path) {
    std::vector<uint8_t> bytes = ReadWholeFileBytes(path);
    if (bytes.empty()) {
        AviDemuxResult r;
        r.error = "无法读取文件: " + path;
        return r;
    }
    return DemuxMjpegAvi(bytes);
}

}  // namespace cfs
