// test_color_transform.cpp — color_transform 的最小单元测试（无第三方框架）
#include <cmath>
#include <cstdio>

#include "color_transform.h"

namespace {
int g_failed = 0;

void Check(bool cond, const char* msg) {
    if (!cond) {
        std::printf("[FAIL] %s\n", msg);
        ++g_failed;
    } else {
        std::printf("[ok]   %s\n", msg);
    }
}

cfs::RgbImage MakeSolid(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    cfs::RgbImage img;
    img.width = w;
    img.height = h;
    img.data.resize(static_cast<size_t>(w) * h * 3);
    for (int i = 0; i < w * h; ++i) {
        img.data[i * 3 + 0] = r;
        img.data[i * 3 + 1] = g;
        img.data[i * 3 + 2] = b;
    }
    return img;
}
}  // namespace

int main() {
    // 1. 纯白 RGB(255,255,255) → Y≈255, Cb≈128, Cr≈128
    {
        auto img = cfs::RgbToYCbCr(MakeSolid(8, 8, 255, 255, 255),
                                   cfs::ChromaSubsampling::k444);
        Check(img.y.at(0, 0) == 255, "white -> Y=255");
        Check(std::abs(int(img.cb.at(0, 0)) - 128) <= 1, "white -> Cb~128");
        Check(std::abs(int(img.cr.at(0, 0)) - 128) <= 1, "white -> Cr~128");
    }
    // 2. 纯黑 → Y=0, Cb≈128, Cr≈128
    {
        auto img = cfs::RgbToYCbCr(MakeSolid(8, 8, 0, 0, 0),
                                   cfs::ChromaSubsampling::k444);
        Check(img.y.at(0, 0) == 0, "black -> Y=0");
        Check(std::abs(int(img.cb.at(0, 0)) - 128) <= 1, "black -> Cb~128");
    }
    // 3. 纯红 RGB(255,0,0) → Y≈76, Cr 明显 >128（红色 Cr 大）
    {
        auto img = cfs::RgbToYCbCr(MakeSolid(8, 8, 255, 0, 0),
                                   cfs::ChromaSubsampling::k444);
        Check(std::abs(int(img.y.at(0, 0)) - 76) <= 1, "red -> Y~76");
        Check(img.cr.at(0, 0) > 200, "red -> Cr large");
    }
    // 4. 4:2:0 抽样后 Cb 平面尺寸减半
    {
        auto img = cfs::RgbToYCbCr(MakeSolid(16, 16, 100, 150, 200),
                                   cfs::ChromaSubsampling::k420);
        Check(img.y.width == 16 && img.y.height == 16, "420 Y full res");
        Check(img.cb.width == 8 && img.cb.height == 8, "420 Cb half res");
    }
    // 5. 纯色抽样后色度值不变（box 平均对常量不变）
    {
        auto img = cfs::RgbToYCbCr(MakeSolid(16, 16, 30, 200, 90),
                                   cfs::ChromaSubsampling::k420);
        auto ref = cfs::RgbToYCbCr(MakeSolid(16, 16, 30, 200, 90),
                                   cfs::ChromaSubsampling::k444);
        Check(img.cb.at(0, 0) == ref.cb.at(0, 0), "solid color survives 420");
    }

    if (g_failed == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return 0;
    }
    std::printf("\n%d TEST(S) FAILED\n", g_failed);
    return 1;
}
