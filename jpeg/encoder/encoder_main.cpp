// encoder_main.cpp — A5 收尾：把 A1-A4 全部串起来，读一张 PPM，吐一个真能被
//                     系统查看器 / libjpeg / ffmpeg 打开的 .jpg 文件。
//
// 完整流水线（每一步对应前面一篇）：
//   读 PPM (A0/image_io)
//     -> RGB→YCbCr + 4:2:0 抽样 (A1/color_transform)
//     -> 按 MCU 顺序取 8x8 块 -> 正向 DCT (A2/dct8x8)
//     -> 量化 (A3/quantize)
//     -> Zig-Zag + DC 差分 + AC 游程 (A4/zigzag_rle)
//     -> 霍夫曼编码进 bit 流 (A4/huffman_encode)
//     -> 组装 JFIF 标记段 + byte stuffing + 写盘 (A5/jfif_writer)
//
// 编码一帧的整条流水线已抽成 cfs::EncodeFrameToJpeg（jpeg_encode_api），
// MJPEG（B1）会逐帧复用同一个函数。这里只负责读文件、调函数、写盘、打印统计。
//
// 用法: ./encoder_main input.ppm output.jpg [quality=90]
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "image_io.h"
#include "jfif_writer.h"      // cfs::WriteFile
#include "jpeg_encode_api.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: %s input.ppm output.jpg [quality=90]\n", argv[0]);
        return 1;
    }
    int quality = (argc >= 4) ? std::atoi(argv[3]) : 90;
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;

    cfs::RgbImage rgb;
    if (!cfs::LoadPpm(argv[1], rgb)) {
        std::fprintf(stderr, "failed to load PPM: %s\n", argv[1]);
        return 2;
    }

    std::vector<uint8_t> file = cfs::EncodeFrameToJpeg(rgb, quality);
    if (!cfs::WriteFile(argv[2], file)) {
        std::fprintf(stderr, "failed to write output: %s\n", argv[2]);
        return 3;
    }

    int mcus = ((rgb.width + 15) / 16) * ((rgb.height + 15) / 16);
    std::printf(
        "encoded %dx%d PPM -> %s : quality=%d, %d MCUs, file=%zu bytes\n",
        rgb.width, rgb.height, argv[2], quality, mcus, file.size());
    return 0;
}
