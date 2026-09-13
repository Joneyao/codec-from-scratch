// intra_predict.h — H.264 帧内预测(Intra Prediction)：用已重建的邻居像素
//                   猜出当前块的内容。这是 IDR 帧里每个宏块的第一步。
//
// 上一篇把一帧切成了 16x16 的宏块网格，也读出了第一个宏块 mb_type=0
// (I_4x4)。I_4x4 宏块里，16x16 会再切成 16 个 4x4 子块，每个子块各自挑一种
// 帧内预测模式，用它上边和左边"已经解码好"的邻居像素，猜出这 4x4 = 16 个
// 像素的值。猜出来的叫"预测块"，它和真实块的差叫残差(residual)，残差走后续
// 的变换量化——那是下一篇的事。
//
// 帧内预测吃的是"空间冗余"：同一帧里相邻像素往往长得很像。所以拿旁边已知的
// 像素去猜，往往能猜个八九不离十，剩下的残差就很小、很好压。
//
// 本文件实现两套模式（严格照 ITU-T H.264 8.3）：
//   - Intra_4x4：9 种模式(8.3.1.2)。编号与名字见 Intra4x4Mode。
//   - Intra_16x16：4 种模式(8.3.2)。编号与名字见 Intra16x16Mode。
//
// 邻居像素的记法沿用规范 p[x, y]：当前块左上角为原点，上邻在 y=-1 那一行、
// 左邻在 x=-1 那一列、左上角单像素是 p[-1,-1]。可用性(available)这里做简化
// 处理：由调用方通过布尔位声明哪些邻居可用，本模块按规范的回退规则取值。
#ifndef CODEC_FROM_SCRATCH_H264_INTRA_PREDICT_H
#define CODEC_FROM_SCRATCH_H264_INTRA_PREDICT_H

#include <array>
#include <cstdint>

namespace cfs {

// Intra_4x4 的 9 种模式（表 8-2 / 8.3.1.2）。
enum class Intra4x4Mode : int {
    kVertical = 0,          // 0：垂直，复制上邻(8.3.1.2.1)
    kHorizontal = 1,        // 1：水平，复制左邻(8.3.1.2.2)
    kDC = 2,                // 2：DC(直流)，取上+左邻均值——"抹平"(8.3.1.2.3)
    kDiagonalDownLeft = 3,  // 3：右上→左下 对角(8.3.1.2.4)
    kDiagonalDownRight = 4, // 4：左上→右下 对角(8.3.1.2.5)
    kVerticalRight = 5,     // 5：偏右的近垂直(8.3.1.2.6)
    kHorizontalDown = 6,    // 6：偏下的近水平(8.3.1.2.7)
    kVerticalLeft = 7,      // 7：偏左的近垂直(8.3.1.2.8)
    kHorizontalUp = 8,      // 8：偏上的近水平(8.3.1.2.9)
};

const char* Intra4x4ModeName(Intra4x4Mode m);
const char* Intra4x4ModeShort(Intra4x4Mode m);  // 简短英文，配图轴标签用

// Intra_16x16 的 4 种模式（表 8-3 / 8.3.2）。
enum class Intra16x16Mode : int {
    kVertical = 0,    // 0：垂直(8.3.2.1)
    kHorizontal = 1,  // 1：水平(8.3.2.2)
    kDC = 2,          // 2：DC 均值(8.3.2.3)
    kPlane = 3,       // 3：平面渐变(8.3.2.4)
};

const char* Intra16x16ModeName(Intra16x16Mode m);

// 4x4 块的邻居像素集合。字段名对应规范 p[x, y]：
//   top[i]      = p[i, -1]，i = 0..7（上邻 4 个 + 右上延伸 4 个）
//   left[i]     = p[-1, i]，i = 0..3（左邻 4 个）
//   top_left    = p[-1, -1]（左上角单像素）
// 可用性标志缺省全可用；不可用时按 8.3.1.2 的回退规则处理。
struct Neighbors4x4 {
    std::array<int, 8> top{};   // p[0..7, -1]
    std::array<int, 4> left{};  // p[-1, 0..3]
    int top_left = 0;           // p[-1, -1]
    bool top_available = true;
    bool left_available = true;
    bool top_left_available = true;
    bool top_right_available = true;  // p[4..7, -1] 是否可用
};

// 16x16 宏块的邻居像素集合。
//   top[i]   = p[i, -1]，i = 0..15
//   left[i]  = p[-1, i]，i = 0..15
//   top_left = p[-1, -1]
struct Neighbors16x16 {
    std::array<int, 16> top{};
    std::array<int, 16> left{};
    int top_left = 0;
    bool top_available = true;
    bool left_available = true;
    bool top_left_available = true;
};

// 一个 4x4 预测块：block[y][x]，x,y = 0..3。像素值范围 0..255。
using Block4x4 = std::array<std::array<int, 4>, 4>;
// 一个 16x16 预测块：block[y][x]，x,y = 0..15。
using Block16x16 = std::array<std::array<int, 16>, 16>;

// 对给定邻居，按指定模式算出 4x4 预测块（严格照 8.3.1.2.1-9）。
Block4x4 PredictIntra4x4(const Neighbors4x4& nb, Intra4x4Mode mode);

// 对给定邻居，按指定模式算出 16x16 预测块（严格照 8.3.2.1-4）。
Block16x16 PredictIntra16x16(const Neighbors16x16& nb, Intra16x16Mode mode);

// SAD(绝对差之和, Sum of Absolute Differences)：预测块与真实块逐像素求
// |pred - orig| 再累加。越小说明预测越准，残差越小。
int Sad4x4(const Block4x4& pred, const Block4x4& orig);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_H264_INTRA_PREDICT_H
