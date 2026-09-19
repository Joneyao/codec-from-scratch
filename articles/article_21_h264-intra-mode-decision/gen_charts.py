#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""D1 手搓 H.264 编码器 #01 配图生成。

所有精确层数据来自仓库真实运行：
  third-party/codec-from-scratch/h264/encoder/chart_data/intra_mode_sad.txt
  测试图片 samples/test_pattern.ppm (256x256)。

真实数据：
  - Intra_4x4 块(41,9) 9 种模式 SAD：
      V=18 H=1038 DC=970 DDL=1105 DDR=1139 VR=1099 HD=1038 VL=963 HU=1006
      => 选中 Vertical(SAD=18)，SATD 也选 Vertical(72)
      best_vs_second: 最优 V=18 vs 次优 VL=963, gap=945
  - Intra_16x16 块(128,128) 4 种模式 SAD：
      V=0 H=49920 DC=25088 Plane=24480 => 选中 Vertical(SAD=0)
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
C_BEST = "#d1495b"    # 红：最优（被选中）
C_BAR = "#2f6690"     # 蓝：其余候选
C_GRAY = "#b8b8b8"    # 灰
C_HL = "#e6a817"      # 黄：标注


# ---------------------------------------------------------------------------
# 图0: 进度地图（复用系列共享骨架，D1 高亮"预测决策"环节）
# ---------------------------------------------------------------------------
def chart_stage_map():
    sys.path.insert(0, os.path.join(OUT, ".."))
    from pipeline_map import draw_h264_encode_map
    draw_h264_encode_map("predict", os.path.join(OUT, "stage_map.png"))


# ---------------------------------------------------------------------------
# 图1: Intra_4x4 块(41,9) 9 种模式 SAD 柱状图（精确层，真实数据）
#      高亮最小的 Vertical，标出与次优的 gap
# ---------------------------------------------------------------------------
def chart_intra4x4_sad():
    # 顺序按 spec 模式号 0..8
    names = ["Vertical\n(垂直)", "Horizontal\n(水平)", "DC", "Diag-Down-Left",
             "Diag-Down-Right", "Vertical-Right", "Horizontal-Down",
             "Vertical-Left", "Horizontal-Up"]
    sad = [18, 1038, 970, 1105, 1139, 1099, 1038, 963, 1006]
    best_idx = sad.index(min(sad))                 # Vertical=18
    second = sorted(sad)[1]                         # 963 = Vertical-Left
    second_idx = sad.index(second)

    colors = [C_BAR] * len(sad)
    colors[best_idx] = C_BEST
    colors[second_idx] = C_HL

    fig, ax = plt.subplots(figsize=(10, 4.8))
    bars = ax.bar(range(len(sad)), sad, color=colors, edgecolor="white",
                  width=0.72)
    for i, v in enumerate(sad):
        ax.text(i, v + 15, str(v), ha="center", va="bottom", fontsize=9,
                color="#333", fontweight="bold" if i == best_idx else "normal")

    # 标注最优
    ax.annotate("编码器选中：Vertical\nSAD=18（残差几乎为零）",
                xy=(best_idx, 18), xytext=(best_idx + 1.4, 620),
                fontsize=10, color=C_BEST, fontweight="bold",
                arrowprops=dict(arrowstyle="->", color=C_BEST, lw=1.6))
    # 标注 gap
    ax.annotate("", xy=(second_idx, second), xytext=(second_idx, 18),
                arrowprops=dict(arrowstyle="<->", color="#555", lw=1.4))
    ax.text(second_idx + 0.15, (second + 18) / 2,
            f"与次优的差距\ngap = {second - 18}", ha="left", va="center",
            fontsize=9.5, color="#555")

    ax.set_xticks(range(len(sad)))
    ax.set_xticklabels(names, fontsize=8.5, rotation=0)
    ax.set_ylabel("SAD（越小越好）", fontsize=11)
    ax.set_title("Intra_4x4 块(41,9)：试遍 9 种模式，Vertical 一枝独秀",
                 fontsize=13, pad=12)
    ax.set_ylim(0, 1260)
    for spine in ("top", "right"):
        ax.spines[spine].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "intra4x4_sad.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------------------
# 图2: Intra_16x16 块(128,128) 4 种模式 SAD 对比（Vertical=0 一枝独秀）
# ---------------------------------------------------------------------------
def chart_intra16x16_sad():
    names = ["Vertical\n(垂直)", "Horizontal\n(水平)", "DC", "Plane\n(平面)"]
    sad = [0, 49920, 25088, 24480]
    best_idx = 0
    colors = [C_BEST if i == best_idx else C_BAR for i in range(len(sad))]

    fig, ax = plt.subplots(figsize=(8, 4.6))
    ax.bar(range(len(sad)), sad, color=colors, edgecolor="white", width=0.6)
    for i, v in enumerate(sad):
        ax.text(i, v + 700, str(v), ha="center", va="bottom", fontsize=10,
                color="#333", fontweight="bold" if i == best_idx else "normal")

    ax.annotate("编码器选中：Vertical\nSAD=0（列方向完全一致）",
                xy=(0, 0), xytext=(0.7, 30000),
                fontsize=10.5, color=C_BEST, fontweight="bold",
                arrowprops=dict(arrowstyle="->", color=C_BEST, lw=1.8))

    ax.set_xticks(range(len(sad)))
    ax.set_xticklabels(names, fontsize=10)
    ax.set_ylabel("SAD（越小越好）", fontsize=11)
    ax.set_title("Intra_16x16 块(128,128)：4 种模式里 Vertical 直接归零",
                 fontsize=13, pad=12)
    ax.set_ylim(0, 56000)
    for spine in ("top", "right"):
        ax.spines[spine].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "intra16x16_sad.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    chart_stage_map()
    chart_intra4x4_sad()
    chart_intra16x16_sad()
    print("charts generated:",
          [f for f in os.listdir(OUT) if f.endswith(".png")])
