#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""D4 手搓 H.264 编码器 #04（运动估计）配图生成。

所有数据来自仓库真实运行：
  third-party/codec-from-scratch/h264/encoder/motion_search.{h,cpp}
  在 samples/test_pattern.ppm（256x256，模拟位移 MV=(5,-3)+轻噪声）上，
  对 49 个 16x16 块分别跑全搜索(FullSearch)与菱形搜索(DiamondSearch)，
  统计 points_checked 与 SAD。原始逐块数据见：
  h264/encoder/chart_data/motion_search.txt

关键汇总（真实数字）：
  blocks 49
  全搜索 avg 点数 1089.000    菱形 avg 点数 20.653   speedup 52.728x
  全搜索 avg SAD 437.673      菱形 avg SAD 657.714   sad_ratio 1.5028
  菱形命中全搜索同一 MV：38/49 = 78%
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

C_FULL = "#2f6690"    # 蓝：全搜索
C_DIA = "#e6a817"     # 黄：菱形
C_OK = "#4c9f70"      # 绿：命中
C_BAD = "#d1495b"     # 红：局部极小
C_GRAY = "#b8b8b8"


# --- 真实逐块数据（从 chart_data/motion_search.txt 摘录，method block points sad）---
# 全搜索每块固定 1089 点。下面按 block_id 存 (full_sad, dia_points, dia_sad)。
BLOCKS = {
    0: (424, 30, 424), 1: (416, 26, 591), 2: (432, 26, 432), 3: (451, 13, 451),
    4: (427, 30, 427), 5: (445, 13, 1635), 6: (426, 26, 426), 7: (419, 13, 419),
    8: (440, 13, 1590), 9: (405, 31, 405), 10: (430, 26, 602), 11: (438, 13, 438),
    12: (446, 30, 446), 13: (427, 26, 427), 14: (454, 13, 454), 15: (443, 13, 1589),
    16: (455, 26, 455), 17: (418, 13, 418), 18: (449, 26, 1731), 19: (461, 26, 1494),
    20: (480, 26, 480), 21: (455, 13, 455), 22: (447, 13, 1597), 23: (443, 26, 443),
    24: (438, 13, 438), 25: (418, 13, 418), 26: (427, 13, 1629), 27: (440, 31, 440),
    28: (436, 13, 436), 29: (425, 13, 1569), 30: (436, 26, 436), 31: (436, 13, 436),
    32: (436, 13, 436), 33: (445, 30, 445), 34: (462, 31, 462), 35: (429, 13, 429),
    36: (473, 26, 473), 37: (438, 26, 438), 38: (475, 13, 475), 39: (434, 13, 434),
    40: (436, 30, 436), 41: (442, 31, 442), 42: (419, 13, 419), 43: (448, 13, 1586),
    44: (450, 26, 450), 45: (406, 13, 406), 46: (417, 13, 417), 47: (416, 31, 416),
    48: (433, 31, 433),
}
FULL_POINTS = 1089
FULL_AVG_PTS, DIA_AVG_PTS = 1089.0, 20.653
FULL_AVG_SAD, DIA_AVG_SAD = 437.673, 657.714
SPEEDUP, SAD_RATIO = 52.728, 1.5028
MV_HIT, MV_TOTAL = 38, 49


# ---------------------------------------------------------------------------
# 图0: 进度地图（复用系列骨架，D4 高亮"预测决策"环节 = 运动估计）
# ---------------------------------------------------------------------------
def chart_stage_map():
    sys.path.insert(0, os.path.join(OUT, ".."))
    from pipeline_map import draw_h264_encode_map
    draw_h264_encode_map("predict", os.path.join(OUT, "stage_map.png"))


# ---------------------------------------------------------------------------
# 图1: 搜索点数对比——全搜索 1089 vs 菱形 20.65（log 轴，看数量级差距）
# ---------------------------------------------------------------------------
def chart_points_compare():
    fig, ax = plt.subplots(figsize=(7.6, 4.6))
    labels = ["全搜索\nFullSearch", "菱形搜索\nDiamondSearch"]
    vals = [FULL_AVG_PTS, DIA_AVG_PTS]
    colors = [C_FULL, C_DIA]
    bars = ax.bar(labels, vals, color=colors, width=0.55, zorder=3)
    ax.set_yscale("log")
    ax.set_ylabel("平均每块检查的候选点数（对数轴）", fontsize=10.5)
    ax.set_ylim(8, 3000)
    for b, v, c in zip(bars, vals, colors):
        ax.text(b.get_x() + b.get_width() / 2, v * 1.15,
                f"{v:.1f} 点", ha="center", va="bottom",
                fontsize=12, fontweight="bold", color=c)
    # 提速倍数标注
    ax.annotate(f"点数省到 1/53\n提速约 {SPEEDUP:.0f}×",
                xy=(1, DIA_AVG_PTS), xytext=(0.5, 250),
                ha="center", fontsize=11, color=C_BAD, fontweight="bold",
                arrowprops=dict(arrowstyle="->", color=C_BAD, lw=1.6))
    ax.set_title("同一张图 49 个块：菱形搜索把算力砍掉 98%",
                 fontsize=12.5, pad=12)
    ax.grid(axis="y", ls="--", alpha=0.4, zorder=0)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "points_compare.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------------------
