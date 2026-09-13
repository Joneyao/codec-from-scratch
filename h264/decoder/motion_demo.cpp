// motion_demo.cpp — 用真实图像演示运动补偿与亚像素插值的价值。
//
// 做四件事，全部基于真实像素、不手编数字：
//   1) 读一张真实测试图，取它的亮度平面当"参考帧"。
//   2) 造一个"当前块"：把参考帧某位置的块，按一个真实的分数位移搬过来，
//      作为"物体移动了半个多像素"后的样子（用高精度插值生成，模拟真实运动）。
//   3) 关键对比：用不同精度的 MV 去预测这个当前块——
//        - 只能整像素对齐（把 MV 四舍五入到整像素）
//        - 半像素对齐
//        - 完整 1/4 像素对齐
//      分别算预测块与当前块的 SAD，展示亚像素插值让残差大幅变小。
//   4) MV 中值预测：给三个真实邻居 MV，算出预测 MV，再配 mvd 还原。
//
// 用法：./motion_demo ../../samples/camera_photo.ppm [chart_data]
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "image_io.h"
#include "motion_compensate.h"

namespace {

inline int Luma(int r, int g, int b) {
    return (77 * r + 150 * g + 29 * b + 128) >> 8;
}

// 把整张 RGB 图转成一张亮度参考平面。
cfs::RefPlane ToRefPlane(const cfs::RgbImage& img) {
    cfs::RefPlane p;
    p.width = img.width;
    p.height = img.height;
    p.samples.resize(static_cast<size_t>(p.width) * p.height);
    for (int y = 0; y < p.height; ++y)
        for (int x = 0; x < p.width; ++x)
            p.samples[y * p.width + x] =
                Luma(img.r(x, y), img.g(x, y), img.b(x, y));
    return p;
}

void PrintBlock(const char* title, const cfs::PredBlock& b) {
    std::printf("%s:\n", title);
    for (int y = 0; y < b.h; ++y) {
        std::printf("  ");
        for (int x = 0; x < b.w; ++x) std::printf("%4d", b.at(x, y));
        std::printf("\n");
    }
}

void DumpBlock(FILE* f, const cfs::PredBlock& b) {
    for (int y = 0; y < b.h; ++y) {
        for (int x = 0; x < b.w; ++x)
            std::fprintf(f, "%d%s", b.at(x, y), x == b.w - 1 ? "" : " ");
        std::fprintf(f, "\n");
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string ppm = argc > 1 ? argv[1] : "../../samples/camera_photo.ppm";
    std::string out_dir = argc > 2 ? argv[2] : "chart_data";

    cfs::RgbImage img;
    if (!cfs::LoadPpm(ppm, img)) {
        std::printf("error: cannot load %s\n", ppm.c_str());
        return 1;
    }
    std::printf("input: %s  (%dx%d)\n", ppm.c_str(), img.width, img.height);
    cfs::RefPlane ref = ToRefPlane(img);

    // ---- 演示 1：亚像素插值的价值 ----
    // 选一个纹理丰富的位置当"当前块"的原始位置。
    const int bx = 120, by = 80, W = 8, H = 8;

    // 用一个"真实的分数运动"生成当前块：物体相对参考帧向右移动了
    // 5/4 像素、向下 3/4 像素（MV=(5,3)，1/4 像素单位）。这个当前块用完整
    // 1/4 像素插值从参考帧取出，代表"物体真的移到了亚像素位置"的样子。
    const cfs::MotionVector true_mv{5, 3};
    cfs::PredBlock current = cfs::MotionCompensateLuma(ref, bx, by, W, H, true_mv);

    // 现在解码端要预测这个当前块。分别用三种精度的 MV：
    // (a) 只能整像素：把 true_mv 四舍五入到最近整像素 = (4,4)。
    const cfs::MotionVector mv_int{4, 4};
    // (b) 半像素精度：把 true_mv 量化到最近半像素 = (6,4)（1/4 单位里半像素=2 的倍数）。
    const cfs::MotionVector mv_half{6, 4};
    // (c) 完整 1/4 像素：就是 true_mv 本身 = (5,3)。
    const cfs::MotionVector mv_qpel = true_mv;

    cfs::PredBlock pred_int = cfs::MotionCompensateLuma(ref, bx, by, W, H, mv_int);
    cfs::PredBlock pred_half =
        cfs::MotionCompensateLuma(ref, bx, by, W, H, mv_half);
    cfs::PredBlock pred_qpel =
        cfs::MotionCompensateLuma(ref, bx, by, W, H, mv_qpel);

    int sad_int = cfs::SadBlock(pred_int, current);
    int sad_half = cfs::SadBlock(pred_half, current);
    int sad_qpel = cfs::SadBlock(pred_qpel, current);

    std::printf("\n==== 演示 1：亚像素插值让预测更贴合 ====\n");
    std::printf("当前块 @(%d,%d) %dx%d，由真实分数运动 MV=(%d,%d)[1/4像素] 生成\n",
                bx, by, W, H, true_mv.x, true_mv.y);
    PrintBlock("当前块（要预测的目标）", current);
    std::printf("\n三种 MV 精度的预测块 SAD（越小越准）：\n");
    std::printf("  只能整像素  MV=(%d,%d)  SAD=%d\n", mv_int.x, mv_int.y, sad_int);
    std::printf("  半像素      MV=(%d,%d)  SAD=%d\n", mv_half.x, mv_half.y,
                sad_half);
    std::printf("  1/4 像素    MV=(%d,%d)  SAD=%d\n", mv_qpel.x, mv_qpel.y,
                sad_qpel);
    std::printf("  --> 整像素 → 1/4 像素，SAD 从 %d 降到 %d（降幅 %.1f%%）\n",
                sad_int, sad_qpel,
                sad_int ? 100.0 * (sad_int - sad_qpel) / sad_int : 0.0);

    // ---- 演示 2：6-tap 半像素插值的逐值展开 ----
    // 取参考帧某一行连续 6 个整像素，展示 6-tap 如何插出中间的半像素。
    const int sx = 120, sy = 80;
    std::array<int, 6> taps = {ref.at(sx - 2, sy), ref.at(sx - 1, sy),
                               ref.at(sx, sy),     ref.at(sx + 1, sy),
                               ref.at(sx + 2, sy), ref.at(sx + 3, sy)};
    int b1 = taps[0] - 5 * taps[1] + 20 * taps[2] + 20 * taps[3] - 5 * taps[4] +
             taps[5];
    int half = cfs::SixTapHalf(taps[0], taps[1], taps[2], taps[3], taps[4],
                               taps[5]);
    int qpel_a = (taps[2] + half + 1) >> 1;  // 1/4 位置 a = (G + b + 1)>>1
    std::printf("\n==== 演示 2：6-tap 半像素插值（真实像素）====\n");
    std::printf("整像素行 @(%d,%d) 附近 6 个样本：", sx, sy);
    for (int t : taps) std::printf(" %d", t);
    std::printf("\n6-tap 加权(1,-5,20,20,-5,1)：b1 = %d\n", b1);
    std::printf("半像素 b = Clip1((%d+16)>>5) = %d\n", b1, half);
    std::printf("1/4 像素 a = (整像素%d + 半像素%d + 1)>>1 = %d\n", taps[2],
                half, qpel_a);

    // ---- 演示 3：MV 中值预测 ----
    // 三个邻居块的真实 MV（模拟一段平移运动，相邻块运动相近）。
    const cfs::MotionVector mv_a{8, -4};   // 左邻 A
    const cfs::MotionVector mv_b{6, -3};   // 上邻 B
    const cfs::MotionVector mv_c{10, -6};  // 右上邻 C
    cfs::MotionVector mvp = cfs::PredictMvMedian(mv_a, mv_b, mv_c);
    const cfs::MotionVector mvd{1, 1};  // 码流里的差分
    cfs::MotionVector mv_final = cfs::ReconstructMv(mvp, mvd);
    std::printf("\n==== 演示 3：MV 中值预测 ====\n");
    std::printf("左邻 A=(%d,%d)  上邻 B=(%d,%d)  右上 C=(%d,%d)\n", mv_a.x,
                mv_a.y, mv_b.x, mv_b.y, mv_c.x, mv_c.y);
    std::printf("预测 MV = 分量中值 = (Median(%d,%d,%d), Median(%d,%d,%d)) "
                "= (%d,%d)\n",
                mv_a.x, mv_b.x, mv_c.x, mv_a.y, mv_b.y, mv_c.y, mvp.x, mvp.y);
    std::printf("码流只需传 mvd=(%d,%d)，真正 MV = mvp + mvd = (%d,%d)\n", mvd.x,
                mvd.y, mv_final.x, mv_final.y);
    std::printf("相邻块运动相近 → 预测准 → mvd 很小 → 省码字\n");

    // ---- dump 供 gen_charts.py ----
    if (!out_dir.empty()) {
        std::string path = out_dir + "/motion_dump.txt";
        FILE* f = std::fopen(path.c_str(), "w");
        if (f) {
            std::fprintf(f, "# H.264 motion compensation demo dump（真实数据）\n");
            std::fprintf(f, "image %s %d %d\n", ppm.c_str(), img.width,
                         img.height);
            std::fprintf(f, "block %d %d %d %d\n", bx, by, W, H);
            std::fprintf(f, "true_mv %d %d\n", true_mv.x, true_mv.y);

            // 演示 1：三种精度的 SAD 与预测块。
            std::fprintf(f, "sad int %d %d %d\n", mv_int.x, mv_int.y, sad_int);
            std::fprintf(f, "sad half %d %d %d\n", mv_half.x, mv_half.y,
                         sad_half);
            std::fprintf(f, "sad qpel %d %d %d\n", mv_qpel.x, mv_qpel.y,
                         sad_qpel);
            std::fprintf(f, "current\n");
            DumpBlock(f, current);
            std::fprintf(f, "pred_int\n");
            DumpBlock(f, pred_int);
            std::fprintf(f, "pred_half\n");
            DumpBlock(f, pred_half);
            std::fprintf(f, "pred_qpel\n");
            DumpBlock(f, pred_qpel);

            // 演示 2：6-tap。
            std::fprintf(f, "taps");
            for (int t : taps) std::fprintf(f, " %d", t);
            std::fprintf(f, "\n");
            std::fprintf(f, "sixtap b1 %d half %d qpel_a %d G %d\n", b1, half,
                         qpel_a, taps[2]);

            // 演示 3：MV 中值。
            std::fprintf(f, "mv_a %d %d\n", mv_a.x, mv_a.y);
            std::fprintf(f, "mv_b %d %d\n", mv_b.x, mv_b.y);
            std::fprintf(f, "mv_c %d %d\n", mv_c.x, mv_c.y);
            std::fprintf(f, "mvp %d %d\n", mvp.x, mvp.y);
            std::fprintf(f, "mvd %d %d\n", mvd.x, mvd.y);
            std::fprintf(f, "mv_final %d %d\n", mv_final.x, mv_final.y);
            std::fclose(f);
            std::printf("\ndumped motion data -> %s\n", path.c_str());
        }
    }
    return 0;
}
