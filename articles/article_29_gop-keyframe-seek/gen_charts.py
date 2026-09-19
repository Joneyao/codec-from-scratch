#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""E3 GOP 与关键帧间隔 配图生成。

所有数据来自 ffmpeg/ffprobe 对真实文件的实测（见同目录 measurements.txt、
gop_size_bitrate.csv、seek_cost.csv、work/keyframes_g*.csv）：
  素材: testsrc2 640x360 30fps 10s (300帧), libx264 preset medium, sc_threshold=0
  用 -g 15/30/60/300 编码同一素材，ffprobe -skip_frame nokey 读关键帧位置。

复现：README 记录的 ffmpeg 命令生成中间文件，关键帧时间戳已固化为同目录
keyframes_g{15,30,60,300}.csv；本脚本读这些 CSV 绘图，无需 work/ 中间产物。
"""
import os
import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# CJK 字体（优先项目共享模块，失败回退 Noto Sans CJK）
_dir = os.path.dirname(os.path.abspath(__file__))
while _dir != os.path.dirname(_dir):
    if os.path.isdir(os.path.join(_dir, "backend")):
        sys.path.insert(0, os.path.join(_dir, "backend"))
        break
    _dir = os.path.dirname(_dir)
try:
    from src.utils.font_helper import setup_cjk_font
    setup_cjk_font()
except Exception:
    plt.rcParams["font.family"] = "Noto Sans CJK JP"
plt.rcParams["axes.unicode_minus"] = False

OUT = os.path.dirname(os.path.abspath(__file__))
# 关键帧时间戳 CSV 持久化在文章目录（keyframes_gN.csv），可脱离 work/ 复现绘图
WORK = OUT
C_BLUE = "#2f6690"
C_RED = "#d1495b"
C_GREEN = "#4c9f70"
C_GRAY = "#b8b8b8"
C_ORANGE = "#e6a817"

GOPS = [15, 30, 60, 300]
COLORS = {15: C_GREEN, 30: C_BLUE, 60: C_ORANGE, 300: C_RED}


def _read_keyframes(g):
    """从实测 CSV 读关键帧时间戳（同目录 keyframes_gN.csv，跳过表头）。"""
    path = os.path.join(WORK, f"keyframes_g{g}.csv")
    kf = []
    with open(path) as f:
        for line in f:
            line = line.strip().rstrip(",")
            if not line or not line[0].isdigit():
                continue  # 跳过表头 pts_time 等非数字行
            kf.append(float(line))
    return sorted(kf)


def _read_size_bitrate():
    """从实测 CSV 读体积/码率（gop_size_bitrate.csv）。"""
    import csv
    data = {}
    with open(os.path.join(OUT, "gop_size_bitrate.csv")) as f:
        for row in csv.DictReader(f):
            data[int(row["GOP"])] = (
                int(row["file_size_bytes"]),
                int(row["bit_rate_bps"]),
            )
    return data


# 图1: 不同 GOP 的关键帧在时间轴上的分布（竖线），直观看 seek 落点粗细
def chart_keyframe_timeline():
    fig, ax = plt.subplots(figsize=(9.2, 4.4))
    for i, g in enumerate(GOPS):
        y = len(GOPS) - i
        kf = _read_keyframes(g)
        # 整条时间轴底色
        ax.hlines(y, 0, 10, color=C_GRAY, alpha=0.35, lw=6)
        # 关键帧竖线（可落点）
        for t in kf:
            ax.vlines(t, y - 0.28, y + 0.28, color=COLORS[g], lw=2)
        ax.text(-0.35, y, f"GOP={g}", ha="right", va="center",
                fontsize=11, fontweight="bold", color=COLORS[g])
        ax.text(10.25, y, f"{len(kf)} 个落点", ha="left", va="center",
                fontsize=9.5, color="#555")
    ax.set_xlim(-1.8, 12.2)
    ax.set_ylim(0.3, len(GOPS) + 0.8)
    ax.set_xlabel("时间（秒）——每条竖线是一个关键帧，也就是 seek 唯一能落的点", fontsize=10)
    ax.set_xticks(range(0, 11))
    ax.set_yticks([])
    ax.set_title("GOP 越大，关键帧越稀，进度条能落的点越粗（ffmpeg 实测）",
                 fontsize=12.5, pad=10)
    for s in ("top", "right", "left"):
        ax.spines[s].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "keyframe_timeline.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# 图2: GOP 大小 vs 文件体积 vs seek 最坏延迟（双轴），体现权衡
def chart_tradeoff():
    sb = _read_size_bitrate()
    sizes_kb = [sb[g][0] / 1024 for g in GOPS]
    # 最坏 seek 回退秒数 = 关键帧间隔 = duration / keyframes
    worst_seek = []
    for g in GOPS:
        kf = _read_keyframes(g)
        if len(kf) > 1:
            worst_seek.append(max(kf[i + 1] - kf[i] for i in range(len(kf) - 1)))
        else:
            worst_seek.append(10.0)  # 单关键帧：整段都要回退到 0
    x = range(len(GOPS))
    labels = [str(g) for g in GOPS]

    fig, ax1 = plt.subplots(figsize=(9, 4.8))
    # 文件体积（柱）
    bars = ax1.bar(x, sizes_kb, width=0.5, color=C_BLUE, alpha=0.85,
                   label="文件体积 (KB)")
    ax1.set_ylabel("文件体积（KB）", color=C_BLUE, fontsize=10.5)
    ax1.tick_params(axis="y", labelcolor=C_BLUE)
    ax1.set_ylim(0, max(sizes_kb) * 1.25)
    for b, v in zip(bars, sizes_kb):
        ax1.text(b.get_x() + b.get_width() / 2, b.get_height() + 12,
                 f"{v:.0f}", ha="center", fontsize=9.5, color=C_BLUE)
    # seek 最坏延迟（线，右轴）
    ax2 = ax1.twinx()
    ax2.plot(x, worst_seek, "-o", color=C_RED, lw=2.4, markersize=8,
             label="最坏 seek 回退（秒）")
    ax2.set_ylabel("最坏 seek 回退（秒）", color=C_RED, fontsize=10.5)
    ax2.tick_params(axis="y", labelcolor=C_RED)
    ax2.set_ylim(0, max(worst_seek) * 1.2)
    for xi, v in zip(x, worst_seek):
        ax2.text(xi, v + 0.35, f"{v:.1f}s", ha="center", fontsize=9.5,
                 color=C_RED, fontweight="bold")
    ax1.set_xticks(list(x))
    ax1.set_xticklabels(labels, fontsize=11)
    ax1.set_xlabel("GOP 大小（关键帧间隔，帧）", fontsize=10.5)
    ax1.set_title("经典权衡：GOP 越大，体积越小（省），但 seek 越粗（卡）",
                  fontsize=12.5, pad=10)
    for s in ("top",):
        ax1.spines[s].set_visible(False)
        ax2.spines[s].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "gop_tradeoff.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# 图3: MJPEG（全关键帧）vs H.264（GOP=300）体积与 seek 对比（实测）
def chart_mjpeg_extreme():
    # 实测数据（见 measurements.txt）
    labels = ["H.264\nGOP=300", "MJPEG\n每帧关键帧"]
    sizes_kb = [954597 / 1024, 5201318 / 1024]   # 932 KB vs 5079 KB
    seek_worst = [10.0, 0.0]                       # 最坏 seek 回退秒数
    x = range(len(labels))

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9.2, 4.3))
    # 左：体积
    b1 = ax1.bar(x, sizes_kb, width=0.5, color=[C_BLUE, C_RED], alpha=0.88)
    for b, v in zip(b1, sizes_kb):
        ax1.text(b.get_x() + b.get_width() / 2, b.get_height() + 90,
                 f"{v:.0f} KB", ha="center", fontsize=10.5, fontweight="bold")
    ax1.set_ylim(0, max(sizes_kb) * 1.2)
    ax1.set_ylabel("文件体积（KB）", fontsize=10.5)
    ax1.set_xticks(list(x))
    ax1.set_xticklabels(labels, fontsize=10.5)
    ax1.set_title("体积：MJPEG 是 5.4 倍", fontsize=12, pad=8)
    # 右：最坏 seek
    b2 = ax2.bar(x, seek_worst, width=0.5, color=[C_BLUE, C_RED], alpha=0.88)
    for b, v in zip(b2, seek_worst):
        ax2.text(b.get_x() + b.get_width() / 2, b.get_height() + 0.25,
                 f"{v:.0f}s", ha="center", fontsize=10.5, fontweight="bold")
    ax2.set_ylim(0, 11.5)
    ax2.set_ylabel("最坏 seek 回退（秒）", fontsize=10.5)
    ax2.set_xticks(list(x))
    ax2.set_xticklabels(labels, fontsize=10.5)
    ax2.set_title("随机访问：MJPEG 回退为 0", fontsize=12, pad=8)
    for ax in (ax1, ax2):
        for s in ("top", "right"):
            ax.spines[s].set_visible(False)
    fig.suptitle("权衡的两极：压缩率 vs 随机访问（ffmpeg 实测）",
                 fontsize=13, y=1.02)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "mjpeg_extreme.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    chart_keyframe_timeline()
    chart_tradeoff()
    chart_mjpeg_extreme()
    print("charts generated:",
          [f for f in os.listdir(OUT) if f.endswith(".png")])
