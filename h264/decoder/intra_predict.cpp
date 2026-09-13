// intra_predict.cpp — 帧内预测的实现，严格照 ITU-T H.264 8.3。
//
// 记法与规范一致：p[x, y] 表示邻居像素，当前块左上角为原点。
//   p[x, -1]  上邻那一行（含右上延伸），x = 0..7（4x4）或 0..15（16x16）
//   p[-1, y]  左邻那一列，y = 0..3（4x4）或 0..15（16x16）
//   p[-1, -1] 左上角单像素
// 下面用小工具 P(...) 把 (x,y) 映射到 Neighbors 结构体的字段上，让公式和规范
// 一一对得上。像素做整数运算，最后 Clip 到 0..255。
#include "intra_predict.h"

#include <algorithm>
#include <cstdlib>

namespace cfs {

namespace {

// 把像素钳到 8bit 合法范围（规范里的 Clip1，BitDepth=8）。
inline int Clip1(int v) { return std::max(0, std::min(255, v)); }

// 取 4x4 邻居 p[x, y]。仅在公式定义域内的坐标会被访问。
inline int P4(const Neighbors4x4& nb, int x, int y) {
    if (y == -1 && x == -1) return nb.top_left;  // p[-1,-1]
    if (y == -1) return nb.top[x];               // p[0..7, -1]
    return nb.left[y];                           // p[-1, 0..3]
}

// 取 16x16 邻居 p[x, y]。
inline int P16(const Neighbors16x16& nb, int x, int y) {
    if (y == -1 && x == -1) return nb.top_left;
    if (y == -1) return nb.top[x];
    return nb.left[y];
}

}  // namespace

const char* Intra4x4ModeName(Intra4x4Mode m) {
    switch (m) {
        case Intra4x4Mode::kVertical:          return "Vertical(垂直)";
        case Intra4x4Mode::kHorizontal:        return "Horizontal(水平)";
        case Intra4x4Mode::kDC:                return "DC(直流/抹平)";
        case Intra4x4Mode::kDiagonalDownLeft:  return "Diagonal_Down_Left(左下对角)";
        case Intra4x4Mode::kDiagonalDownRight: return "Diagonal_Down_Right(右下对角)";
        case Intra4x4Mode::kVerticalRight:     return "Vertical_Right(右偏垂直)";
        case Intra4x4Mode::kHorizontalDown:    return "Horizontal_Down(下偏水平)";
        case Intra4x4Mode::kVerticalLeft:      return "Vertical_Left(左偏垂直)";
        case Intra4x4Mode::kHorizontalUp:      return "Horizontal_Up(上偏水平)";
    }
    return "?";
}

const char* Intra4x4ModeShort(Intra4x4Mode m) {
    switch (m) {
        case Intra4x4Mode::kVertical:          return "V";
        case Intra4x4Mode::kHorizontal:        return "H";
        case Intra4x4Mode::kDC:                return "DC";
        case Intra4x4Mode::kDiagonalDownLeft:  return "DDL";
        case Intra4x4Mode::kDiagonalDownRight: return "DDR";
        case Intra4x4Mode::kVerticalRight:     return "VR";
        case Intra4x4Mode::kHorizontalDown:    return "HD";
        case Intra4x4Mode::kVerticalLeft:      return "VL";
        case Intra4x4Mode::kHorizontalUp:      return "HU";
    }
    return "?";
}

const char* Intra16x16ModeName(Intra16x16Mode m) {
    switch (m) {
        case Intra16x16Mode::kVertical:   return "Vertical(垂直)";
        case Intra16x16Mode::kHorizontal: return "Horizontal(水平)";
        case Intra16x16Mode::kDC:         return "DC(直流/抹平)";
        case Intra16x16Mode::kPlane:      return "Plane(平面渐变)";
    }
    return "?";
}

// ============================ Intra_4x4（9 种模式）============================
// 每种模式严格照 8.3.1.2.1 到 8.3.1.2.9。坐标 (x,y) 均 0..3，用 P4() 访问邻居。
Block4x4 PredictIntra4x4(const Neighbors4x4& nb, Intra4x4Mode mode) {
    Block4x4 pred{};
    auto p = [&](int x, int y) { return P4(nb, x, y); };

    switch (mode) {
        // 8.3.1.2.1 Vertical：整列复制正上方的上邻像素 p[x,-1]。竖条纹。
        case Intra4x4Mode::kVertical:
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                    pred[y][x] = p(x, -1);
            break;

        // 8.3.1.2.2 Horizontal：整行复制左边的左邻像素 p[-1,y]。横条纹。
        case Intra4x4Mode::kHorizontal:
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                    pred[y][x] = p(-1, y);
            break;

        // 8.3.1.2.3 DC：4 个上邻 + 4 个左邻求平均，全块填同一个值。抹平。
        // 邻居不全时按 (8-48)(8-49)(8-50) 回退：只用可用的一侧，都没有用 128。
        case Intra4x4Mode::kDC: {
            int val;
            if (nb.top_available && nb.left_available) {
                int s = 0;
                for (int i = 0; i < 4; ++i) s += p(i, -1) + p(-1, i);
                val = (s + 4) >> 3;                        // (8-47)
            } else if (nb.left_available) {
                int s = 0;
                for (int i = 0; i < 4; ++i) s += p(-1, i);
                val = (s + 2) >> 2;                        // (8-48)
            } else if (nb.top_available) {
                int s = 0;
                for (int i = 0; i < 4; ++i) s += p(i, -1);
                val = (s + 2) >> 2;                        // (8-49)
            } else {
                val = 128;                                 // (8-50)
            }
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                    pred[y][x] = val;
            break;
        }

        // 8.3.1.2.4 Diagonal_Down_Left：右上方向往左下拉，用上邻 p[0..7,-1]。
        case Intra4x4Mode::kDiagonalDownLeft:
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    if (x == 3 && y == 3)
                        pred[y][x] = (p(6, -1) + 3 * p(7, -1) + 2) >> 2;   // (8-51)
                    else
                        pred[y][x] = (p(x + y, -1) + 2 * p(x + y + 1, -1) +
                                      p(x + y + 2, -1) + 2) >> 2;          // (8-52)
                }
            break;

        // 8.3.1.2.5 Diagonal_Down_Right：左上方向往右下拉，用上邻+左邻+左上角。
        case Intra4x4Mode::kDiagonalDownRight:
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    if (x > y)
                        pred[y][x] = (p(x - y - 2, -1) + 2 * p(x - y - 1, -1) +
                                      p(x - y, -1) + 2) >> 2;              // (8-53)
                    else if (x < y)
                        pred[y][x] = (p(-1, y - x - 2) + 2 * p(-1, y - x - 1) +
                                      p(-1, y - x) + 2) >> 2;              // (8-54)
                    else
                        pred[y][x] = (p(0, -1) + 2 * p(-1, -1) +
                                      p(-1, 0) + 2) >> 2;                  // (8-55)
                }
            break;

