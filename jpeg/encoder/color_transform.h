// color_transform.h — RGB 到 YCbCr 的转换与色度抽样（JPEG 编码第一步）
//
// 说明：JPEG 标准 (ITU-T T.81) 本身只处理抽象的"分量(component)"，
// 并不规定 RGB 如何变成 YCbCr。真正规定这套转换的是 JFIF / ITU-T T.871，
// 它采用 BT.601 全范围（full-range）系数。本文件实现的就是这套约定。
#ifndef CODEC_FROM_SCRATCH_JPEG_COLOR_TRANSFORM_H
#define CODEC_FROM_SCRATCH_JPEG_COLOR_TRANSFORM_H

#include <cstdint>
#include <string>
#include <vector>

#include "image_io.h"

namespace cfs {

// 单通道平面（8 bit 采样），可表示全分辨率的 Y，或抽样后的 Cb/Cr。
struct Plane {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> data;  // size = width * height

    uint8_t at(int x, int y) const { return data[y * width + x]; }
    uint8_t& at(int x, int y) { return data[y * width + x]; }
};

// 色度抽样格式。
enum class ChromaSubsampling {
    k444,  // 不抽样：Cb/Cr 与 Y 同分辨率
    k422,  // 水平 2:1
    k420,  // 水平、垂直各 2:1（JPEG/JFIF 最常用）
};

// 一张 YCbCr 图像：Y 为全分辨率，Cb/Cr 视抽样格式可能更小。
struct YCbCrImage {
    Plane y;
    Plane cb;
    Plane cr;
    ChromaSubsampling subsampling = ChromaSubsampling::k420;
};

// RGB → YCbCr（JFIF/BT.601 full-range），随后按 subsampling 对 Cb/Cr 做平均抽样。
YCbCrImage RgbToYCbCr(const RgbImage& rgb, ChromaSubsampling subsampling);

// 把某个平面导出为文本矩阵（每行一行数字，空格分隔），供 matplotlib 读取绘图。
// x0,y0 起点，w×h 大小的窗口（用于导出一个便于观察的局部块，如 8×8）。
bool DumpPlaneRegion(const Plane& p, int x0, int y0, int w, int h,
                     const std::string& path);

// 把整个平面导出为灰度 PPM（其实是把单通道复制到 R=G=B），便于直观查看。
bool SavePlaneAsGrayPpm(const Plane& p, const std::string& path);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_JPEG_COLOR_TRANSFORM_H
