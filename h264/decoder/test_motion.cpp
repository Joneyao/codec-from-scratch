// test_motion.cpp — 校验运动补偿的三块核心逻辑，全部可手算核对：
//   1) 6-tap 半像素滤波：已知 6 个整像素 → ((1,-5,20,20,-5,1)·像素 + 16) >> 5。
//   2) 1/4 像素 = 最近整像素与半像素的向上取整平均。
//   3) MV 中值预测：三邻居 MV 的分量各自取中值。
//   4) 整像素 MV：直接搬块，无插值。
//   5) 色度 1/8 双线性：四角权重和为 64，整像素相位应还原原值。
#include <cstdio>
#include <vector>

#include "motion_compensate.h"

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

using cfs::MotionVector;
using cfs::RefPlane;

// 造一个 w*h 的参考平面，像素值由回调给定。
RefPlane MakePlane(int w, int h, int (*f)(int, int)) {
    RefPlane p;
    p.width = w;
    p.height = h;
    p.samples.resize(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) p.samples[y * w + x] = f(x, y);
    return p;
}

// 测试 1：6-tap 半像素滤波，手算核对。
void TestSixTap() {
    // 6 个整像素全 100：(100 - 500 + 2000 + 2000 - 500 + 100)=3200。
    // (3200 + 16) >> 5 = 3216 >> 5 = 100。常量输入应还原常量。
    CHECK(cfs::SixTapHalf(100, 100, 100, 100, 100, 100) == 100);

    // 教科书典型：(10,20,30,40,50,60)。
    // 10 - 5*20 + 20*30 + 20*40 - 5*50 + 60 = 10-100+600+800-250+60 = 1120。
    // (1120 + 16) >> 5 = 1136 >> 5 = 35。
    CHECK(cfs::SixTapHalf(10, 20, 30, 40, 50, 60) == 35);

    // 带 Clip1：极端输入不应越界 0..255。
    int v = cfs::SixTapHalf(255, 0, 255, 255, 0, 255);
    CHECK(v >= 0 && v <= 255);
}

// 测试 2：半像素位置（frac_x=2）从平面取样，等于对该行 6-tap。
// 平面每行是水平斜坡 val=10*x，则 (x,y) 与 (x+1,y) 之间半像素：
//   6 个整像素为 10*(x-2..x+3)。
void TestHalfSampleFromPlane() {
    RefPlane p = MakePlane(16, 4, [](int x, int) { return 10 * x; });
    // 在 int_x=6 处取水平半像素：像素 = 10*(4,5,6,7,8,9)=40,50,60,70,80,90。
    // 6-tap: 40 -5*50 +20*60 +20*70 -5*80 +90 = 40-250+1200+1400-400+90=2080。
    // (2080+16)>>5 = 2096>>5 = 65。恰好是 60 和 70 的中点，符合线性直觉。
    int half = cfs::SampleLumaQpel(p, 6, 1, /*frac_x=*/2, /*frac_y=*/0);
    CHECK(half == 65);
}

// 测试 3：1/4 像素 = 整像素与半像素的向上取整平均。
void TestQuarterSample() {
    RefPlane p = MakePlane(16, 4, [](int x, int) { return 10 * x; });
    // int_x=6：整像素 G=60，水平半=65（见上）。
    // frac_x=1(位置 a) = (G + b + 1)>>1 = (60+65+1)>>1 = 126>>1 = 63。
    int a = cfs::SampleLumaQpel(p, 6, 1, /*frac_x=*/1, /*frac_y=*/0);
    CHECK(a == 63);
    // frac_x=3(位置 c) = (H + b + 1)>>1，H=70：(70+65+1)>>1 = 136>>1 = 68。
    int c = cfs::SampleLumaQpel(p, 6, 1, /*frac_x=*/3, /*frac_y=*/0);
    CHECK(c == 68);
}