        // 8.3.1.2.6 Vertical_Right：接近垂直、略向右倾。zVR 决定用哪条公式。
        case Intra4x4Mode::kVerticalRight:
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    int zvr = 2 * x - y;
                    if (zvr == 0 || zvr == 2 || zvr == 4 || zvr == 6)
                        pred[y][x] = (p(x - (y >> 1) - 1, -1) +
                                      p(x - (y >> 1), -1) + 1) >> 1;       // (8-56)
                    else if (zvr == 1 || zvr == 3 || zvr == 5)
                        pred[y][x] = (p(x - (y >> 1) - 2, -1) +
                                      2 * p(x - (y >> 1) - 1, -1) +
                                      p(x - (y >> 1), -1) + 2) >> 2;       // (8-57)
                    else if (zvr == -1)
                        pred[y][x] = (p(-1, 0) + 2 * p(-1, -1) +
                                      p(0, -1) + 2) >> 2;                  // (8-58)
                    else  // zvr == -2 或 -3
                        pred[y][x] = (p(-1, y - 1) + 2 * p(-1, y - 2) +
                                      p(-1, y - 3) + 2) >> 2;              // (8-59)
                }
            break;

        // 8.3.1.2.7 Horizontal_Down：接近水平、略向下倾。zHD 决定用哪条公式。
        case Intra4x4Mode::kHorizontalDown:
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    int zhd = 2 * y - x;
                    if (zhd == 0 || zhd == 2 || zhd == 4 || zhd == 6)
                        pred[y][x] = (p(-1, y - (x >> 1) - 1) +
                                      p(-1, y - (x >> 1)) + 1) >> 1;       // (8-60)
                    else if (zhd == 1 || zhd == 3 || zhd == 5)
                        pred[y][x] = (p(-1, y - (x >> 1) - 2) +
                                      2 * p(-1, y - (x >> 1) - 1) +
                                      p(-1, y - (x >> 1)) + 2) >> 2;       // (8-61)
                    else if (zhd == -1)
                        pred[y][x] = (p(-1, 0) + 2 * p(-1, -1) +
                                      p(0, -1) + 2) >> 2;                  // (8-62)
                    else  // zhd == -2 或 -3
                        pred[y][x] = (p(x - 1, -1) + 2 * p(x - 2, -1) +
                                      p(x - 3, -1) + 2) >> 2;              // (8-63)
                }
            break;

        // 8.3.1.2.8 Vertical_Left：接近垂直、略向左倾，用上邻 p[0..7,-1]。
        case Intra4x4Mode::kVerticalLeft:
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    if (y == 0 || y == 2)
                        pred[y][x] = (p(x + (y >> 1), -1) +
                                      p(x + (y >> 1) + 1, -1) + 1) >> 1;   // (8-64)
                    else  // y == 1 或 3
                        pred[y][x] = (p(x + (y >> 1), -1) +
                                      2 * p(x + (y >> 1) + 1, -1) +
                                      p(x + (y >> 1) + 2, -1) + 2) >> 2;   // (8-65)
                }
            break;

        // 8.3.1.2.9 Horizontal_Up：接近水平、略向上倾，用左邻 p[-1,0..3]。
        case Intra4x4Mode::kHorizontalUp:
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    int zhu = x + 2 * y;
                    if (zhu == 0 || zhu == 2 || zhu == 4)
                        pred[y][x] = (p(-1, y + (x >> 1)) +
                                      p(-1, y + (x >> 1) + 1) + 1) >> 1;   // (8-66)
                    else if (zhu == 1 || zhu == 3)
                        pred[y][x] = (p(-1, y + (x >> 1)) +
                                      2 * p(-1, y + (x >> 1) + 1) +
                                      p(-1, y + (x >> 1) + 2) + 2) >> 2;   // (8-67)
                    else if (zhu == 5)
                        pred[y][x] = (p(-1, 2) + 3 * p(-1, 3) + 2) >> 2;   // (8-68)
                    else  // zhu > 5
                        pred[y][x] = p(-1, 3);                            // (8-69)
                }
            break;
    }

    // 上述公式产生的都是邻居像素的加权平均，天然落在 0..255，Clip 只做保险。
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            pred[y][x] = Clip1(pred[y][x]);
    return pred;
}

