// test_avi_muxer.cpp — 校验 AVI 容器字节结构的最小单元测试。
// 不依赖外部播放器：直接读回自己写的字节，核对关键 FOURCC、长度回填、
// movi/idx1 帧数是否自洽。ffprobe/ffmpeg 的端到端识别在集成脚本里另测。
//
// 注意：用自定义 CHECK 而非 assert——Release 下 NDEBUG 会把 assert 消掉，
// 校验就形同虚设。CHECK 无论构建类型都真跑，失败即返回非零退出码。
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "avi_muxer.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::fprintf(stderr, "CHECK failed: %s (line %d)\n", #cond,  \
                         __LINE__);                                      \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

uint32_t ReadU32LE(const std::vector<uint8_t>& b, size_t p) {
    return static_cast<uint32_t>(b[p]) |
           (static_cast<uint32_t>(b[p + 1]) << 8) |
           (static_cast<uint32_t>(b[p + 2]) << 16) |
           (static_cast<uint32_t>(b[p + 3]) << 24);
}
bool FourCCAt(const std::vector<uint8_t>& b, size_t p, const char* cc) {
    return b[p] == cc[0] && b[p + 1] == cc[1] && b[p + 2] == cc[2] &&
           b[p + 3] == cc[3];
}

}  // namespace

int main() {
    // 造三帧假 JPEG 数据（内容无所谓，muxer 不解释像素）。故意用奇数长度
    // 触发 pad 分支。
    std::vector<cfs::JpegFrame> frames = {
        {0xFF, 0xD8, 0x01, 0x02, 0xFF, 0xD9},        // 6 字节（偶）
        {0xFF, 0xD8, 0xAB, 0xFF, 0xD9},              // 5 字节（奇 -> pad）
        {0xFF, 0xD8, 0x11, 0x22, 0x33, 0x44, 0xFF},  // 7 字节（奇 -> pad）
    };
    cfs::AviParams p;
    p.width = 192;
    p.height = 144;
    p.fps = 10;

    std::vector<uint8_t> avi = cfs::MuxMjpegAvi(p, frames);

    // 1) 顶层 RIFF('AVI ')，长度 = 文件长 - 8。
    CHECK(FourCCAt(avi, 0, "RIFF"));
    CHECK(ReadU32LE(avi, 4) == avi.size() - 8);
    CHECK(FourCCAt(avi, 8, "AVI "));

    // 2) 紧跟 LIST('hdrl')，其内第一个是 avih，avih 数据长度 56。
    CHECK(FourCCAt(avi, 12, "LIST"));
    CHECK(FourCCAt(avi, 20, "hdrl"));
    CHECK(FourCCAt(avi, 24, "avih"));
    CHECK(ReadU32LE(avi, 28) == 56);
    // avih.dwTotalFrames 位于 avih 数据偏移 +16，即 32 + 16 = 48。
    CHECK(ReadU32LE(avi, 32 + 16) == frames.size());

    // 3) 扫描全文件，找关键 FOURCC，并数 '00dc' 出现次数。
    bool found_movi = false, found_idx1 = false, found_strh = false,
         found_strf = false, found_mjpg = false;
    size_t dc_count = 0;
    for (size_t i = 0; i + 4 <= avi.size(); ++i) {
        if (FourCCAt(avi, i, "movi")) found_movi = true;
        if (FourCCAt(avi, i, "idx1")) found_idx1 = true;
        if (FourCCAt(avi, i, "strh")) found_strh = true;
        if (FourCCAt(avi, i, "strf")) found_strf = true;
        if (FourCCAt(avi, i, "MJPG")) found_mjpg = true;
        if (FourCCAt(avi, i, "00dc")) ++dc_count;
    }
    CHECK(found_movi);
    CHECK(found_idx1);
    CHECK(found_strh);
    CHECK(found_strf);
    CHECK(found_mjpg);
    // 每帧一个 movi chunk + 一个 idx1 项，都叫 '00dc'，共 2 * N 次。
    CHECK(dc_count == frames.size() * 2);

    // 4) idx1 段长度 = N * 16。
    bool idx1_len_ok = false;
    for (size_t i = 0; i + 8 <= avi.size(); ++i) {
        if (FourCCAt(avi, i, "idx1")) {
            idx1_len_ok = (ReadU32LE(avi, i + 4) == frames.size() * 16);
            break;
        }
    }
    CHECK(idx1_len_ok);

    if (g_failures != 0) {
        std::fprintf(stderr, "test_avi_muxer: %d FAILURE(s)\n", g_failures);
        return 1;
    }
    std::printf("test_avi_muxer: OK (%zu frames, %zu bytes)\n", frames.size(),
                avi.size());
    return 0;
}
