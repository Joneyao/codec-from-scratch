// motion_compensate.cpp — 运动补偿实现，严格照 ITU-T H.264 8.4。
#include "motion_compensate.h"

#include <algorithm>

namespace cfs {

namespace {

// Clip1_Y：把亮度样本钳到 0..255（8 bit）。spec 里 Clip1Y(x)=Clip3(0,255,x)。
inline int Clip1(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

}  // namespace

int Median3(int a, int b, int c) {
    // 取中值 = a+b+c - max - min，避免多次比较。
    return a + b + c - std::max({a, b, c}) - std::min({a, b, c});
}

MotionVector PredictMvMedian(const MotionVector& mv_a, const MotionVector& mv_b,
                             const MotionVector& mv_c) {
    // spec 式 8-165 / 8-166：两个分量各自取三邻居的中值。
    MotionVector mvp;
    mvp.x = Median3(mv_a.x, mv_b.x, mv_c.x);
    mvp.y = Median3(mv_a.y, mv_b.y, mv_c.y);
    return mvp;
}

MotionVector ReconstructMv(const MotionVector& mvp, const MotionVector& mvd) {
    return {mvp.x + mvd.x, mvp.y + mvd.y};
}

int SixTapHalf(int p0, int p1, int p2, int p3, int p4, int p5) {
    // spec 式 8-185：b1 = (E - 5F + 20G + 20H - 5I + J)。
    int b1 = p0 - 5 * p1 + 20 * p2 + 20 * p3 - 5 * p4 + p5;
    // spec 式 8-187：b = Clip1((b1 + 16) >> 5)。
    return Clip1((b1 + 16) >> 5);
}

// 水平 6-tap 半像素：中心在整像素 (x,y) 与 (x+1,y) 之间。
static int HalfH(const RefPlane& ref, int x, int y) {
    return SixTapHalf(ref.at(x - 2, y), ref.at(x - 1, y), ref.at(x, y),
                      ref.at(x + 1, y), ref.at(x + 2, y), ref.at(x + 3, y));
}

// 垂直 6-tap 半像素：中心在整像素 (x,y) 与 (x,y+1) 之间。
static int HalfV(const RefPlane& ref, int x, int y) {
    return SixTapHalf(ref.at(x, y - 2), ref.at(x, y - 1), ref.at(x, y),
                      ref.at(x, y + 1), ref.at(x, y + 2), ref.at(x, y + 3));
}

// 中心半像素 j（对角双半）：先水平 6-tap 出一列中间值 b1，再对 b1 竖直 6-tap。
// spec 式 8-189/8-191：j = Clip1((j1 + 512) >> 10)。这里用未钳的中间值 b1。
static int b1_h(const RefPlane& ref, int x, int y) {
    return ref.at(x - 2, y) - 5 * ref.at(x - 1, y) + 20 * ref.at(x, y) +
           20 * ref.at(x + 1, y) - 5 * ref.at(x + 2, y) + ref.at(x + 3, y);
}

static int CenterHalf(const RefPlane& ref, int x, int y) {
    // 对 6 行中间值 b1 再做一次 6-tap（垂直方向）。
    int cc = b1_h(ref, x, y - 2);
    int dd = b1_h(ref, x, y - 1);
    int h1 = b1_h(ref, x, y);
    int m1 = b1_h(ref, x, y + 1);
    int ee = b1_h(ref, x, y + 2);
    int ff = b1_h(ref, x, y + 3);
    int j1 = cc - 5 * dd + 20 * h1 + 20 * m1 - 5 * ee + ff;
    return Clip1((j1 + 512) >> 10);
}

int SampleLumaQpel(const RefPlane& ref, int int_x, int int_y, int frac_x,
                   int frac_y) {
    // frac 相位对应 spec Figure 8-4 的 a..s 位置。用整像素 G 与半像素
    // b(水平半)/h(竖直半)/j(中心半)组合，1/4 位置再取平均(向上取整)。
    // G = 整像素本身。
    const int G = ref.at(int_x, int_y);

    // 3 个半像素邻位（可能用到）。
    auto b = [&]() { return HalfH(ref, int_x, int_y); };       // 水平半(G-H 间)
    auto h = [&]() { return HalfV(ref, int_x, int_y); };       // 竖直半(G-M 间)
    auto j = [&]() { return CenterHalf(ref, int_x, int_y); };  // 中心半

    if (frac_x == 0 && frac_y == 0) return G;                  // 整像素
    if (frac_x == 2 && frac_y == 0) return b();                // 半(水平)
    if (frac_x == 0 && frac_y == 2) return h();                // 半(竖直)
    if (frac_x == 2 && frac_y == 2) return j();                // 半(中心)

    // 1/4 像素：与最近的整/半像素做向上取整平均（spec 式 8-194..8-209）。
    // 只覆盖 frac ∈ {0,1,2,3}²。为保持实现清晰，用"最近整/半样本平均"通式。
    // 先算出该 frac 落点周围的四个锚点样本：
    //   左上锚点值取决于 frac_x/frac_y 是否 >=2。
    auto avg = [](int a, int b2) { return (a + b2 + 1) >> 1; };

    // 水平方向：frac_x==0 用 G 那一列，frac_x==2 用半像素列，1/3 介于其间。
    // 竖直方向同理。我们按 spec 逐个位置列公式（a,c,d,n,e,f,g,i,k,p,q,r,s,m）。
    // 为覆盖全部 1/4 组合，用如下映射（H=右邻整像素，M=下邻整像素）：
    const int H = ref.at(int_x + 1, int_y);
    const int M = ref.at(int_x, int_y + 1);

    // 各半像素只在需要时求值。
    if (frac_y == 0) {
        // 只在水平方向取分数：a=(G+b)/2, c=(H+b)/2。
        int bb = b();
        if (frac_x == 1) return avg(G, bb);   // a
        if (frac_x == 3) return avg(H, bb);   // c
    }
    if (frac_x == 0) {
        // 只在竖直方向取分数：d=(G+h)/2, n=(M+h)/2。
        int hh = h();
        if (frac_y == 1) return avg(G, hh);   // d
        if (frac_y == 3) return avg(M, hh);   // n
    }
    // 其余对角 1/4 位置：与两个最近半像素做平均（e,g,p,r），
    // 以及与中心 j 相关的 f,i,k,q。
    int bb = b();
    int hh = h();
    int jj = j();
    // 右侧竖直半（H-? 间）与下侧水平半，用于对角平均。
    int s_half = HalfH(ref, int_x, int_y + 1);  // 下一行的水平半(s)
    int m_half = HalfV(ref, int_x + 1, int_y);  // 右一列的竖直半(m)

    // spec 式 8-194..8-209 的对角/中心组合：
    if (frac_x == 1 && frac_y == 1) return avg(bb, hh);      // e
    if (frac_x == 3 && frac_y == 1) return avg(bb, m_half);  // g
    if (frac_x == 1 && frac_y == 3) return avg(hh, s_half);  // p
    if (frac_x == 3 && frac_y == 3) return avg(m_half, s_half);  // r
    if (frac_x == 2 && frac_y == 1) return avg(bb, jj);      // f
    if (frac_x == 1 && frac_y == 2) return avg(hh, jj);      // i
    if (frac_x == 3 && frac_y == 2) return avg(jj, m_half);  // k
    if (frac_x == 2 && frac_y == 3) return avg(jj, s_half);  // q

    return G;  // 理论上不会到这
}

PredBlock MotionCompensateLuma(const RefPlane& ref, int bx, int by, int w,
                               int h, const MotionVector& mv) {
    PredBlock out;
    out.w = w;
    out.h = h;
    out.pix.resize(static_cast<size_t>(w) * h);

    // MV 单位是 1/4 像素：整数位移 = mv >> 2，分数相位 = mv & 3。
    // 用算术右移处理负数：C++ 对负数右移是实现定义，改用 floor 除法。
    auto floordiv4 = [](int v) { return (v >> 2); };  // 对 2 的补码等价 floor(v/4)
    const int int_dx = floordiv4(mv.x);
    const int int_dy = floordiv4(mv.y);
    const int frac_x = mv.x & 3;
    const int frac_y = mv.y & 3;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sx = bx + x + int_dx;
            int sy = by + y + int_dy;
            out.pix[static_cast<size_t>(y) * w + x] =
                SampleLumaQpel(ref, sx, sy, frac_x, frac_y);
        }
    }
    return out;
}

int SampleChromaEpel(const RefPlane& ref, int int_x, int int_y, int frac_x,
                     int frac_y) {
    // spec 式 8-214：双线性 1/8 像素。
    int A = ref.at(int_x, int_y);
    int B = ref.at(int_x + 1, int_y);
    int C = ref.at(int_x, int_y + 1);
    int D = ref.at(int_x + 1, int_y + 1);
    int val = (8 - frac_x) * (8 - frac_y) * A + frac_x * (8 - frac_y) * B +
              (8 - frac_x) * frac_y * C + frac_x * frac_y * D + 32;
    return Clip1(val >> 6);
}

int SadBlock(const PredBlock& pred, const PredBlock& orig) {
    int sad = 0;
    const int n = pred.w * pred.h;
    for (int i = 0; i < n; ++i) {
        int d = pred.pix[i] - orig.pix[i];
        sad += d < 0 ? -d : d;
    }
    return sad;
}

}  // namespace cfs