// ============================ Intra_16x16（4 种模式）==========================
// 严格照 8.3.2.1 到 8.3.2.4。
Block16x16 PredictIntra16x16(const Neighbors16x16& nb, Intra16x16Mode mode) {
    Block16x16 pred{};
    auto p = [&](int x, int y) { return P16(nb, x, y); };

    switch (mode) {
        // 8.3.2.1 Vertical：整列复制上邻 p[x,-1]。
        case Intra16x16Mode::kVertical:
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    pred[y][x] = p(x, -1);
            break;

        // 8.3.2.2 Horizontal：整行复制左邻 p[-1,y]。
        case Intra16x16Mode::kHorizontal:
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    pred[y][x] = p(-1, y);
            break;

        // 8.3.2.3 DC：16 上邻 + 16 左邻求平均，全块一个值。回退同 4x4 思路。
        case Intra16x16Mode::kDC: {
            int val;
            if (nb.top_available && nb.left_available) {
                int s = 0;
                for (int i = 0; i < 16; ++i) s += p(i, -1) + p(-1, i);
                val = (s + 16) >> 5;                       // (8-72)
            } else if (nb.left_available) {
                int s = 0;
                for (int i = 0; i < 16; ++i) s += p(-1, i);
                val = (s + 8) >> 4;                        // (8-73)
            } else if (nb.top_available) {
                int s = 0;
                for (int i = 0; i < 16; ++i) s += p(i, -1);
                val = (s + 8) >> 4;                        // (8-74)
            } else {
                val = 128;                                 // (8-75)
            }
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    pred[y][x] = val;
            break;
        }

        // 8.3.2.4 Plane：拿上邻、左邻的"斜率"拟合出一个平面，做线性渐变。
        // a 是中心亮度，b 是水平方向斜率，c 是垂直方向斜率。(8-76)~(8-81)
        case Intra16x16Mode::kPlane: {
            int H = 0, V = 0;
            for (int xp = 0; xp <= 7; ++xp)
                H += (xp + 1) * (p(8 + xp, -1) - p(6 - xp, -1));   // (8-80)
            for (int yp = 0; yp <= 7; ++yp)
                V += (yp + 1) * (p(-1, 8 + yp) - p(-1, 6 - yp));   // (8-81)
            int a = 16 * (p(-1, 15) + p(15, -1));                  // (8-77)
            int b = (5 * H + 32) >> 6;                             // (8-78)
            int c = (5 * V + 32) >> 6;                             // (8-79)
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    pred[y][x] = Clip1((a + b * (x - 7) + c * (y - 7) + 16) >> 5);  // (8-76)
            break;
        }
    }

    if (mode != Intra16x16Mode::kPlane) {  // Plane 已在公式内 Clip
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                pred[y][x] = Clip1(pred[y][x]);
    }
    return pred;
}

int Sad4x4(const Block4x4& pred, const Block4x4& orig) {
    int sad = 0;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            sad += std::abs(pred[y][x] - orig[y][x]);
    return sad;
}

}  // namespace cfs
