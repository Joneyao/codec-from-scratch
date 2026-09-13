// mjpeg_encoder_main.cpp — B1 的收尾：把一串 PPM 帧编码成一个真能播放的 .avi。
//
// 数据流（每一步都在复用前面写好的模块）：
//   读一串 PPM 帧 (common/image_io)
//     -> 逐帧独立 JPEG 编码 (frame_sequencer -> jpeg/encoder/EncodeFrameToJpeg)
//     -> AVI 容器封装 (avi_muxer)
//     -> 写盘 out.avi
//
// 这里没有任何"视频算法"。MJPEG 的实现就是"JPEG 编码器 + 一个循环 + 一个容器"。
//
// 用法:
//   mjpeg_encoder <frames_dir> <out.avi> [--quality N] [--fps N]
//     frames_dir 里按文件名排序读取所有 .ppm（如 frame_000.ppm, frame_001.ppm...）
//   或显式列帧:
//   mjpeg_encoder --frames a.ppm b.ppm c.ppm --out out.avi [--quality N] [--fps N]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

#include "avi_muxer.h"
#include "frame_sequencer.h"
#include "image_io.h"

namespace {

bool HasSuffix(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() &&
           s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

// 列出目录下所有 .ppm，按文件名字典序排序（帧号补零后即帧顺序）。
std::vector<std::string> ListPpmSorted(const std::string& dir) {
    std::vector<std::string> files;
    DIR* d = opendir(dir.c_str());
    if (!d) return files;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (HasSuffix(name, ".ppm")) files.push_back(dir + "/" + name);
    }
    closedir(d);
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace

int main(int argc, char** argv) {
    int quality = 80;
    int fps = 25;
    std::string out_path;
    std::string frames_dir;
    std::vector<std::string> explicit_frames;

    // 解析参数：支持位置参数(dir out) 与 --frames ... --out 两种形式。
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--quality" && i + 1 < argc) {
            quality = std::atoi(argv[++i]);
        } else if (a == "--fps" && i + 1 < argc) {
            fps = std::atoi(argv[++i]);
        } else if (a == "--out" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (a == "--frames") {
            while (i + 1 < argc && argv[i + 1][0] != '-')
                explicit_frames.push_back(argv[++i]);
        } else {
            positional.push_back(a);
        }
    }
    if (out_path.empty() && positional.size() >= 2) {
        frames_dir = positional[0];
        out_path = positional[1];
    } else if (!positional.empty() && frames_dir.empty() &&
               explicit_frames.empty()) {
        frames_dir = positional[0];
    }

    if (out_path.empty()) {
        std::fprintf(stderr,
                     "usage: %s <frames_dir> <out.avi> [--quality N] [--fps N]\n"
                     "   or: %s --frames a.ppm b.ppm ... --out out.avi "
                     "[--quality N] [--fps N]\n",
                     argv[0], argv[0]);
        return 1;
    }

    std::vector<std::string> frame_paths = explicit_frames;
    if (frame_paths.empty()) frame_paths = ListPpmSorted(frames_dir);
    if (frame_paths.empty()) {
        std::fprintf(stderr, "no .ppm frames found (dir=%s)\n",
                     frames_dir.c_str());
        return 2;
    }

    // 读入所有帧。要求所有帧同分辨率（第一帧决定容器宽高）。
    std::vector<cfs::RgbImage> frames;
    frames.reserve(frame_paths.size());
    for (const auto& p : frame_paths) {
        cfs::RgbImage img;
        if (!cfs::LoadPpm(p, img)) {
            std::fprintf(stderr, "failed to load frame: %s\n", p.c_str());
            return 3;
        }
        if (!frames.empty() &&
            (img.width != frames[0].width || img.height != frames[0].height)) {
            std::fprintf(stderr,
                         "frame size mismatch: %s is %dx%d, expected %dx%d\n",
                         p.c_str(), img.width, img.height, frames[0].width,
                         frames[0].height);
            return 4;
        }
        frames.push_back(std::move(img));
    }

    // 逐帧独立 JPEG 编码（MJPEG 的全部"算法"）。
    std::vector<cfs::JpegFrame> jpegs =
        cfs::EncodeFrameSequence(frames, quality);

    // AVI 封装。
    cfs::AviParams ap;
    ap.width = frames[0].width;
    ap.height = frames[0].height;
    ap.fps = fps;
    std::vector<uint8_t> avi = cfs::MuxMjpegAvi(ap, jpegs);
    if (!cfs::WriteAviFile(out_path, avi)) {
        std::fprintf(stderr, "failed to write AVI: %s\n", out_path.c_str());
        return 5;
    }

    // 打印统计 + 每帧字节数（供画图脚本直接读 stdout 或转存）。
    size_t total_jpeg = 0, min_f = SIZE_MAX, max_f = 0;
    for (const auto& j : jpegs) {
        total_jpeg += j.size();
        if (j.size() < min_f) min_f = j.size();
        if (j.size() > max_f) max_f = j.size();
    }
    std::printf(
        "MJPEG: %zu frames @ %dx%d, quality=%d, fps=%d\n"
        "  AVI file = %zu bytes\n"
        "  JPEG payload total = %zu bytes (min=%zu, max=%zu, avg=%zu)\n",
        jpegs.size(), frames[0].width, frames[0].height, quality, fps,
        avi.size(), total_jpeg, min_f, max_f,
        total_jpeg / (jpegs.empty() ? 1 : jpegs.size()));
    std::printf("per-frame bytes:");
    for (const auto& j : jpegs) std::printf(" %zu", j.size());
    std::printf("\n");
    return 0;
}
