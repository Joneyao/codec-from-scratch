// decoder_main.cpp — 最小 H.264 解码器命令行入口。
//
//   ./h264_decoder input.h264 output.yuv [stats.txt]
//
// 把整段 Annex B 码流解成 YUV 4:2:0（planar）。I(IDR) 帧完整重建并去块；
// P 帧的范围见 h264_decoder.h 的诚实说明。可选把统计写进 stats.txt 供配图。
#include <cstdio>
#include <string>
#include <vector>

#include "h264_decoder.h"
#include "nal_splitter.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: %s <input.h264> <output.yuv> [stats.txt]\n", argv[0]);
        return 1;
    }
    const std::string in_path = argv[1];
    const std::string out_path = argv[2];

    std::vector<uint8_t> bytes = cfs::ReadFileBytes(in_path);
    if (bytes.empty()) {
        std::printf("error: cannot read %s\n", in_path.c_str());
        return 1;
    }

    cfs::H264Decoder dec;
    std::vector<cfs::DecodedFrame> frames;
    std::string err;
    if (!dec.DecodeAnnexB(bytes, &frames, &err)) {
        std::printf("decode failed: %s\n", err.c_str());
        return 1;
    }

    std::FILE* fp = std::fopen(out_path.c_str(), "wb");
    if (!fp) {
        std::printf("error: cannot open %s for write\n", out_path.c_str());
        return 1;
    }
    int written = 0;
    for (const auto& f : frames) {
        if (!cfs::AppendFrameToYuv(f, fp)) {
            std::printf("error: write frame %d failed\n", written);
            std::fclose(fp);
            return 1;
        }
        ++written;
    }
    std::fclose(fp);

    const cfs::DecodeStats& s = dec.stats();
    std::printf("input : %s (%zu bytes)\n", in_path.c_str(), bytes.size());
    std::printf("output: %s\n", out_path.c_str());
    std::printf("decoded frames written : %d\n", written);
    std::printf("  I/IDR frames         : %d\n", s.idr_frames);
    std::printf("  P frames (skipped)   : %d\n", s.p_frames);
    std::printf("macroblocks (I frames) : %d\n", s.total_mbs);
    std::printf("  Intra_4x4            : %d\n", s.intra4x4_mbs);
    std::printf("  Intra_16x16          : %d\n", s.intra16x16_mbs);
    std::printf("  I_PCM                : %d\n", s.ipcm_mbs);
    std::printf("nonzero luma coeffs    : %ld\n", s.luma_coeffs);

    if (argc >= 4) {
        std::FILE* st = std::fopen(argv[3], "w");
        if (st) {
            std::fprintf(st, "# key value\n");
            std::fprintf(st, "frames_written %d\n", written);
            std::fprintf(st, "idr_frames %d\n", s.idr_frames);
            std::fprintf(st, "p_frames %d\n", s.p_frames);
            std::fprintf(st, "total_mbs %d\n", s.total_mbs);
            std::fprintf(st, "intra4x4_mbs %d\n", s.intra4x4_mbs);
            std::fprintf(st, "intra16x16_mbs %d\n", s.intra16x16_mbs);
            std::fprintf(st, "ipcm_mbs %d\n", s.ipcm_mbs);
            std::fprintf(st, "luma_coeffs %ld\n", s.luma_coeffs);
            std::fclose(st);
            std::printf("stats -> %s\n", argv[3]);
        }
    }
    return 0;
}
