#!/usr/bin/env python3
"""B3 MJPEG vs H.264 对比篇配图。

所有数字来自真实测量：
  - codec-from-scratch/mjpeg/encoder/chart_data/compare.json
    由 mjpeg/tools/compare_mjpeg_h264.py 在本机跑出（我们的 MJPEG 编码器
    + ffmpeg libx264 + ffmpeg mjpeg 的真实文件字节数与编码耗时）。
不手编数字。若 compare.json 不存在，先跑对比脚本再生成配图。
"""
import json
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.font_manager as fm
fm._load_fontmanager(try_read_cache=False)
import matplotlib.pyplot as plt
import numpy as np

plt.rcParams["font.family"] = "Noto Sans CJK JP"
plt.rcParams["axes.unicode_minus"] = False

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(
    HERE, "../../../../../third-party/codec-from-scratch"))
COMPARE = os.path.join(REPO, "mjpeg/encoder/chart_data/compare.json")

C_BLUE = "#2563eb"
C_GREEN = "#16a34a"
C_ORANGE = "#ea580c"
C_RED = "#dc2626"
C_GRAY = "#94a3b8"
C_PURPLE = "#7c3aed"


def load():
    with open(COMPARE) as f:
        return json.load(f)


def fmt_kb(b):
    return "%.1f KB" % (b / 1024.0)


# --- 图1：四种编码方式文件体积对比（真实字节） ---
def chart_size(d):
    labels = ["我们的\nMJPEG", "ffmpeg\nMJPEG", "H.264\n全 I 帧", "H.264\n帧间预测"]
    vals = [d["our_mjpeg"]["bytes"], d["ffmpeg_mjpeg"]["bytes"],
            d["h264_allintra"]["bytes"], d["h264_inter"]["bytes"]]
    colors = [C_ORANGE, C_GRAY, C_BLUE, C_GREEN]
    fig, ax = plt.subplots(figsize=(7.2, 4.2))
    bars = ax.bar(labels, [v / 1024 for v in vals], color=colors, width=0.62)
    for bar, v in zip(bars, vals):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 1.5,
                fmt_kb(v), ha="center", va="bottom", fontsize=11,
                fontweight="bold")
    ratio = vals[0] / vals[3]
    ax.set_ylabel("文件大小 (KB)", fontsize=12)
    ax.set_title("同一段 20 帧运动序列 · 四种编码的真实体积\n"
                 "(192x144, fps=10, 我们的MJPEG是H.264帧间预测的 %.1f 倍)" % ratio,
                 fontsize=12.5, fontweight="bold")
    ax.set_ylim(0, max(v / 1024 for v in vals) * 1.18)
    ax.grid(axis="y", alpha=0.3)
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "chart_size.png"), dpi=130)
    plt.close()


# --- 图2：帧间预测省了多少（全I帧 vs 帧间预测） ---
def chart_interframe_value(d):
    intra = d["h264_allintra"]["bytes"]
    inter = d["h264_inter"]["bytes"]
    saved = intra - inter
    fig, ax = plt.subplots(figsize=(7.0, 4.0))
    labels = ["H.264 全 I 帧\n(每帧独立)", "H.264 帧间预测\n(P帧参考前帧)"]
    vals = [intra, inter]
    colors = [C_BLUE, C_GREEN]
    bars = ax.barh(labels, [v / 1024 for v in vals], color=colors, height=0.5)
    for bar, v in zip(bars, vals):
        ax.text(bar.get_width() + 0.6, bar.get_y() + bar.get_height() / 2,
                fmt_kb(v), va="center", fontsize=12, fontweight="bold")
    ax.set_xlabel("文件大小 (KB)", fontsize=12)
    ax.set_title("同一个 x264 编码器，唯一差别是允不允许帧间预测\n"
                 "帧间预测省下 %s（体积降到 %.0f%%）"
                 % (fmt_kb(saved), inter / intra * 100),
                 fontsize=12.5, fontweight="bold")
    ax.set_xlim(0, intra / 1024 * 1.25)
    ax.invert_yaxis()
    ax.grid(axis="x", alpha=0.3)
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "chart_interframe.png"), dpi=130)
    plt.close()


# --- 图3：每帧大小分布（MJPEG均匀 vs H.264的I/P悬殊） ---
def chart_perframe(d):
    ours = d["our_mjpeg"]["per_frame_bytes"]
    h264 = d["h264_inter"].get("per_frame_bytes", [])
    types = d["h264_inter"].get("per_frame_type", [])
    n = min(len(ours), len(h264)) if h264 else len(ours)
    x = np.arange(1, n + 1)
    fig, ax = plt.subplots(figsize=(8.0, 4.2))
    ax.plot(x, [b / 1024 for b in ours[:n]], "-o", color=C_ORANGE,
            lw=2, ms=4, label="我们的 MJPEG（每帧独立 JPEG）")
    if h264:
        ax.plot(x, [b / 1024 for b in h264[:n]], "-s", color=C_GREEN,
                lw=2, ms=4, label="H.264 帧间预测")
        # 标出 I 帧
        for i, t in enumerate(types[:n]):
            if t == "I":
                ax.annotate("I 帧", (x[i], h264[i] / 1024),
                            textcoords="offset points", xytext=(6, 10),
                            fontsize=10, color=C_RED, fontweight="bold")
                ax.plot(x[i], h264[i] / 1024, "s", color=C_RED, ms=8)
    ax.set_xlabel("帧序号", fontsize=12)
    ax.set_ylabel("该帧编码后大小 (KB)", fontsize=12)
    ax.set_title("每帧大小分布：MJPEG 均匀，H.264 的 I/P 帧悬殊\n"
                 "MJPEG 每帧都是完整 JPEG；H.264 只有首帧是完整 I 帧，其余靠预测",
                 fontsize=12, fontweight="bold")
    ax.legend(fontsize=11, loc="center right")
    ax.grid(alpha=0.3)
    ax.set_ylim(0, max(b / 1024 for b in ours[:n]) * 1.15)
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "chart_perframe.png"), dpi=130)
    plt.close()


