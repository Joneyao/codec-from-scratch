// jpeg_decoder_main.cpp — A8 收官命令行：一个 .jpg 进，一个 .ppm 出。
//
// 这就是整个 JPEG 系列（A1-A5 编码 + A6-A8 解码）拼齐后的成品：
//   ./jpeg_decoder input.jpg output.ppm
// 内部走 DecodeJpegFile 的完整流水线：解析头 → 逐 MCU 熵解码 → 反量化/IDCT
//   → 色度上采样 → YCbCr→RGB → 裁剪到真实宽高 → 写 PPM。
//
// 输出的 PPM 可以直接用系统查看器打开，也可以拿去和 libjpeg/PIL 解出的结果
// 逐像素比 PSNR——那是"手搓解码器真的能用"的硬证据。
#include <cstdio>
#include <string>

#include "image_io.h"
#include "jpeg_decoder.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "用法: %s <input.jpg> <output.ppm>\n", argv[0]);
        return 1;
    }
    const std::string in = argv[1];
    const std::string out = argv[2];

    cfs::DecodeResult r = cfs::DecodeJpegFile(in);
    if (!r.ok) {
        std::fprintf(stderr, "解码失败: %s\n", r.error.c_str());
        return 2;
    }

    if (!cfs::SavePpm(out, r.image)) {
        std::fprintf(stderr, "写 PPM 失败: %s\n", out.c_str());
        return 3;
    }

    std::printf(
        "解码 %s -> %s : %dx%d, %dx%d MCUs (每 MCU %dx%d 像素), "
        "重启间隔=%d\n",
        in.c_str(), out.c_str(), r.image.width, r.image.height,
        r.mcu_cols, r.mcu_rows, r.max_h * 8, r.max_v * 8, r.restart_interval);
    return 0;
}
