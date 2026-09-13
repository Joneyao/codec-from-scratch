// motion_compensate.h — H.264 帧间预测(Inter Prediction)/运动补偿
//                       (Motion Compensation)：从参考帧"搬"一块像素过来当预测。
//
// 上一篇把 I 帧（完全靠自己、不依赖别帧的关键帧）解完了：帧内预测猜出内容、
// CAVLC 解出残差、反变换重建。可视频里 I 帧是少数，大多数是 P 帧(前向预测帧,
// Predicted frame)——它不从零画起，而是指着上一帧说"我这块长得跟你那块几乎
// 一样"，把那块像素搬过来，再补一点残差。这就是运动补偿。
//
// 帧间预测吃的是"时间冗余"：相邻两帧往往高度相似，物体只是挪了一点位置。
// 与其把当前块的像素重新编一遍，不如只记录"这块从参考帧的哪个位置搬来"——
// 这个方向和距离，就是运动矢量(MV, Motion Vector)。
//
// 本文件实现三件事（严格照 ITU-T H.264 8.4）：
//   1) 运动矢量中值预测(8.4.1.3.1)：MV 本身也差分编码。当前块的 MV 预测值
//      取左邻 A、上邻 B、右上邻 C 三个已解码 MV 的分量中值(Median)；真正的
//      MV = 预测 MV + 码流里的 mvd(运动矢量差, Motion Vector Difference)。
//      因为相邻块运动往往一致，预测得准，mvd 就很小、很省码字。
//   2) 亮度亚像素插值(8.4.2.2.1)：MV 精度到 1/4 像素。半像素位置用 6-tap
//      (6 抽头滤波器)插值，抽头 (1, -5, 20, 20, -5, 1)；1/4 像素位置由相邻的
//      整像素与半像素做平均(向上取整)得到。
//   3) 色度亚像素插值(8.4.2.2.2)：色度 MV 精度到 1/8 像素，用双线性插值。
//
// MV 的分量单位是 1/4 像素：mv=(4,0) 表示向右整整 1 个像素，mv=(2,0) 表示
// 向右半个像素，mv=(1,0) 表示向右 1/4 像素。
#ifndef CODEC_FROM_SCRATCH_H264_MOTION_COMPENSATE_H
#define CODEC_FROM_SCRATCH_H264_MOTION_COMPENSATE_H

#include <cstdint>
#include <vector>

namespace cfs {

// 一个运动矢量，单位是 1/4 像素（亮度）。x 向右为正，y 向下为正。
struct MotionVector {
    int x = 0;  // 水平分量，1/4 像素单位
    int y = 0;  // 垂直分量，1/4 像素单位
};

// 参考帧的一个平面（亮度或单个色度分量）。按行优先存整数像素样本，
// 取样越界时按 spec 的 Clip3 规则钳到边界（边缘像素复制）。
struct RefPlane {
    int width = 0;
    int height = 0;
    std::vector<int> samples;  // size = width*height，像素值 0..255

    int at(int x, int y) const {
        if (x < 0) x = 0;
        else if (x >= width) x = width - 1;
        if (y < 0) y = 0;
        else if (y >= height) y = height - 1;
        return samples[static_cast<size_t>(y) * width + x];
    }
};

// 一小块预测像素：block[y*w + x]，尺寸 w*h。
struct PredBlock {
    int w = 0, h = 0;
    std::vector<int> pix;
    int at(int x, int y) const { return pix[static_cast<size_t>(y) * w + x]; }
};

// 三分量中值（spec Median()）：把三个数排序取中间那个。
int Median3(int a, int b, int c);

// 运动矢量中值预测(spec 8.4.1.3.1，式 8-165/8-166)：分量各自取中值。
// mv_a=左邻, mv_b=上邻, mv_c=右上邻。返回预测 MV(mvpLX)。
MotionVector PredictMvMedian(const MotionVector& mv_a, const MotionVector& mv_b,
                             const MotionVector& mv_c);

// 由预测 MV 和码流里的 mvd 还原真正的 MV：mv = mvp + mvd。
MotionVector ReconstructMv(const MotionVector& mvp, const MotionVector& mvd);

// 6-tap 半像素滤波(spec 8.4.2.2.1，式 8-185/8-187)：给 6 个整像素样本，
// 算 b1 = p0 - 5*p1 + 20*p2 + 20*p3 - 5*p4 + p5，再 Clip1((b1+16)>>5)。
int SixTapHalf(int p0, int p1, int p2, int p3, int p4, int p5);

// 亮度亚像素取样(spec 8.4.2.2.1)：从参考平面按 1/4 像素精度取一个样本。
// (int_x,int_y) 是整像素位置，(frac_x,frac_y) 是 0..3 的 1/4 像素相位。
// frac=0 整像素直接取；frac=2 半像素走 6-tap；frac=1/3 走整/半平均。
int SampleLumaQpel(const RefPlane& ref, int int_x, int int_y, int frac_x,
                   int frac_y);

// 亮度运动补偿：给参考平面、块左上角整像素位置(bx,by)、块尺寸(w,h)、
// MV(1/4 像素单位)，取出 w*h 的预测块。整像素 MV 直接搬块；分数像素插值。
PredBlock MotionCompensateLuma(const RefPlane& ref, int bx, int by, int w,
                               int h, const MotionVector& mv);

// 色度亚像素取样(spec 8.4.2.2.2，式 8-214)：1/8 像素双线性插值。
// frac_x/frac_y 为 0..7 的 1/8 像素相位。
int SampleChromaEpel(const RefPlane& ref, int int_x, int int_y, int frac_x,
                     int frac_y);

// SAD(绝对差之和)：预测块与真实块逐像素 |pred-orig| 累加。越小预测越准。
int SadBlock(const PredBlock& pred, const PredBlock& orig);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_H264_MOTION_COMPENSATE_H
