// rate_control_demo.cpp — 用一段 30 帧、复杂度有起伏的序列，分别跑固定 QP 和 CBR，
// 导出对比配图数据到 chart_data/rate_control.txt。
//
// 揭示点用真实数字画出来：
//   固定 QP —— QP 一条直线，但每帧比特数随复杂度剧烈起伏，码率波动大。
//   CBR    —— 比特数被压平（贴近目标预算），代价是 QP 上下波动、缓冲区在安全
//             区间起伏。
//
// 输出格式（每行）：frame mode qp bits buffer
//   mode = fixed | cbr
//   固定 QP 行的 buffer 恒为 0（固定模式不依赖缓冲区，用 0 占位）。
// 便于 matplotlib 读：按 mode 分两组，画 bits 曲线对比 + CBR 的 buffer 曲线。
//
// 用法：./rate_control_demo [chart_data_dir]
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "rate_control.h"

using cfs::RateController;
using cfs::RcConfig;

namespace {

// 构造 30 帧的复杂度序列：忽高忽低，含平稳段 + 剧烈运动段 + 场景切换阶跃。
// 数值量级选得让固定 QP 下的比特数波动明显。
std::vector<double> BuildComplexitySequence(int n) {
    std::vector<double> c(n);
    for (int i = 0; i < n; ++i) {
        double base = 40000.0;
        double wave = 22000.0 * std::sin(i * 0.5);      // 周期性起伏
        double step = (i >= 10 && i < 18) ? 35000.0 : 0.0;  // 一段剧烈运动
        double dip = (i >= 22 && i < 26) ? -28000.0 : 0.0;  // 一段静止画面
        double val = base + wave + step + dip;
        if (val < 2000.0) val = 2000.0;
        c[i] = val;
    }
    return c;
}

struct FrameRecord {
    int frame;
    int qp;
    int bits;
    double buffer;  // 固定模式恒 0
};

// 跑一遍某个模式，返回每帧记录。
std::vector<FrameRecord> RunMode(const RcConfig& cfg,
                                 const std::vector<double>& complexity) {
    RateController rc(cfg);
    std::vector<FrameRecord> recs;
    for (size_t i = 0; i < complexity.size(); ++i) {
        int qp = rc.PickQpForFrame(static_cast<int>(i), complexity[i]);
        int bits = cfs::EstimateBitsForQp(complexity[i], qp);
        rc.UpdateAfterFrame(bits);
        double buf = (cfg.mode == 1) ? rc.buffer_fullness() : 0.0;
        recs.push_back({static_cast<int>(i), qp, bits, buf});
    }
    return recs;
}

// 计算比特数的均值和波动（标准差 / 均值 = 变异系数），用来量化"码率波动大小"。
void BitsStats(const std::vector<FrameRecord>& recs, double& mean_out,
               double& cv_out, int& min_out, int& max_out) {
    double sum = 0.0;
    min_out = recs.empty() ? 0 : recs[0].bits;
    max_out = recs.empty() ? 0 : recs[0].bits;
    for (const auto& r : recs) {
        sum += r.bits;
        if (r.bits < min_out) min_out = r.bits;
        if (r.bits > max_out) max_out = r.bits;
    }
    mean_out = recs.empty() ? 0.0 : sum / recs.size();
    double var = 0.0;
    for (const auto& r : recs) {
        double d = r.bits - mean_out;
        var += d * d;
    }
    var = recs.empty() ? 0.0 : var / recs.size();
    double sd = std::sqrt(var);
    cv_out = mean_out > 0.0 ? sd / mean_out : 0.0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string out_dir = argc > 1 ? argv[1] : "chart_data";

    const int kFrames = 30;
    const int kFps = 30;
    const int kTargetKbps = 2000;  // 2 Mbps
    const int kInitQp = 28;

    std::vector<double> complexity = BuildComplexitySequence(kFrames);

    // 固定 QP 模式。
    RcConfig fixed_cfg;
    fixed_cfg.mode = 0;
    fixed_cfg.init_qp = kInitQp;
    fixed_cfg.fps = kFps;
    std::vector<FrameRecord> fixed = RunMode(fixed_cfg, complexity);

    // CBR 模式（同样的初始 QP 与帧率，加上目标码率）。
    RcConfig cbr_cfg;
    cbr_cfg.mode = 1;
    cbr_cfg.target_bitrate_kbps = kTargetKbps;
    cbr_cfg.fps = kFps;
    cbr_cfg.init_qp = kInitQp;
    std::vector<FrameRecord> cbr = RunMode(cbr_cfg, complexity);

    // ---- 控制台摘要 ----
    double fm, fcv, cm, ccv;
    int fmin, fmax, cmin, cmax;
    BitsStats(fixed, fm, fcv, fmin, fmax);
    BitsStats(cbr, cm, ccv, cmin, cmax);

    double target_bits_per_frame =
        static_cast<double>(kTargetKbps) * 1000.0 / kFps;

    std::printf("==== H.264 码率控制对比（30 帧，复杂度起伏）====\n");
    std::printf("目标码率 %d kbps @ %d fps -> 每帧预算 %.0f bits\n\n",
                kTargetKbps, kFps, target_bits_per_frame);

    std::printf("固定 QP=%d：\n", kInitQp);
    std::printf("  每帧比特 均值 %.0f，范围 [%d, %d]，波动系数 CV=%.2f\n", fm,
                fmin, fmax, fcv);
    std::printf("  -> QP 恒定画质均匀，但比特数随复杂度剧烈起伏（码率波动大）\n\n");

    std::printf("CBR（目标 %d kbps）：\n", kTargetKbps);
    std::printf("  每帧比特 均值 %.0f，范围 [%d, %d]，波动系数 CV=%.2f\n", cm,
                cmin, cmax, ccv);
    std::printf("  -> 比特被压向预算，码率平稳；代价是 QP 波动、缓冲区起伏\n\n");

    std::printf("波动对比：固定 QP 的比特 CV=%.2f，CBR 的 CV=%.2f", fcv, ccv);
    if (ccv < fcv) {
        std::printf("（CBR 把码率波动压小了 %.0f%%）\n",
                    (1.0 - ccv / fcv) * 100.0);
    } else {
        std::printf("\n");
    }

    // ---- 导出 chart_data ----
    std::string path = out_dir + "/rate_control.txt";
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        std::fprintf(stderr, "无法写入 %s\n", path.c_str());
        return 1;
    }
    std::fprintf(f, "# H.264 rate control demo: fixed-QP vs CBR\n");
    std::fprintf(f, "# target_bitrate_kbps %d  fps %d  init_qp %d\n",
                 kTargetKbps, kFps, kInitQp);
    std::fprintf(f, "# target_bits_per_frame %.1f\n", target_bits_per_frame);
    std::fprintf(f, "# columns: frame mode qp bits buffer\n");
    std::fprintf(f, "# (fixed 模式 buffer 恒为 0，仅占位)\n");
    for (const auto& r : fixed)
        std::fprintf(f, "%d fixed %d %d %.0f\n", r.frame, r.qp, r.bits,
                     r.buffer);
    for (const auto& r : cbr)
        std::fprintf(f, "%d cbr %d %d %.0f\n", r.frame, r.qp, r.bits, r.buffer);
    std::fclose(f);
    std::printf("\ndumped -> %s\n", path.c_str());
    return 0;
}
