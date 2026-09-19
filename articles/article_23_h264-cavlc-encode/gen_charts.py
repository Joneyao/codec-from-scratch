#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""D3 手搓 H.264 编码器 #03（CAVLC 编码）配图生成。

所有数据来自仓库真实运行：
  third-party/codec-from-scratch/h264/encoder/  的 CAVLC 正向编码器
  对一个真实 4x4 量化块编码后的比特统计（chart_data/cavlc_encode.txt）。

真实数据（编码→本仓库 decoder 解码 往返一致，roundtrip_ok=1）：
  块（zigzag 扫描顺序）: 13 -3 -4 2 1 2 0 -1 1 0 0 0 0 0 0 0
  nc=2, total_coeff=8, trailing_ones=2, total_zeros=1
  五个语法元素比特数：coeff_token=11, sign=2, level=24,
                      total_zeros=4, run_before=2, 总计=43 bit
  压缩：16 系数按 32bit 裸存=512, 按 9bit 裸存=144, CAVLC=43
       压缩比 vs 32bit=11.9x, vs 9bit=3.35x
"""
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.font_manager as fm

# --- CJK 字体（项目共享模块，向上查找 backend/）---
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
    fm._load_fontmanager(try_read_cache=False)
    plt.rcParams["font.family"] = "Noto Sans CJK JP"
plt.rcParams["axes.unicode_minus"] = False

OUT = os.path.dirname(os.path.abspath(__file__))

# 配色
C_BLUE = "#2f6690"
C_GREEN = "#4c9f70"
C_RED = "#d1495b"
C_YELLOW = "#e6a817"
C_PURPLE = "#8e6fb3"
C_GRAY = "#b8b8b8"


# ---------------------------------------------------------------------------
# 图0: 进度地图（复用系列共享骨架，高亮"熵编码 CAVLC"环节）
# ---------------------------------------------------------------------------
def chart_stage_map():
    sys.path.insert(0, os.path.join(OUT, ".."))
    from pipeline_map import draw_h264_encode_map
    draw_h264_encode_map("entropy", os.path.join(OUT, "stage_map.png"))


# ---------------------------------------------------------------------------
# 图1: 五个语法元素的比特分解（精确层，真实比特数 11/2/24/4/2）
# ---------------------------------------------------------------------------
def chart_bit_breakdown():
    labels = ["coeff_token\n(选表+计数)", "尾1符号\n(sign)", "level\n(幅值)",
              "total_zeros\n(0总数)", "run_before\n(0分布)"]
    bits = [11, 2, 24, 4, 2]
    colors = [C_BLUE, C_YELLOW, C_RED, C_GREEN, C_PURPLE]
    total = sum(bits)

    fig, ax = plt.subplots(figsize=(9, 4.6))
    bars = ax.bar(labels, bits, color=colors, edgecolor="white", width=0.62)
    for b, v in zip(bars, bits):
        pct = v / total * 100
        ax.text(b.get_x() + b.get_width() / 2, v + 0.5,
                f"{v} bit\n{pct:.0f}%", ha="center", va="bottom",
                fontsize=10, fontweight="bold", color="#333")
    ax.axhline(total, color="#888", ls="--", lw=1)
    ax.text(4.4, total + 0.3, f"整块共 {total} bit", ha="right",
            va="bottom", fontsize=10.5, color="#555", fontweight="bold")
    ax.set_ylabel("比特数", fontsize=11)
    ax.set_ylim(0, total + 5)
    ax.set_title("一个 4×4 块 43 比特花在哪：level 幅值独占一半以上",
                 fontsize=12.5, pad=12)
    for spine in ("top", "right"):
        ax.spines[spine].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "bit_breakdown.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------------------
# 图2: 压缩效果对比（精确层，512bit 裸存 vs 144bit vs 43bit CAVLC）
# ---------------------------------------------------------------------------
def chart_compress():
    labels = ["16 系数\n32 位裸存", "16 系数\n9 位裸存", "CAVLC 编码"]
    bits = [512, 144, 43]
    colors = [C_GRAY, C_YELLOW, C_GREEN]

    fig, ax = plt.subplots(figsize=(8.4, 4.8))
    bars = ax.bar(labels, bits, color=colors, edgecolor="white", width=0.55)
    for b, v in zip(bars, bits):
        ax.text(b.get_x() + b.get_width() / 2, v + 8,
                f"{v} bit", ha="center", va="bottom",
                fontsize=12, fontweight="bold", color="#333")
    # 压缩比标注
    ax.annotate("", xy=(2, 43), xytext=(0, 512),
                arrowprops=dict(arrowstyle="->", color=C_RED, lw=1.8,
                                connectionstyle="arc3,rad=-0.25"))
    ax.text(1.05, 340, "压缩 11.9×", color=C_RED, fontsize=12,
            fontweight="bold", ha="center")
    ax.text(2.0, 120, "vs 9bit 裸存\n再压 3.35×", color="#a07800",
            fontsize=9.5, ha="center")
    ax.set_ylabel("比特数", fontsize=11)
    ax.set_ylim(0, 570)
    ax.set_title("同一个 4×4 块：裸存 512 比特，CAVLC 只要 43 比特",
                 fontsize=12.5, pad=12)
    for spine in ("top", "right"):
        ax.spines[spine].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "compress.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    chart_stage_map()
    chart_bit_breakdown()
    chart_compress()
    print("charts generated:",
          [f for f in os.listdir(OUT) if f.endswith(".png")])
