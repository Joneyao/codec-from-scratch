// rate_control.h — H.264 码率控制（D5）。在"画质均匀"和"体积可控"之间走钢丝。
//
// 编码器每帧都要回答一个问题：这一帧用多大的 QP？QP 决定了量化的狠劲——QP 越大，
// 量化步长越大，保留的非零系数越少，编码出来的比特数越少，但画质越糊。反过来 QP
// 越小，画质越好，比特数越多。码率控制就是逐帧挑 QP 的策略。
//
// 两种模式，各有各的痛：
//   1) 固定 QP（mode=0）：每帧都用同一个 QP。画质在帧间是均匀的（同样的量化狠劲），
//      但比特数完全随画面复杂度起伏——静止画面几百比特，剧烈运动几万比特，码率曲线
//      忽高忽低。存储没问题，但走固定带宽的网络会爆。
//   2) 恒定码率 CBR（mode=1）：给定目标码率，用一个"漏桶/缓冲区"模型反馈调整帧级 QP。
//      每帧有一个平均比特预算 target_bits_per_frame = bitrate / fps。实际编码后，
//      把 (actual_bits - target) 累加进缓冲区占用：编超了缓冲变满，编少了缓冲变空。
//      下一帧根据缓冲区占用反向调 QP——缓冲快满就加 QP（压比特），缓冲空就减 QP
//      （提画质）。码率长期收敛到目标附近，但画质在帧间波动。
//
// 揭示点：没有完美解。固定 QP 画质稳、码率飘；CBR 码率稳、画质飘。demo 会用真实
// 数据把两条码率曲线和 CBR 的缓冲区占用画出来，让"钢丝"看得见。
//
// 帧比特模型（教学用简化）：真实编码器编完才知道比特数，这里用一个解析近似替代
// 真实编码——bits ≈ complexity * 2^(-(qp - baseline) / 6)，即 QP 每加 6 比特减半。
// 这条规律和 D2 的"QP 每 +6 非零系数减半"同源。complexity 给不同帧不同值，模拟
// 画面复杂度的起伏。
//
// 命名空间 cfs，C++17。QP 取值范围裁剪到 H.264 的 [0, 51]。
#ifndef CODEC_FROM_SCRATCH_H264_RATE_CONTROL_H
#define CODEC_FROM_SCRATCH_H264_RATE_CONTROL_H

#include <vector>

namespace cfs {

// 码率控制配置。
struct RcConfig {
    int mode = 0;                  // 0=固定QP, 1=CBR
    int target_bitrate_kbps = 0;   // 目标码率（kbps），仅 CBR 用
    int fps = 30;                  // 帧率，用来把码率摊到每帧
    int init_qp = 28;              // 初始/固定 QP
};

// 帧比特模型的 QP 基准点：complexity 在 baseline QP 下大致等于比特数本身。
constexpr int kBitModelBaselineQp = 28;

// H.264 QP 合法范围。
constexpr int kMinQp = 0;
constexpr int kMaxQp = 51;

// 简化帧比特模型：给定画面复杂度 complexity 和量化参数 qp，估算编码比特数。
//   bits ≈ complexity * 2^(-(qp - baseline) / 6)
// QP 每加 6，比特减半；QP 每减 6，比特翻倍。complexity 越大（画面越复杂），
// 同 QP 下比特越多。返回值向下取整到非负整数。
// 这是一个自由函数，不依赖任何状态，方便单测和 demo 直接调用。
int EstimateBitsForQp(double complexity, int qp);

// 逐帧码率控制器。持有 CBR 的缓冲区状态，并记录每帧的 QP / 比特数 / 缓冲区占用
// 供配图。
class RateController {
   public:
    explicit RateController(const RcConfig& cfg);

    // 为第 frame_index 帧挑选 QP。
    //   - 固定 QP 模式：直接返回 init_qp，忽略 complexity。
    //   - CBR 模式：根据当前缓冲区占用反向调整——缓冲越满 QP 越高（压比特），
    //     缓冲越空 QP 越低（提画质）。complexity 用于配合帧比特模型，让调整
    //     更贴近实际（复杂帧倾向给稍高 QP 以守住预算）。
    // 返回裁剪到 [kMinQp, kMaxQp] 的 QP。挑出的 QP 会记入 qp_history。
    int PickQpForFrame(int frame_index, double complexity);

    // 用实际编码比特数更新缓冲区（漏桶/HRD 简化模型）：
    //   buffer_fullness += actual_bits - target_bits_per_frame
    // 再把 buffer_fullness clamp 到 [0, buffer_size]。同时把 actual_bits 记入
    // bits_history、把更新后的缓冲占用记入 buffer_history。
    // 固定 QP 模式也会记录 bits，但缓冲区不参与决策（仅作对照，不影响 QP）。
    void UpdateAfterFrame(int actual_bits);

    // ---- 供配图/断言读取的记录与状态 ----
    const std::vector<int>& qp_history() const { return qp_history_; }
    const std::vector<int>& bits_history() const { return bits_history_; }
    const std::vector<double>& buffer_history() const { return buffer_history_; }

    double buffer_fullness() const { return buffer_fullness_; }
    double buffer_size() const { return buffer_size_; }
    double target_bits_per_frame() const { return target_bits_per_frame_; }

    // 已记录的比特总数（用于算长期平均码率）。
    long long total_bits() const;

   private:
    RcConfig cfg_;

    // 每帧平均比特预算 = target_bitrate / fps（比特）。
    double target_bits_per_frame_ = 0.0;

    // 缓冲区容量（比特）。取若干帧预算，给码率短期波动留缓冲空间。
    double buffer_size_ = 0.0;

    // 当前缓冲区占用（比特），初始置于半满，给上下调都留余地。
    double buffer_fullness_ = 0.0;

    std::vector<int> qp_history_;
    std::vector<int> bits_history_;
    std::vector<double> buffer_history_;
};

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_H264_RATE_CONTROL_H
