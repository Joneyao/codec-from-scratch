#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""E4 Profile 与 Level 配图生成。

所有数据均来自 ffmpeg/ffprobe 实测与 ITU-T H.264 Annex A Table A-1：
  - measure_profiles.txt：三 profile 的 profile_idc、工具集、文件体积（ffmpeg 实测）
  - measure_levels.txt  ：Table A-1 各 Level 的 MaxMBPS/MaxFS/MaxBR（标准值 + 实测佐证）

复现：上述 .txt 由 README/文件内记录的 ffmpeg 命令生成；本脚本读数据绘图。
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
C_BLUE = "#2f6690"
C_RED = "#d1495b"
C_GREEN = "#4c9f70"
C_GRAY = "#b8b8b8"
C_ORANGE = "#e6a817"


# 图1: 三 profile 的工具集矩阵（有无）+ 同素材同 QP 体积对比（实测）
def chart_profile_tools():
    tools = ["CAVLC\n(基础熵编码)", "CABAC\n(算术熵编码)", "B 帧\n(双向预测)",
             "加权 P 预测", "8x8 变换"]
    profiles = ["Baseline\n(idc=66)", "Main\n(idc=77)", "High\n(idc=100)"]
    # 1=支持, 0=不支持（来自 x264 编码日志实测）
    # 行=profile, 列=tool
    matrix = [
        [1, 0, 0, 0, 0],   # baseline: cabac=0 8x8dct=0 bframes=0 weightp=0
        [1, 1, 1, 1, 0],   # main:     cabac=1 8x8dct=0 bframes=3 weightp=2
        [1, 1, 1, 1, 1],   # high:     cabac=1 8x8dct=1 bframes=3 weightp=2
    ]
    sizes = [428795, 383127, 376933]  # bytes, 720p QP=26 实测

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12.6, 4.8),
                                   gridspec_kw={"width_ratios": [1.55, 1]})

    # 左：工具矩阵
    for r in range(3):
        for c in range(5):
            on = matrix[r][c]
            ax1.add_patch(plt.Rectangle((c, 2 - r), 1, 1,
                          facecolor=(C_GREEN if on else "#f2f2f2"),
                          edgecolor="white", lw=2))
            ax1.text(c + 0.5, 2 - r + 0.5, "有" if on else "无",
                     ha="center", va="center",
                     color=("white" if on else "#999"),
                     fontsize=12, fontweight=("bold" if on else "normal"))
    ax1.set_xticks([c + 0.5 for c in range(5)])
    ax1.set_xticklabels(tools, fontsize=9.5)
    ax1.set_yticks([2 - r + 0.5 for r in range(3)])
    ax1.set_yticklabels(profiles, fontsize=10.5)
    ax1.set_xlim(0, 5)
    ax1.set_ylim(0, 3)
    ax1.set_title("Profile 管「用了哪些工具」（x264 日志实测）",
                  fontsize=12.5, pad=10)
    for s in ax1.spines.values():
        s.set_visible(False)
    ax1.tick_params(length=0)

    # 右：体积对比
    labels = ["Baseline", "Main", "High"]
    kb = [s / 1024 for s in sizes]
    colors = [C_ORANGE, C_BLUE, C_GREEN]
    bars = ax2.bar(labels, kb, color=colors, width=0.62)
    base = kb[0]
    for i, b in enumerate(bars):
        pct = (kb[i] - base) / base * 100
        tag = f"{kb[i]:.0f} KB"
        if i > 0:
            tag += f"\n{pct:+.1f}%"
        ax2.text(b.get_x() + b.get_width() / 2, b.get_height() + 3,
                 tag, ha="center", fontsize=10, fontweight="bold")
    ax2.set_ylabel("文件大小（KB，越小越好）", fontsize=10)
    ax2.set_ylim(0, max(kb) * 1.18)
    ax2.set_title("同素材同 QP：工具越强，压得越小", fontsize=12.5, pad=10)
    for s in ("top", "right"):
        ax2.spines[s].set_visible(False)

    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "profile_tools.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)


# 图2: Level 的分辨率-码率上限阶梯图（Table A-1 关键档位）
def chart_level_ladder():
    # (level, 典型最大画面, MaxMBPS, MaxFS宏块, MaxBR kbps base)
    levels = ["3.0", "3.1", "4.0", "4.1", "5.0", "5.1"]
    maxfs = [1620, 3600, 8192, 8192, 22080, 36864]          # 宏块数
    maxbr = [10000, 14000, 20000, 50000, 135000, 240000]    # kbps
    # 每帧最大像素（MaxFS x 256）转成「百万像素」，便于直觉
    mpix = [f * 256 / 1e6 for f in maxfs]
    res_note = ["720x576", "1280x720", "1920x1080", "1080p高码率",
                "2560x1600", "4096x2304"]

    fig, ax1 = plt.subplots(figsize=(10.2, 4.9))
    x = range(len(levels))

    # 柱：每帧最大像素（画面大小上限）
    bars = ax1.bar(x, mpix, width=0.56, color=C_BLUE, alpha=0.85,
                   label="每帧最大像素（MaxFS，画面上限）")
    for i, b in enumerate(bars):
        ax1.text(b.get_x() + b.get_width() / 2, b.get_height() + 0.18,
                 f"{mpix[i]:.1f}M · {res_note[i]}", ha="center",
                 fontsize=8.4, color=C_BLUE)
    ax1.set_ylabel("每帧最大像素（百万）", fontsize=10, color=C_BLUE)
    ax1.tick_params(axis="y", labelcolor=C_BLUE)
    ax1.set_ylim(0, max(mpix) * 1.28)
    ax1.set_xticks(list(x))
    ax1.set_xticklabels([f"Level {l}" for l in levels], fontsize=10)

    # 折线：最大码率（吞吐上限）
    ax2 = ax1.twinx()
    ax2.plot(x, maxbr, "-o", color=C_RED, lw=2.2, markersize=7,
             label="最大码率 MaxBR（kbps）")
    for i, v in enumerate(maxbr):
        # 码率标签放在点下方偏右，避开柱子的分辨率标注
        ax2.annotate(f"{v/1000:.0f}M", (i, v), xytext=(11, -14),
                     textcoords="offset points", ha="left",
                     fontsize=9, color=C_RED, fontweight="bold")
    ax2.set_ylabel("最大码率（kbps，base）", fontsize=10, color=C_RED)
    ax2.tick_params(axis="y", labelcolor=C_RED)
    ax2.set_ylim(0, max(maxbr) * 1.25)

    ax1.set_title("Level 管「画面多大、码率多高」（ITU-T H.264 Table A-1）",
                  fontsize=12.5, pad=12)
    for s in ("top",):
        ax1.spines[s].set_visible(False)
        ax2.spines[s].set_visible(False)
    l1, lab1 = ax1.get_legend_handles_labels()
    l2, lab2 = ax2.get_legend_handles_labels()
    ax1.legend(l1 + l2, lab1 + lab2, fontsize=9, loc="upper left")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "level_ladder.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    chart_profile_tools()
    chart_level_ladder()
    print("charts generated:",
          sorted(f for f in os.listdir(OUT) if f.endswith(".png")))
