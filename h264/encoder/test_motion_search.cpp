// test_motion_search.cpp — 运动搜索的最小校验（可手算/可断言的构造输入）。
//
// 核心思路：造一个有纹理的参考帧，把某个 16x16 块整体平移一个「已知」的偏移
// 当作当前块。那么正确的 MV 就是这个已知偏移的反向量，且在该 MV 处 SAD 应该
// 恰好为 0（完美命中）。逐条核对：
//   1) FullSearch 能找回已知 MV，SAD==0。
//   2) DiamondSearch 在同一情形也能找到（单峰代价面，DS 会收敛到同一点）。
//   3) DiamondSearch 的 points_checked 明显小于 FullSearch。
//   4) 平手/零运动：当前块=参考帧同位块时，两种搜索都返回 MV=(0,0)、SAD=0。
// Release 构建默认带 -DNDEBUG 会抹掉 assert，这里强制启用。
#undef NDEBUG

#include <cassert>
#include <cstdio>
#include <vector>

#include "motion_search.h"

using cfs::DiamondSearch;
using cfs::FullSearch;
using cfs::GrayFrame;
using cfs::SearchResult;

namespace {

// 造一个有纹理的灰度帧：像素值随 (x,y) 平滑变化并叠加格纹，保证代价面不平坦、
// 大体单峰，菱形搜索能沿梯度滑到真解。
GrayFrame MakeTexturedFrame(int w, int h) {
    GrayFrame f;
    f.width = w;
    f.height = h;
    f.samples.resize(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int v = (x * 3 + y * 5) & 0xFF;          // 平滑斜坡
            v = (v + ((x ^ y) & 0x1F) * 2) & 0xFF;   // 叠一点高频格纹
            f.samples[static_cast<size_t>(y) * w + x] = v;
        }
    }
    return f;
}

// 从参考帧里以 (bx,by) 为左上角抠出一个 16x16 块，拷成独立数组（stride=16）。
// 这样得到的「当前块」就是参考帧同位偏移 (bx-orig_x, by-orig_y) 的平移版本。
std::vector<int> Grab16x16(const GrayFrame& f, int bx, int by) {
    std::vector<int> blk(16 * 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            blk[y * 16 + x] = f.at(bx + x, by + y);
    return blk;
}

// 测试 1+2+3：已知平移 -> 两种搜索都找回 MV，DS 点数远少于全搜索。
void TestKnownShift() {
    GrayFrame ref = MakeTexturedFrame(160, 128);
    const int range = 8;
    // 当前块「本应」落在同位 (block_x, block_y)，但内容取自参考帧偏移
    // (shift_x, shift_y) 处。于是最佳 MV = (shift_x, shift_y)，该处 SAD=0。
    const int block_x = 64, block_y = 48;
    const int shift_x = 3, shift_y = -2;
    std::vector<int> cur = Grab16x16(ref, block_x + shift_x, block_y + shift_y);

    SearchResult full =
        FullSearch(ref, cur.data(), 16, block_x, block_y, range);
    assert(full.mv.mvx == shift_x);
    assert(full.mv.mvy == shift_y);
    assert(full.sad == 0);
    assert(full.points_checked == (2 * range + 1) * (2 * range + 1));  // 289
    std::printf("  [1] FullSearch 找回 MV=(%d,%d) SAD=%d 点数=%d ... ok\n",
                full.mv.mvx, full.mv.mvy, full.sad, full.points_checked);

    SearchResult diamond =
        DiamondSearch(ref, cur.data(), 16, block_x, block_y, range);
    assert(diamond.mv.mvx == shift_x);
    assert(diamond.mv.mvy == shift_y);
    assert(diamond.sad == 0);
    std::printf("  [2] DiamondSearch 找回 MV=(%d,%d) SAD=%d 点数=%d ... ok\n",
                diamond.mv.mvx, diamond.mv.mvy, diamond.sad,
                diamond.points_checked);

    // 菱形搜索点数应「明显」少于全搜索——这是速度-质量权衡的核心证据。
    assert(diamond.points_checked < full.points_checked);
    assert(diamond.points_checked * 3 < full.points_checked);  // 至少 3 倍差距
    std::printf("  [3] DS 点数(%d) 明显 < 全搜索(%d) ... ok\n",
                diamond.points_checked, full.points_checked);
}

// 测试 4：零运动 -> 当前块=同位块，两种搜索都返回 (0,0)、SAD=0。
void TestZeroMotion() {
    GrayFrame ref = MakeTexturedFrame(96, 96);
    const int range = 8;
    const int block_x = 32, block_y = 32;
    std::vector<int> cur = Grab16x16(ref, block_x, block_y);  // 同位、无平移

    SearchResult full =
        FullSearch(ref, cur.data(), 16, block_x, block_y, range);
    assert(full.mv.mvx == 0 && full.mv.mvy == 0 && full.sad == 0);

    SearchResult diamond =
        DiamondSearch(ref, cur.data(), 16, block_x, block_y, range);
    assert(diamond.mv.mvx == 0 && diamond.mv.mvy == 0 && diamond.sad == 0);
    std::printf("  [4] 零运动 -> 两法都得 MV=(0,0) SAD=0 ... ok\n");
}

// 测试 5：range=0 边界 -> 搜索窗口只有一个点，返回零 MV。
void TestRangeZero() {
    GrayFrame ref = MakeTexturedFrame(64, 64);
    std::vector<int> cur = Grab16x16(ref, 16, 16);
    SearchResult full = FullSearch(ref, cur.data(), 16, 16, 16, 0);
    SearchResult diamond = DiamondSearch(ref, cur.data(), 16, 16, 16, 0);
    assert(full.points_checked == 1 && full.mv.mvx == 0 && full.mv.mvy == 0);
    assert(diamond.points_checked == 1 && diamond.mv.mvx == 0);
    std::printf("  [5] range=0 -> 单点，MV=(0,0) ... ok\n");
}

}  // namespace

int main() {
    std::printf("test_motion_search:\n");
    TestKnownShift();
    TestZeroMotion();
    TestRangeZero();
    std::printf("all tests passed\n");
    return 0;
}
