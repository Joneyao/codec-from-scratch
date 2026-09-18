// motion_search.cpp — 全搜索 / 菱形搜索的实现。见 motion_search.h 的原理说明。
#include "motion_search.h"

#include <cstdlib>  // std::abs
#include <vector>

namespace cfs {

int Sad16x16(const GrayFrame& ref, int ref_x, int ref_y, const int* cur,
             int cur_stride) {
    int sad = 0;
    for (int y = 0; y < 16; ++y) {
        const int* row = cur + static_cast<size_t>(y) * cur_stride;
        for (int x = 0; x < 16; ++x) {
            int d = ref.at(ref_x + x, ref_y + y) - row[x];
            sad += (d < 0) ? -d : d;
        }
    }
    return sad;
}

namespace {

// |MV| 的 L1 度量，用于平手时偏向更小的运动矢量（更靠近零 MV，编码更省）。
inline int MvCost(int mvx, int mvy) {
    return std::abs(mvx) + std::abs(mvy);
}

}  // namespace

SearchResult FullSearch(const GrayFrame& ref, const int* cur, int cur_stride,
                        int block_x, int block_y, int range) {
    SearchResult best;
    best.sad = -1;  // 尚未初始化标记
    best.points_checked = 0;

    for (int dy = -range; dy <= range; ++dy) {
        for (int dx = -range; dx <= range; ++dx) {
            int sad = Sad16x16(ref, block_x + dx, block_y + dy, cur, cur_stride);
            ++best.points_checked;
            bool take = false;
            if (best.sad < 0) {
                take = true;  // 第一个点
            } else if (sad < best.sad) {
                take = true;
            } else if (sad == best.sad &&
                       MvCost(dx, dy) < MvCost(best.mv.mvx, best.mv.mvy)) {
                take = true;  // 平手取更小的 MV
            }
            if (take) {
                best.sad = sad;
                best.mv.mvx = dx;
                best.mv.mvy = dy;
            }
        }
    }
    return best;
}

namespace {

// 已检查过的候选点缓存：记录某个偏移 (dx,dy) 的 SAD，避免菱形迭代里重复计算。
// 用一张覆盖 [-range,range]^2 的稠密表 + 命中标记，查询/写入都是 O(1)。
class SadCache {
public:
    SadCache(const GrayFrame& ref, const int* cur, int cur_stride, int block_x,
             int block_y, int range)
        : ref_(ref),
          cur_(cur),
          cur_stride_(cur_stride),
          block_x_(block_x),
          block_y_(block_y),
          range_(range),
          side_(2 * range + 1),
          computed_(static_cast<size_t>(side_) * side_, false),
          value_(static_cast<size_t>(side_) * side_, 0) {}

    // 候选点是否在 [-range,range] 方形窗口内（越界的不检查，和全搜索同范围）。
    bool InRange(int dx, int dy) const {
        return dx >= -range_ && dx <= range_ && dy >= -range_ && dy <= range_;
    }

    // 取 (dx,dy) 的 SAD；首次计算时记一次 points_checked。要求调用方已确保 InRange。
    int Get(int dx, int dy, int* points_checked) {
        size_t idx = Index(dx, dy);
        if (!computed_[idx]) {
            value_[idx] = Sad16x16(ref_, block_x_ + dx, block_y_ + dy, cur_,
                                   cur_stride_);
            computed_[idx] = true;
            ++(*points_checked);
        }
        return value_[idx];
    }

private:
    size_t Index(int dx, int dy) const {
        return static_cast<size_t>(dy + range_) * side_ + (dx + range_);
    }

    const GrayFrame& ref_;
    const int* cur_;
    int cur_stride_;
    int block_x_;
    int block_y_;
    int range_;
    int side_;
    std::vector<char> computed_;
    std::vector<int> value_;
};

}  // namespace

SearchResult DiamondSearch(const GrayFrame& ref, const int* cur, int cur_stride,
                           int block_x, int block_y, int range) {
    SearchResult result;
    result.points_checked = 0;

    // range=0 时窗口只有一个点，直接返回零 MV。
    if (range <= 0) {
        result.mv = {0, 0};
        result.sad = Sad16x16(ref, block_x, block_y, cur, cur_stride);
        result.points_checked = 1;
        return result;
    }

    SadCache cache(ref, cur, cur_stride, block_x, block_y, range);

    // 大菱形 LDSP：中心 + 8 个外圈点（上下左右各 2，四个斜向各 1）。
    static const int kLdsp[9][2] = {
        {0, 0},  {0, -2}, {0, 2},  {-2, 0}, {2, 0},
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1},
    };
    // 小菱形 SDSP：中心的上下左右 4 个邻点。
    static const int kSdsp[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};

    int cx = 0, cy = 0;  // 当前搜索中心（相对同位的偏移，即候选 MV）
    // 中心必在窗口内，先算出它的 SAD 作为起点。
    int center_sad = cache.Get(cx, cy, &result.points_checked);

    // 阶段一：大菱形一路下滑，直到最优点回到中心。
    while (true) {
        int best_sad = center_sad;
        int best_dx = cx, best_dy = cy;
        bool moved = false;
        for (const auto& p : kLdsp) {
            int nx = cx + p[0];
            int ny = cy + p[1];
            if (nx == cx && ny == cy) continue;   // 跳过中心自身
            if (!cache.InRange(nx, ny)) continue;  // 超出窗口不检查
            int sad = cache.Get(nx, ny, &result.points_checked);
            if (sad < best_sad ||
                (sad == best_sad &&
                 MvCost(nx, ny) < MvCost(best_dx, best_dy))) {
                best_sad = sad;
                best_dx = nx;
                best_dy = ny;
                moved = true;
            }
        }
        if (!moved) break;  // 最优仍是中心，LDSP 收敛
        cx = best_dx;
        cy = best_dy;
        center_sad = best_sad;
    }

    // 阶段二：小菱形定最终点。
    {
        int best_sad = center_sad;
        int best_dx = cx, best_dy = cy;
        for (const auto& p : kSdsp) {
            int nx = cx + p[0];
            int ny = cy + p[1];
            if (!cache.InRange(nx, ny)) continue;
            int sad = cache.Get(nx, ny, &result.points_checked);
            if (sad < best_sad ||
                (sad == best_sad &&
                 MvCost(nx, ny) < MvCost(best_dx, best_dy))) {
                best_sad = sad;
                best_dx = nx;
                best_dy = ny;
            }
        }
        cx = best_dx;
        cy = best_dy;
        center_sad = best_sad;
    }

    result.mv.mvx = cx;
    result.mv.mvy = cy;
    result.sad = center_sad;
    return result;
}

}  // namespace cfs
