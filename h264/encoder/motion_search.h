// motion_search.h — H.264 编码器的「运动估计」(Motion Estimation)：给当前帧
//                    的一个 16x16 块，去参考帧里找一个最像它的位置。
//
// 这是帧间编码里最贵的一步——一部电影里 P 帧占绝大多数，每个 P 帧又有成百上千
// 个宏块，每个宏块都要在参考帧里「搜」一遍。工程上运动估计常吃掉编码器 80~90%
// 的时间。所以「怎么搜」直接决定编码器快慢。
//
// ── 解码器只「搬」，编码器要「找」──
//   decoder/motion_compensate.{h,cpp} 做的是运动补偿：码流里已经写死了 MV，
//   解码器照着 MV 把参考帧对应块搬过来即可，它「不做选择」。编码侧相反：MV 是
//   编码器自己搜出来的。搜得准，残差小、码率低；搜得快，编码器才跑得动。这一
//   对「质量 vs 速度」的矛盾，正是本模块要用真实数据摊开来看的东西。
//
// ── 两把尺子：全搜索 vs 菱形搜索 ──
//   · FullSearch(全搜索)：在 [-range,+range] 的方形窗口里逐点算 SAD，取最小。
//     它一定能找到窗口内的全局最优点——质量上限，但点数是 (2*range+1)^2，极慢。
//   · DiamondSearch(菱形搜索，经典 DS 算法)：假设 SAD 代价面大体是「单峰碗形」，
//     用一个大菱形(LDSP)朝代价下降方向一路滑，滑到中心最优后换小菱形(SDSP)定
//     最终点。点数通常只有全搜索的十分之一量级，代价略高或持平——拿质量换速度。
//
//   H.264 规范只规定「给定 MV 该怎么解」，至于编码器该怎么搜 MV，spec 一个字
//   都不写。全搜索、菱形搜索、六边形搜索、UMH…全凭编码器自选。这正是不同 H.264
//   编码器压缩效率/速度拉开差距的核心地带之一。
//
// ── 关于精度 ──
//   本模块只做整像素搜索：MV 分量单位就是「整数像素」，mvx=3 表示向右 3 个像素。
//   （解码器那侧的 MV 是 1/4 像素单位、还带 6-tap 亚像素插值——那是运动补偿的
//    活儿；编码侧的整像素搜索是亚像素细化之前的第一步，本篇只讲到这里。）
#ifndef CODEC_FROM_SCRATCH_H264_ENCODER_MOTION_SEARCH_H
#define CODEC_FROM_SCRATCH_H264_ENCODER_MOTION_SEARCH_H

#include <vector>

namespace cfs {

// 一个运动矢量，单位是「整数像素」。mvx 向右为正，mvy 向下为正。
// 注意：这里刻意用整像素单位、字段名 mvx/mvy，和 decoder 侧 1/4 像素精度的
// MotionVector{x,y} 区分开——编码侧整像素搜索是独立的第一步。
struct MotionVector {
    int mvx = 0;
    int mvy = 0;
};

// 一次搜索的结果：找到的 MV、该 MV 处的 SAD 代价、以及这次搜索实际算了多少个
// 候选点（points_checked）。points_checked 是本篇配图的关键数据——它把「全
// 搜索 vs 菱形搜索」的算力差距直接量化出来。
struct SearchResult {
    MotionVector mv;
    int sad = 0;
    int points_checked = 0;
};

// 一帧的灰度平面（亮度 Y）。按行优先存整数像素，取样越界时钳到边界（边缘
// 像素复制），这样块滑到图像边缘也不会越界读。
struct GrayFrame {
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

// 代价函数：当前 16x16 块（cur，行优先，16*16 个像素）与参考帧中以 (ref_x,
// ref_y) 为左上角的 16x16 块，逐像素求 |差| 累加。越小越像。ref 越界按边界
// 钳。cur 用普通指针 + stride 描述，方便直接指向某帧的内部。
int Sad16x16(const GrayFrame& ref, int ref_x, int ref_y,
             const int* cur, int cur_stride);

// 全搜索：以当前块在参考帧里的「同位」(block_x, block_y) 为搜索中心，在
// [-range,+range] x [-range,+range] 的方形窗口里逐点算 SAD，取最小那个点。
//   ref          参考帧灰度平面
//   cur          当前 16x16 块（行优先，指向 16*16 个像素，或某帧内部）
//   cur_stride   cur 的行跨度（若 cur 是独立的 16x16 数组则传 16）
//   block_x/y    当前块在图像里的位置（也是搜索窗口的中心/零 MV 位置）
//   range        搜索半径（像素）
// 返回：最优 MV（相对同位的偏移）、其 SAD、检查的点数 (2*range+1)^2。
// 平手取 |MV| 更小者（更靠近零 MV，编码更省），再平手取先遇到的。
SearchResult FullSearch(const GrayFrame& ref, const int* cur, int cur_stride,
                        int block_x, int block_y, int range);

// 菱形搜索（经典 Diamond Search / DS 算法）：
//   1) 大菱形模式 LDSP（Large Diamond Search Pattern，9 点：中心 + 8 个外圈）
//      反复迭代——每轮算 LDSP 上各点 SAD，若最优点仍是中心则收敛，否则把中心
//      移到最优点继续。为省算力，移动后只算「新暴露」的点（已算过的点缓存跳过）。
//   2) 收敛后切小菱形模式 SDSP（Small Diamond Search Pattern，4 个上下左右邻点），
//      算一轮取最优，即为最终整像素 MV。
//   参数含义同 FullSearch。range 用来把搜索限制在同一窗口内（超出 range 的候选
//   点不检查），保证和全搜索在同一「可达范围」下比较才公平。
// 返回：找到的 MV、其 SAD、以及实际检查过的候选点总数（去重后）。
SearchResult DiamondSearch(const GrayFrame& ref, const int* cur, int cur_stride,
                           int block_x, int block_y, int range);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_H264_ENCODER_MOTION_SEARCH_H