# --- 图4：随机访问代价示意（H.264要回溯到I帧 vs MJPEG直接跳） ---
def chart_random_access(d):
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8.4, 4.6))
    target = 12   # 想跳到第 12 帧
    n = 16
    for ax, title, mode in [
            (ax1, "MJPEG：每帧独立，直接跳到目标帧解码", "mjpeg"),
            (ax2, "H.264：P帧依赖前帧，要回溯到最近 I 帧顺序重放", "h264")]:
        for i in range(1, n + 1):
            if mode == "mjpeg":
                fc = C_ORANGE if i == target else "#fde4d3"
                ec = C_ORANGE
            else:
                if i == 1:
                    fc, ec = C_RED, C_RED           # I 帧
                elif i <= target:
                    fc, ec = "#cdeccd", C_GREEN     # 必须重放的 P 帧
                else:
                    fc, ec = "#eef2f7", C_GRAY
            ax.add_patch(plt.Rectangle((i, 0), 0.82, 1, fc=fc, ec=ec, lw=1.4))
            lab = "I" if (mode == "h264" and i == 1) else str(i)
            ax.text(i + 0.41, 0.5, lab, ha="center", va="center", fontsize=9,
                    fontweight="bold" if i == target or (mode=="h264" and i==1)
                    else "normal")
        # 目标帧箭头
        ax.annotate("要第 %d 帧" % target, (target + 0.41, 1.05),
                    ha="center", fontsize=10, color="#333", fontweight="bold")
        if mode == "mjpeg":
            ax.text(0.5, 0.5, "解 1 帧", ha="right", va="center",
                    fontsize=10, color=C_ORANGE, fontweight="bold")
        else:
            ax.text(0.5, 0.5, "解 %d 帧" % target, ha="right", va="center",
                    fontsize=10, color=C_RED, fontweight="bold")
        ax.set_xlim(-1.4, n + 1)
        ax.set_ylim(-0.2, 1.5)
        ax.axis("off")
        ax.set_title(title, fontsize=11.5, fontweight="bold", loc="left")
    fig.suptitle("随机访问：跳到中间某帧的代价", fontsize=13, fontweight="bold")
    plt.tight_layout(rect=[0, 0, 1, 0.95])
    plt.savefig(os.path.join(HERE, "chart_random_access.png"), dpi=130)
    plt.close()


# --- 图5（形象层）：MJPEG 应用场景选型 ---
def chart_scenarios():
    fig, ax = plt.subplots(figsize=(8.4, 4.6))
    ax.axis("off")
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 6)
    ax.text(5, 5.6, "为什么这些场景至今选 MJPEG", ha="center",
            fontsize=14, fontweight="bold")
    ax.text(5, 5.1, "MJPEG 的\"缺点\"（浪费码率）在这些场景反而是优点",
            ha="center", fontsize=10.5, color="#566573")
    cards = [
        ("监控 / 安防", "抗丢帧：丢一帧不影响后续\n每帧独立、可靠随机访问", C_BLUE),
        ("医疗影像", "每帧无损/高保真独立可查\n不接受帧间预测引入的依赖", C_GREEN),
        ("工业采集卡", "编码延迟低：无需运动搜索\n实时性优先于体积", C_ORANGE),
        ("老相机 / 内窥镜", "实现简单、硬件成本低\nJPEG 芯片现成、逐帧可剪", C_PURPLE),
    ]
    xs = [0.4, 5.2]
    ys = [2.7, 0.3]
    i = 0
    for r in range(2):
        for c in range(2):
            title, desc, col = cards[i]
            x, y = xs[c], ys[r]
            ax.add_patch(plt.Rectangle((x, y), 4.4, 2.0, fc="white",
                                       ec=col, lw=2.2, zorder=2,
                                       joinstyle="round"))
            ax.add_patch(plt.Rectangle((x, y + 1.5), 4.4, 0.5, fc=col,
                                       ec=col, lw=2.2, zorder=3))
            ax.text(x + 2.2, y + 1.75, title, ha="center", va="center",
                    fontsize=12, fontweight="bold", color="white", zorder=4)
            ax.text(x + 2.2, y + 0.72, desc, ha="center", va="center",
                    fontsize=10, color="#333", zorder=4)
            i += 1
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "chart_scenarios.png"), dpi=130)
    plt.close()


def main():
    d = load()
    chart_size(d)
    chart_interframe_value(d)
    chart_perframe(d)
    chart_random_access(d)
    chart_scenarios()
    # 进度地图：MJPEG 系列总览（不高亮单一环节，传入不存在的 key 即全灰总览）
    import sys
    sys.path.insert(0, os.path.join(HERE, ".."))
    from pipeline_map import draw_mjpeg_map
    draw_mjpeg_map("all", os.path.join(HERE, "stage_map.png"))
    print("charts generated in", HERE)


if __name__ == "__main__":
    main()
