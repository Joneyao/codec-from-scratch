// test_rate_control.cpp — 校验 H.264 码率控制（D5）。
//
// 覆盖：
//   1) 帧比特模型：QP 每 +6 比特减半；complexity 越大比特越多；QP 越大比特越少。
//   2) 固定 QP 模式：PickQpForFrame 恒返回 init_qp，无视复杂度起伏。
//   3) CBR 模式 - 缓冲反馈方向：缓冲将满时挑出的 QP 高于半满时（反向调整）。
//   4) CBR 模式 - 码率收敛：喂一串复杂度忽高忽低的帧跑完，长期平均码率收敛到
//      目标附近（合理误差内）。
//   5) CBR 缓冲区始终在 [0, buffer_size] 安全区间内。
// Release 构建默认带 -DNDEBUG 会抹掉 assert，导致只在断言里用到的变量被判为
// 未使用。测试的价值就在断言，这里强制启用 assert。
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "rate_control.h"

using cfs::RateController;
using cfs::RcConfig;

namespace {

// 测试 1：帧比特模型的三条基本规律。
void TestBitModel() {
    double c = 10000.0;
    int base = cfs::kBitModelBaselineQp;

    // QP=baseline 时比特 ≈ complexity。
    int bits_base = cfs::EstimateBitsForQp(c, base);
    assert(bits_base == 10000);

    // QP 每 +6 比特减半。
    int bits_plus6 = cfs::EstimateBitsForQp(c, base + 6);
    assert(std::abs(bits_plus6 - 5000) <= 1);

    // QP 每 -6 比特翻倍。
    int bits_minus6 = cfs::EstimateBitsForQp(c, base - 6);
    assert(std::abs(bits_minus6 - 20000) <= 1);

    // QP 越大比特单调不增。
    int prev = cfs::EstimateBitsForQp(c, 10);
    for (int qp = 11; qp <= 51; ++qp) {
        int b = cfs::EstimateBitsForQp(c, qp);
        assert(b <= prev);
        prev = b;
    }

    // complexity 越大同 QP 下比特越多。
    assert(cfs::EstimateBitsForQp(20000.0, base) >
           cfs::EstimateBitsForQp(5000.0, base));

    // complexity 为 0 -> 0 比特。
    assert(cfs::EstimateBitsForQp(0.0, base) == 0);
}

// 测试 2：固定 QP 模式恒返回 init_qp。
void TestFixedQp() {
    RcConfig cfg;
    cfg.mode = 0;
    cfg.init_qp = 30;
    cfg.fps = 30;
    RateController rc(cfg);

    // 喂复杂度忽高忽低的一串帧，QP 必须纹丝不动。
    double complexities[] = {2000, 50000, 800, 30000, 12000, 60000};
    for (int i = 0; i < 6; ++i) {
        int qp = rc.PickQpForFrame(i, complexities[i]);
        assert(qp == 30);
        int bits = cfs::EstimateBitsForQp(complexities[i], qp);
        rc.UpdateAfterFrame(bits);
    }
    // qp_history 全是 30。
    for (int qp : rc.qp_history()) assert(qp == 30);
}

// 测试 3：CBR 缓冲反馈方向。手动把缓冲区推到"将满"，挑出的 QP 应高于半满时。
void TestCbrFeedbackDirection() {
    RcConfig cfg;
    cfg.mode = 1;
    cfg.target_bitrate_kbps = 2000;
    cfg.fps = 30;
    cfg.init_qp = 28;

    // 场景 A：缓冲半满（初始态）下挑 QP。
    RateController rc_half(cfg);
    int qp_half = rc_half.PickQpForFrame(0, 10000.0);

    // 场景 B：把缓冲区喂到接近满——连续喂远超预算的大比特帧。
    RateController rc_full(cfg);
    double big = rc_full.target_bits_per_frame() * 5.0;  // 每帧都严重超预算
    for (int i = 0; i < 20; ++i) {
        rc_full.PickQpForFrame(i, 10000.0);
        rc_full.UpdateAfterFrame(static_cast<int>(big));
    }
    // 此时缓冲区应接近满。
    assert(rc_full.buffer_fullness() > rc_full.buffer_size() * 0.8);
    int qp_full = rc_full.PickQpForFrame(21, 10000.0);

    // 缓冲将满 -> QP 更高（压比特）。
    assert(qp_full > qp_half);

    // 反向：把缓冲区喂到接近空——连续喂远低于预算的小比特帧。
    RateController rc_empty(cfg);
    for (int i = 0; i < 20; ++i) {
        rc_empty.PickQpForFrame(i, 10000.0);
        rc_empty.UpdateAfterFrame(0);  // 一点比特都不用
    }
    assert(rc_empty.buffer_fullness() < rc_empty.buffer_size() * 0.2);
    int qp_empty = rc_empty.PickQpForFrame(21, 10000.0);
    // 缓冲将空 -> QP 更低（提画质）。
    assert(qp_empty < qp_half);
}

// 测试 4 + 5：CBR 喂复杂度起伏序列，长期平均码率收敛到目标，缓冲区始终在安全区间。
void TestCbrConvergence() {
    RcConfig cfg;
    cfg.mode = 1;
    cfg.target_bitrate_kbps = 2000;  // 2 Mbps
    cfg.fps = 30;
    cfg.init_qp = 28;
    RateController rc(cfg);

    const int kFrames = 300;  // 10 秒，够长期收敛
    for (int i = 0; i < kFrames; ++i) {
        // 复杂度忽高忽低：正弦起伏 + 阶跃扰动，模拟场景切换。
        double base = 40000.0;
        double wave = 25000.0 * std::sin(i * 0.3);
        double step = (i % 50 < 25) ? 15000.0 : -15000.0;
        double complexity = base + wave + step;
        if (complexity < 1000.0) complexity = 1000.0;

        int qp = rc.PickQpForFrame(i, complexity);
        int bits = cfs::EstimateBitsForQp(complexity, qp);
        rc.UpdateAfterFrame(bits);

        // 缓冲区始终在安全区间。
        assert(rc.buffer_fullness() >= 0.0);
        assert(rc.buffer_fullness() <= rc.buffer_size());
    }

    // 长期平均码率 = 总比特 / 时长(秒)。
    double duration_sec = static_cast<double>(kFrames) / cfg.fps;
    double avg_bitrate_bps = static_cast<double>(rc.total_bits()) / duration_sec;
    double target_bps = cfg.target_bitrate_kbps * 1000.0;
    double rel_err = std::fabs(avg_bitrate_bps - target_bps) / target_bps;

    std::printf(
        "  CBR 收敛：平均码率 %.0f bps，目标 %.0f bps，相对误差 %.1f%%\n",
        avg_bitrate_bps, target_bps, rel_err * 100.0);

    // 收敛到目标 ±20% 内（教学模型 + 简单反馈，这个误差是合理的）。
    assert(rel_err < 0.20);
}

}  // namespace

int main() {
    TestBitModel();
    TestFixedQp();
    TestCbrFeedbackDirection();
    TestCbrConvergence();
    std::printf("all tests passed\n");
    return 0;
}
