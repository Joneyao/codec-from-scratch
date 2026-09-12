// image_io.h — 最小的 PPM (P6) 图像读写，供各编码器读入测试图片
// codec-from-scratch / common
#ifndef CODEC_FROM_SCRATCH_COMMON_IMAGE_IO_H
#define CODEC_FROM_SCRATCH_COMMON_IMAGE_IO_H

#include <cstdint>
#include <string>
#include <vector>

namespace cfs {

// 一张交错存储的 RGB 图像：data 布局为 R,G,B,R,G,B,...，每分量 8 bit。
struct RgbImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> data;  // size = width * height * 3

    uint8_t r(int x, int y) const { return data[(y * width + x) * 3 + 0]; }
    uint8_t g(int x, int y) const { return data[(y * width + x) * 3 + 1]; }
    uint8_t b(int x, int y) const { return data[(y * width + x) * 3 + 2]; }
};

// 读取二进制 PPM (P6)。失败返回 false。
bool LoadPpm(const std::string& path, RgbImage& out);

// 写出二进制 PPM (P6)，便于用系统查看器核对。
bool SavePpm(const std::string& path, const RgbImage& img);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_COMMON_IMAGE_IO_H