// 测试 4：MV 中值预测，分量各自取中值。
void TestMvMedian() {
    // A=(3,-2) B=(1,5) C=(2,1)。x 中值=Median(3,1,2)=2；y 中值=Median(-2,5,1)=1。
    MotionVector a{3, -2}, b{1, 5}, c{2, 1};
    MotionVector mvp = cfs::PredictMvMedian(a, b, c);
    CHECK(mvp.x == 2);
    CHECK(mvp.y == 1);

    // 重复值：A=B=(4,4), C=(9,0)。中值应为 4/4（两个相同值占多数）。
    MotionVector p = cfs::PredictMvMedian({4, 4}, {4, 4}, {9, 0});
    CHECK(p.x == 4);
    CHECK(p.y == 4);

    // 还原真正 MV：mvp + mvd。
    MotionVector mv = cfs::ReconstructMv({2, 1}, {-1, 3});
    CHECK(mv.x == 1);
    CHECK(mv.y == 4);
}

// 测试 5：整像素 MV 直接搬块，无插值。
// 平面 val = x + 100*y。MV=(4,8) 即整整右 1、下 2 像素（1/4 单位 4=1 像素）。
void TestIntegerMvCopy() {
    RefPlane p = MakePlane(16, 16, [](int x, int y) { return x + 100 * y; });
    MotionVector mv{4, 8};  // 右 1 像素、下 2 像素
    cfs::PredBlock blk = cfs::MotionCompensateLuma(p, 4, 4, 4, 4, mv);
    // 预测块 (0,0) 应等于参考帧 (4+1, 4+2)=(5,6) 的像素 = 5 + 600 = 605？
    // 注意像素值本身可 >255（这里仅测搬运位置正确，不做 Clip）。
    // 整像素路径不经过 Clip1，直接返回 ref.at。
    CHECK(blk.at(0, 0) == p.at(5, 6));
    CHECK(blk.at(3, 3) == p.at(8, 9));
    // 零 MV：预测块 == 原位置块。
    cfs::PredBlock z = cfs::MotionCompensateLuma(p, 4, 4, 4, 4, {0, 0});
    CHECK(z.at(0, 0) == p.at(4, 4));
    CHECK(z.at(2, 1) == p.at(6, 5));
}

// 测试 6：色度 1/8 双线性。整像素相位(0,0)应还原 A；四角权重和 = 64。
void TestChromaBilinear() {
    RefPlane p = MakePlane(8, 8, [](int x, int y) { return 10 * x + y; });
    // frac=(0,0)：应等于 A=p.at(3,3)=30+3=33。
    CHECK(cfs::SampleChromaEpel(p, 3, 3, 0, 0) == p.at(3, 3));
    // frac=(4,0)：水平半程，= (A+B)/2 的四舍五入。A=33,B=43。
    // ((8-4)*8*33 + 4*8*43 + 0 + 0 + 32)>>6 = (32*33 + 32*43 + 32)>>6
    //  = (1056 + 1376 + 32)>>6 = 2464>>6 = 38。
    CHECK(cfs::SampleChromaEpel(p, 3, 3, 4, 0) == 38);
    // frac=(4,4)：正中心，四点等权。A=33,B=43,C=34,D=44。
    // (16*33+16*43+16*34+16*44+32)>>6 = (16*154+32)>>6 = (2464+32)>>6=2496>>6=39。
    CHECK(cfs::SampleChromaEpel(p, 3, 3, 4, 4) == 39);
}

// 测试 7：Median3 直接校验。
void TestMedian3() {
    CHECK(cfs::Median3(1, 2, 3) == 2);
    CHECK(cfs::Median3(3, 1, 2) == 2);
    CHECK(cfs::Median3(-5, -5, 10) == -5);
    CHECK(cfs::Median3(7, 7, 7) == 7);
}

}  // namespace

int main() {
    TestSixTap();
    TestHalfSampleFromPlane();
    TestQuarterSample();
    TestMvMedian();
    TestIntegerMvCopy();
    TestChromaBilinear();
    TestMedian3();

    if (g_failures != 0) {
        std::fprintf(stderr, "test_motion: %d FAILURE(s)\n", g_failures);
        return 1;
    }
    std::printf("test_motion: 全部通过\n");
    return 0;
}
