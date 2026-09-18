// rate_control.cpp — 码率控制实现。见 rate_control.h 的原理说明。
#include "rate_control.h"

#include <algorithm>
#include <cmath>

namespace cfs {

namespace {

// 把 QP 裁剪到 H.264 合法范围。
int ClampQp(int qp) { return std::max(kMinQp, std::min(kMaxQp, qp)); }

}  // namespace

int EstimateBitsForQp(double complexity, int qp) {
    if (complexity <= 0.0) return 0;
    // bits ≈ complexity * 2^(-(qp - baseline) / 6)
    // QP 每加 6，指数减 1，比特减半。
    double exponent = -static_cast<double>(qp - kBitModelBaselineQp) / 6.0;
    double bits = complexity * std::pow(2.0, exponent);
    if (bits < 0.0) bits = 0.0;
    return static_cast<int>(bits);  // 向下取整
}

RateController::RateController(const RcConfig& cfg) : cfg_(cfg) {
    // 每帧平均比特预算 = 码率(bps) / fps。target_bitrate_kbps * 1000 = bps。
    int fps = cfg_.fps > 0 ? cfg_.fps : 30;
    target_bits_per_frame_ =
        static_cast<double>(cfg_.target_bitrate_kbps) * 1000.0 / fps;

    // 缓冲区容量取 2 秒的码率（= 2*fps 帧的预算），给短期波动足够回旋空间。
    // 这是 HRD/漏桶里 CPB 大小的教学化取值。
    buffer_size_ = target_bits_per_frame_ * fps * 2.0;
    if (buffer_size_ <= 0.0) buffer_size_ = 1.0;  // 防御：固定QP或零码率时避免除零

    // 初始置于半满：既不至于一开始就判定"快满要加 QP"，也不至于"空了猛降 QP"。
    buffer_fullness_ = buffer_size_ * 0.5;
}

int RateController::PickQpForFrame(int frame_index, double complexity) {
    int qp;

    if (cfg_.mode == 0) {
        // 固定 QP：无视一切，恒返回 init_qp。
        qp = ClampQp(cfg_.init_qp);
    } else {
        // CBR：以 init_qp 为中心，按缓冲区占用反向偏移。
        // occupancy 在 [0,1]：0=空，1=满，0.5=半满（平衡点）。
        double occupancy = buffer_fullness_ / buffer_size_;
        occupancy = std::max(0.0, std::min(1.0, occupancy));

        // 偏离半满越远，调整越猛。deviation ∈ [-0.5, +0.5]。
        // 缓冲偏满(deviation>0) -> 加 QP 压比特；偏空(deviation<0) -> 减 QP 提画质。
        double deviation = occupancy - 0.5;

        // 缓冲反馈项：把 [-0.5,+0.5] 的偏离映射到 QP 调整量。
        // 系数 kBufferGain 决定反馈强度：满时最多加 ~9 QP，空时最多减 ~9 QP。
        constexpr double kBufferGain = 18.0;
        double qp_adjust = deviation * kBufferGain;

        // 复杂度前馈项（可选、弱）：这一帧若明显比"基准复杂度"复杂，
        // 单靠预算会超支，先给一点点正偏移守预算。基准复杂度 = 在 init_qp 下
        // 正好花掉 target_bits_per_frame 的复杂度。
        int base_qp = ClampQp(cfg_.init_qp);
        int bits_at_base =
            EstimateBitsForQp(complexity, base_qp);
        if (target_bits_per_frame_ > 0.0 && bits_at_base > 0) {
            double ratio = bits_at_base / target_bits_per_frame_;
            // ratio>1 说明这帧在 base_qp 下会超预算，log2(ratio)*6 正好是
            // "要多加多少 QP 才能回到预算"（因为 QP 每 +6 比特减半）。
            // 只取一部分（0.5）做前馈，主反馈仍交给缓冲区，避免抢戏。
            double feedforward = std::log2(ratio) * 6.0 * 0.5;
            qp_adjust += feedforward;
        }

        qp = ClampQp(base_qp + static_cast<int>(std::lround(qp_adjust)));
    }

    qp_history_.push_back(qp);
    (void)frame_index;  // 记录顺序即帧序，索引本身未直接参与决策
    return qp;
}

void RateController::UpdateAfterFrame(int actual_bits) {
    bits_history_.push_back(actual_bits);

    // 漏桶：本帧编码消耗 actual_bits，同时按预算"漏掉" target_bits_per_frame。
    // 净增 = actual_bits - target：编超了缓冲涨，编少了缓冲落。
    buffer_fullness_ += static_cast<double>(actual_bits) - target_bits_per_frame_;

    // 上下限 clamp：缓冲不能为负（欠载=画质有富余），不能超容量（过载=会丢帧）。
    buffer_fullness_ = std::max(0.0, std::min(buffer_size_, buffer_fullness_));

    buffer_history_.push_back(buffer_fullness_);
}

long long RateController::total_bits() const {
    long long sum = 0;
    for (int b : bits_history_) sum += b;
    return sum;
}

}  // namespace cfs
