// color_transform.cpp — RGB→YCbCr + 色度抽样
#include "color_transform.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace cfs {

namespace {

// 把浮点结果夹到 [0,255] 并四舍五入到 8 bit。
uint8_t ClampToByte(double v) {
    int iv = static_cast<int>(std::lround(v));
    if (iv < 0) iv = 0;
    if (iv > 255) iv = 255;
    return static_cast<uint8_t>(iv);
}

// JFIF / ITU-T T.871 full-range BT.601 系数。
// Y  =  0.299 R + 0.587 G + 0.114 B
// Cb = -0.168736 R - 0.331264 G + 0.5 B + 128
// Cr =  0.5 R - 0.418688 G - 0.081312 B + 128
void RgbPixelToYCbCr(uint8_t r, uint8_t g, uint8_t b,
                     uint8_t& y, uint8_t& cb, uint8_t& cr) {
    double rf = r, gf = g, bf = b;
    y = ClampToByte(0.299 * rf + 0.587 * gf + 0.114 * bf);
    cb = ClampToByte(-0.168736 * rf - 0.331264 * gf + 0.5 * bf + 128.0);
    cr = ClampToByte(0.5 * rf - 0.418688 * gf - 0.081312 * bf + 128.0);
}

// 对全分辨率的色度平面做 box 平均抽样。
// factor_x / factor_y 为 1 或 2。
Plane BoxDownsample(const Plane& full, int factor_x, int factor_y) {
    Plane out;
    out.width = (full.width + factor_x - 1) / factor_x;
    out.height = (full.height + factor_y - 1) / factor_y;
    out.data.resize(static_cast<size_t>(out.width) * out.height);

    for (int oy = 0; oy < out.height; ++oy) {
        for (int ox = 0; ox < out.width; ++ox) {
            int sum = 0, cnt = 0;
            for (int dy = 0; dy < factor_y; ++dy) {
                for (int dx = 0; dx < factor_x; ++dx) {
                    int sx = ox * factor_x + dx;
                    int sy = oy * factor_y + dy;
                    if (sx < full.width && sy < full.height) {
                        sum += full.at(sx, sy);
                        ++cnt;
                    }
                }
            }
            out.at(ox, oy) = static_cast<uint8_t>((sum + cnt / 2) / cnt);
        }
    }
    return out;
}

}  // namespace

YCbCrImage RgbToYCbCr(const RgbImage& rgb, ChromaSubsampling subsampling) {
    YCbCrImage out;
    out.subsampling = subsampling;

    // Y 与全分辨率 Cb/Cr 先算出来。
    out.y.width = rgb.width;
    out.y.height = rgb.height;
    out.y.data.resize(static_cast<size_t>(rgb.width) * rgb.height);

    Plane cb_full, cr_full;
    cb_full.width = cr_full.width = rgb.width;
    cb_full.height = cr_full.height = rgb.height;
    cb_full.data.resize(out.y.data.size());
    cr_full.data.resize(out.y.data.size());

    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            uint8_t yy, cb, cr;
            RgbPixelToYCbCr(rgb.r(x, y), rgb.g(x, y), rgb.b(x, y), yy, cb, cr);
            out.y.at(x, y) = yy;
            cb_full.at(x, y) = cb;
            cr_full.at(x, y) = cr;
        }
    }

    int fx = 1, fy = 1;
    switch (subsampling) {
        case ChromaSubsampling::k444: fx = 1; fy = 1; break;
        case ChromaSubsampling::k422: fx = 2; fy = 1; break;
        case ChromaSubsampling::k420: fx = 2; fy = 2; break;
    }
    out.cb = (fx == 1 && fy == 1) ? cb_full : BoxDownsample(cb_full, fx, fy);
    out.cr = (fx == 1 && fy == 1) ? cr_full : BoxDownsample(cr_full, fx, fy);
    return out;
}

bool DumpPlaneRegion(const Plane& p, int x0, int y0, int w, int h,
                     const std::string& path) {
    std::ofstream os(path);
    if (!os) return false;
    for (int y = y0; y < y0 + h; ++y) {
        for (int x = x0; x < x0 + w; ++x) {
            int v = (x < p.width && y < p.height) ? p.at(x, y) : 0;
            os << v;
            if (x < x0 + w - 1) os << ' ';
        }
        os << '\n';
    }
    return static_cast<bool>(os);
}

bool SavePlaneAsGrayPpm(const Plane& p, const std::string& path) {
    RgbImage img;
    img.width = p.width;
    img.height = p.height;
    img.data.resize(static_cast<size_t>(p.width) * p.height * 3);
    for (int i = 0; i < p.width * p.height; ++i) {
        uint8_t v = p.data[i];
        img.data[i * 3 + 0] = v;
        img.data[i * 3 + 1] = v;
        img.data[i * 3 + 2] = v;
    }
    return SavePpm(path, img);
}

}  // namespace cfs
