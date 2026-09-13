// mjpeg_decoder_main.cpp — B2 收官命令行：一个 .avi 进，一叠 .ppm（或 .yuv）出。
//
// 这就是 MJPEG 解码的全貌，也是本篇最想说清楚的一句话：
//   MJPEG 解码 = 拆 AVI 容器（avi_demuxer） + 循环调 JPEG 解码器（jpeg/decoder）
//
// 数据流（每一步都在复用前面写好的模块，没有任何"视频解码算法"）：
//   读 .avi 字节 (avi_demuxer::ReadWholeFileBytes)
//     -> 解封装成"视频信息 + 一叠 JPEG 字节流" (DemuxMjpegAvi)
//     -> 逐帧独立 JPEG 解码 (jpeg/decoder::ParseJpeg + DecodeJpeg)
//     -> 逐帧写 frame_000.ppm... 或拼成一条 .yuv
//
// 用法:
//   mjpeg_decoder <input.avi> <output_dir>            # 输出 frame_000.ppm...
//   mjpeg_decoder <input.avi> <output.yuv> --yuv      # 输出 I420 平面序列
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "avi_demuxer.h"
#include "image_io.h"
#include "jfif_parser.h"
#include "jpeg_decoder.h"

namespace {

// RGB -> I420(YUV420p) 平面：Y 全分辨率，U/V 各 1/4（2x2 平均）。
// 仅用于 --yuv 输出，方便和 ffmpeg -pix_fmt yuv420p 的裸流比对。
void AppendI420(const cfs::RgbImage& img, std::vector<uint8_t>& out) {
    const int w = img.width, h = img.height;
    std::vector<uint8_t> yp(static_cast<size_t>(w) * h);
    std::vector<uint8_t> up(static_cast<size_t>((w + 1) / 2) * ((h + 1) / 2));
    std::vector<uint8_t> vp(up.size());
    auto clamp8 = [](int v) {
        return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int r = img.r(x, y), g = img.g(x, y), bl = img.b(x, y);
            int yy = (77 * r + 150 * g + 29 * bl) >> 8;
            yp[static_cast<size_t>(y) * w + x] = clamp8(yy);
        }
    }
    const int cw = (w + 1) / 2;
    for (int y = 0; y < h; y += 2) {
        for (int x = 0; x < w; x += 2) {
            int rs = 0, gs = 0, bs = 0, cnt = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    int px = x + dx, py = y + dy;
                    if (px < w && py < h) {
                        rs += img.r(px, py);
                        gs += img.g(px, py);
                        bs += img.b(px, py);
                        ++cnt;
                    }
                }
            if (cnt == 0) cnt = 1;
            int r = rs / cnt, g = gs / cnt, bl = bs / cnt;
            int u = ((-43 * r - 84 * g + 127 * bl) >> 8) + 128;
            int v = ((127 * r - 106 * g - 21 * bl) >> 8) + 128;
            size_t ci = static_cast<size_t>(y / 2) * cw + (x / 2);
            up[ci] = clamp8(u);
            vp[ci] = clamp8(v);
        }
    }
    out.insert(out.end(), yp.begin(), yp.end());
    out.insert(out.end(), up.begin(), up.end());
    out.insert(out.end(), vp.begin(), vp.end());
}

}  // namespace

int main(int argc, char** argv) {
    std::string in_path, out_path;
    bool yuv_mode = false;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--yuv") {
            yuv_mode = true;
        } else {
            pos.push_back(a);
        }
    }
    if (pos.size() < 2) {
        std::fprintf(stderr,
                     "用法: %s <input.avi> <output_dir>          # 逐帧 PPM\n"
                     "   或: %s <input.avi> <output.yuv> --yuv    # I420 裸流\n",
                     argv[0], argv[0]);
        return 1;
    }
    in_path = pos[0];
    out_path = pos[1];

    // ---- 1. 解封装：把 .avi 拆成"视频信息 + 一叠 JPEG 字节流" ----
    cfs::AviDemuxResult dm = cfs::DemuxMjpegAviFile(in_path);
    if (!dm.ok) {
        std::fprintf(stderr, "解封装失败: %s\n", dm.error.c_str());
        return 2;
    }
    std::printf(
        "解封装 %s : %dx%d, fps=%d, 声明帧数=%d, 实际取出=%zu 帧, "
        "MJPG=%s, movi@字节 %zu%s\n",
        in_path.c_str(), dm.width, dm.height, dm.fps, dm.total_frames,
        dm.frames.size(), dm.is_mjpeg ? "是" : "否", dm.movi_offset,
        dm.used_idx1 ? ", 含 idx1 索引" : "");

    if (!yuv_mode) {
        // 尽量创建输出目录（已存在则忽略）。
        mkdir(out_path.c_str(), 0755);
    }

    std::vector<uint8_t> yuv_stream;
    int ok_frames = 0;

    // ---- 2. 逐帧独立解码：这才是 MJPEG 解码的"循环体" ----
    for (size_t i = 0; i < dm.frames.size(); ++i) {
        const std::vector<uint8_t>& jpeg = dm.frames[i];

        // 复用 A6 解析头 + A8 完整解码，与解单张 .jpg 完全一样。
        cfs::ParseResult pr = cfs::ParseJpeg(jpeg);
        if (!pr.ok) {
            std::fprintf(stderr, "第 %zu 帧解析失败: %s\n", i, pr.error.c_str());
            continue;
        }
        cfs::DecodeResult dr = cfs::DecodeJpeg(pr.header, jpeg);
        if (!dr.ok) {
            std::fprintf(stderr, "第 %zu 帧解码失败: %s\n", i, dr.error.c_str());
            continue;
        }
        ++ok_frames;

        if (yuv_mode) {
            AppendI420(dr.image, yuv_stream);
        } else {
            char name[64];
            std::snprintf(name, sizeof(name), "/frame_%03zu.ppm", i);
            std::string fp = out_path + name;
            if (!cfs::SavePpm(fp, dr.image)) {
                std::fprintf(stderr, "写 PPM 失败: %s\n", fp.c_str());
                return 3;
            }
        }
    }

    if (yuv_mode) {
        std::FILE* fp = std::fopen(out_path.c_str(), "wb");
        if (!fp) {
            std::fprintf(stderr, "无法写 YUV: %s\n", out_path.c_str());
            return 4;
        }
        std::fwrite(yuv_stream.data(), 1, yuv_stream.size(), fp);
        std::fclose(fp);
        std::printf("解码 %d/%zu 帧 -> %s (I420, %zu 字节)\n", ok_frames,
                    dm.frames.size(), out_path.c_str(), yuv_stream.size());
    } else {
        std::printf("解码 %d/%zu 帧 -> %s/frame_000.ppm ...\n", ok_frames,
                    dm.frames.size(), out_path.c_str());
    }
    return ok_frames == static_cast<int>(dm.frames.size()) ? 0 : 5;
}
