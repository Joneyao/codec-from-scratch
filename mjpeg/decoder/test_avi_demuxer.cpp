// test_avi_demuxer.cpp — 校验 AVI 解封装的最小单元测试。
// 直接解析 B1 真实产出的 out.avi（由 avi_muxer 写出），核对：
//   - 视频信息：192x144、20 帧、fps 10、识别为 MJPG
//   - 每帧抠出的字节流是合法 JPEG（FFD8 开头、FFD9 结尾）
//   - demuxer 解出的帧数与 avih 声明的 dwTotalFrames 一致
//
// 用自定义 CHECK 而非 assert——Release 下 NDEBUG 会把 assert 消掉。
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "avi_demuxer.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "CHECK failed: %s (line %d)\n", #cond, \
                         __LINE__);                                     \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

}  // namespace

int main(int argc, char** argv) {
    // 允许命令行覆盖路径，默认取 encoder 目录里的 out.avi。
    std::string avi_path =
        argc > 1 ? argv[1] : "../../encoder/out.avi";

    cfs::AviDemuxResult r = cfs::DemuxMjpegAviFile(avi_path);
    if (!r.ok) {
        std::fprintf(stderr, "解封装失败(%s): %s\n", avi_path.c_str(),
                     r.error.c_str());
        return 1;
    }

    // 1) 视频信息与 B1 编码参数一致。
    CHECK(r.width == 192);
    CHECK(r.height == 144);
    CHECK(r.fps == 10);
    CHECK(r.total_frames == 20);
    CHECK(r.is_mjpeg);

    // 2) 实际抠出的帧数与声明帧数一致。
    CHECK(r.frames.size() == 20);
    CHECK(r.frame_offsets.size() == r.frames.size());
    CHECK(r.frame_sizes.size() == r.frames.size());

    // 3) 每帧都是合法 JPEG：FFD8 开头、FFD9 结尾，且非空。
    for (size_t i = 0; i < r.frames.size(); ++i) {
        const auto& f = r.frames[i];
        CHECK(f.size() >= 4);
        if (f.size() >= 2) {
            CHECK(f[0] == 0xFF && f[1] == 0xD8);  // SOI
        }
        if (f.size() >= 2) {
            CHECK(f[f.size() - 2] == 0xFF && f[f.size() - 1] == 0xD9);  // EOI
        }
        // 抠出的字节长度应与记录的 frame_sizes 相符。
        CHECK(f.size() == r.frame_sizes[i]);
    }

    // 4) movi 偏移应指向文件内一个合法位置（大于头部）。
    CHECK(r.movi_offset > 0);

    if (g_failures != 0) {
        std::fprintf(stderr, "test_avi_demuxer: %d FAILURE(s)\n", g_failures);
        return 1;
    }
    std::printf(
        "test_avi_demuxer: OK (%dx%d, %d 帧 @ fps %d, MJPG=%s, "
        "第一帧 %zu 字节)\n",
        r.width, r.height, r.total_frames, r.fps, r.is_mjpeg ? "是" : "否",
        r.frames[0].size());
    return 0;
}
