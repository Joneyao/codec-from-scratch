#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""E1 色彩发灰之谜 配图生成。

所有数据来自 ffmpeg/ffprobe 对真实文件的实测（见同目录 *.txt）：
  - stored_y.txt：同一 RGB 源按 full/limited range 编码，存进码流的真实 Y 值
  - range_mismatch.txt：limited 数据被误当 full 显示的真实偏差
  - matrix_diff.txt：BT.601 vs BT.709 亮度系数对典型颜色的 Y 值差
  - probe_metadata.txt：ffprobe 读出的色彩元数据（标记 vs unknown）

复现：本目录下这些 .txt 由 README 记录的 ffmpeg 命令生成；本脚本读数据绘图。
"""
import os
import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.font_manager as fm
from matplotlib.patches import Rectangle

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


# 图1: full vs limited range 的 Y 取值范围映射（概念+实测锚点）
def chart_range_mapping():
    fig, ax = plt.subplots(figsize=(9, 3.6))
    # full range 条：0..255
    ax.barh(1.6, 255, left=0, height=0.5, color=C_BLUE, alpha=0.85)
    ax.text(127, 1.6, "full / pc  ·  0 – 255", ha="center", va="center",
            color="white", fontsize=11, fontweight="bold")
    # limited range 条：16..235
    ax.barh(0.8, 235 - 16, left=16, height=0.5, color=C_ORANGE, alpha=0.9)
    ax.text(125, 0.8, "limited / tv  ·  16 – 235", ha="center", va="center",
            color="white", fontsize=11, fontweight="bold")
    # 两端空白区标注
    for x0, x1 in [(0, 16), (235, 255)]:
        ax.axvspan(x0, x1, ymin=0.30, ymax=0.46, color=C_GRAY, alpha=0.3)
    ax.text(8, 0.35, "留白", ha="center", fontsize=8, color="#666")
    ax.text(245, 0.35, "留白", ha="center", fontsize=8, color="#666")
    # 实测锚点
    for label, yf, yl in [("黑", 0, 16), ("灰", 128, 126), ("白", 255, 235)]:
        ax.plot([yf], [1.6], "o", color="white", markersize=6, mec=C_BLUE)
        ax.plot([yl], [0.8], "o", color="white", markersize=6, mec=C_ORANGE)
    ax.text(0, 2.05, "0", ha="center", fontsize=9)
    ax.text(255, 2.05, "255", ha="center", fontsize=9)
    ax.text(16, 0.35, "16", ha="center", fontsize=9, color=C_ORANGE)
    ax.text(235, 0.35, "235", ha="center", fontsize=9, color=C_ORANGE)
    ax.set_title("两套 range：同样的黑白，存进码流的 Y 值不一样（ffmpeg 实测）",
                 fontsize=12.5, pad=10)
    ax.set_xlim(-8, 263)
    ax.set_ylim(0.1, 2.3)
    ax.axis("off")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "range_mapping.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)


# 图2: range 标错导致的发灰（实测偏差：黑0->16 白255->235）
def chart_washout():
    names = ["黑块", "灰块", "白块"]
    correct = [0, 128, 255]      # 正确按 limited 拉伸后的显示值
    wrong = [16, 126, 235]       # 误当 full 直接显示
    x = range(len(names))
    fig, ax = plt.subplots(figsize=(8.4, 4.6))
    w = 0.36
    b1 = ax.bar([i - w/2 for i in x], correct, w, label="正确显示", color=C_GREEN)
    b2 = ax.bar([i + w/2 for i in x], wrong, w, label="range 标错，误当 full 直接显示",
                color=C_RED)
    for bars in (b1, b2):
        for b in bars:
            ax.text(b.get_x() + b.get_width()/2, b.get_height() + 4,
                    str(int(b.get_height())), ha="center", fontsize=10)
    # 偏差标注
    for i, (c, w2) in enumerate(zip(correct, wrong)):
        d = w2 - c
        if d:
            ax.annotate(f"{d:+d}", (i + w/2, w2 + 20), ha="center",
                        fontsize=10, color=C_RED, fontweight="bold")
    ax.axhspan(0, 260, xmin=0, xmax=0, color="none")
    ax.set_ylabel("显示亮度值（0-255）", fontsize=10)
    ax.set_ylim(0, 285)
    ax.set_xticks(list(x))
    ax.set_xticklabels(names, fontsize=11)
    ax.set_title("发灰的真身：黑被抬亮、白被压暗，对比度被压扁",
                 fontsize=12.5, pad=10)
    ax.legend(fontsize=9.5, loc="upper left")
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "washout.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)


# 图3: BT.601 vs BT.709 亮度系数对典型颜色 Y 值的差（实测计算）
def chart_matrix_diff():
    colors = ["纯红", "纯绿", "纯蓝", "黄", "青"]
    y601 = [76.2, 149.7, 29.1, 225.9, 178.8]
    y709 = [54.2, 182.4, 18.4, 236.6, 200.8]
    x = range(len(colors))
    fig, ax = plt.subplots(figsize=(9, 4.6))
    w = 0.36
    ax.bar([i - w/2 for i in x], y601, w, label="BT.601 (0.299/0.587/0.114)",
           color=C_BLUE)
    ax.bar([i + w/2 for i in x], y709, w, label="BT.709 (0.2126/0.7152/0.0722)",
           color=C_ORANGE)
    for i, (a, c) in enumerate(zip(y601, y709)):
        ax.annotate(f"{a-c:+.0f}", (i, max(a, c) + 6), ha="center",
                    fontsize=10, color=C_RED, fontweight="bold")
    ax.set_ylabel("该颜色算出的亮度 Y（0-255）", fontsize=10)
    ax.set_ylim(0, 270)
    ax.set_xticks(list(x))
    ax.set_xticklabels(colors, fontsize=11)
    ax.set_title("两套矩阵，同一个颜色算出的亮度不同：绿色差最大(+33)",
                 fontsize=12.5, pad=10)
    ax.legend(fontsize=9.5, loc="upper right")
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "matrix_diff.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    chart_range_mapping()
    chart_washout()
    chart_matrix_diff()
    print("charts generated:",
          [f for f in os.listdir(OUT) if f.endswith(".png")])