# 图2: 速度-质量权衡——49 块逐块散点（x=点数 log, y=SAD）
# 全搜索一列贴左上（点多、SAD 低且集中），菱形铺右侧（点少、SAD 有高有低）
# ---------------------------------------------------------------------------
def chart_tradeoff_scatter():
    full_pts = [FULL_POINTS] * len(BLOCKS)
    full_sad = [BLOCKS[i][0] for i in sorted(BLOCKS)]
    dia_pts = [BLOCKS[i][1] for i in sorted(BLOCKS)]
    dia_sad = [BLOCKS[i][2] for i in sorted(BLOCKS)]

    fig, ax = plt.subplots(figsize=(8.2, 5.0))
    ax.scatter(full_pts, full_sad, s=55, color=C_FULL, alpha=0.75,
               edgecolor="white", linewidth=0.5, label="全搜索（每块 1089 点）",
               zorder=3)
    ax.scatter(dia_pts, dia_sad, s=55, color=C_DIA, alpha=0.85,
               edgecolor="white", linewidth=0.5, label="菱形搜索（13~31 点）",
               zorder=3)
    ax.set_xscale("log")
    ax.set_xlabel("每块检查的候选点数（对数轴，越左越省算力）", fontsize=10.5)
    ax.set_ylabel("找到 MV 的 SAD 残差（越低越准）", fontsize=10.5)
    # 均线
    ax.axhline(FULL_AVG_SAD, color=C_FULL, ls="--", lw=1.2, alpha=0.7)
    ax.axhline(DIA_AVG_SAD, color=C_DIA, ls="--", lw=1.2, alpha=0.7)
    ax.text(1150, FULL_AVG_SAD - 60, f"全搜索均值 {FULL_AVG_SAD:.0f}",
            color=C_FULL, fontsize=9)
    ax.text(32, DIA_AVG_SAD + 40, f"菱形均值 {DIA_AVG_SAD:.0f}",
            color=C_DIA, fontsize=9)
    # 标注困在局部的几个高 SAD 点
    ax.annotate("菱形困在局部极小\n（11 个块，SAD 冲到 1500+）",
                xy=(13, 1635), xytext=(45, 1450),
                ha="left", fontsize=9.5, color=C_BAD,
                arrowprops=dict(arrowstyle="->", color=C_BAD, lw=1.4))
    ax.legend(loc="center right", fontsize=10, frameon=True)
    ax.set_title("速度换质量：菱形挪到左边（省算力），代价是少数块 SAD 飙高",
                 fontsize=12, pad=12)
    ax.grid(True, ls="--", alpha=0.3, zorder=0)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "tradeoff_scatter.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------------------
# 图3: 菱形命中率——38/49 找到与全搜索相同 MV，11 个落入局部极小
# ---------------------------------------------------------------------------
def chart_hit_rate():
    hit = MV_HIT
    miss = MV_TOTAL - MV_HIT
    fig, ax = plt.subplots(figsize=(6.8, 4.4))
    wedges, _, autotexts = ax.pie(
        [hit, miss], colors=[C_OK, C_BAD],
        autopct=lambda p: f"{p:.0f}%\n({round(p / 100 * MV_TOTAL)} 块)",
        startangle=90, counterclock=False,
        wedgeprops=dict(width=0.42, edgecolor="white"),
        textprops=dict(fontsize=11, color="white", fontweight="bold"))
    ax.legend(wedges,
              ["找到与全搜索相同的最优 MV", "落入局部极小（找错了）"],
              loc="center", bbox_to_anchor=(0.5, -0.08), fontsize=10.5,
              frameon=False)
    ax.text(0, 0, f"{MV_HIT}/{MV_TOTAL}\n命中", ha="center", va="center",
            fontsize=14, fontweight="bold", color="#333")
    ax.set_title("菱形搜索的真实代价：78% 命中，22% 找错",
                 fontsize=12.5, pad=14)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "hit_rate.png"), dpi=150,
                bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    chart_stage_map()
    chart_points_compare()
    chart_tradeoff_scatter()
    chart_hit_rate()
    print("charts generated:",
          sorted(f for f in os.listdir(OUT) if f.endswith(".png")))
