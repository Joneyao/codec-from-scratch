#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""E5 B帧/PTS/DTS 配图生成。

所有数据来自 ffmpeg/ffprobe 对真实文件的实测（见同目录数据文件）：
  - bframe_frames.csv   ：带 B 帧（-bf 3）视频，逐帧 pict_type/pts/dts（解码顺序）
  - nobframe_frames.csv ：无 B 帧（-bf 0）视频，逐帧 pict_type/pts/dts
  - decode_vs_display_sample.txt ：前 8 帧解码序 vs 显示序对照
  - has_b_frames.txt    ：ffprobe stream=has_b_frames 等元数据

复现：数据由 has_b_frames.txt 记录的 ffmpeg 命令生成；本脚本读 csv 绘图。
时间基 1/15360，30fps 下 1 帧 = 512 tick，图中 PTS/DTS 均已换算为 tick。
"""
import os
import sys
import csv
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


def load(name):
    rows = []
    with open(os.path.join(OUT, name)) as f:
        for r in csv.DictReader(f):
            rows.append(r)
    return rows


# 图1: 带 B 帧 vs 无 B 帧，PTS/DTS 随解码顺序的曲线（实测）
def chart_pts_dts():
    bf = load("bframe_frames.csv")
    nb = load("nobframe_frames.csv")
    N = 16  # 取前 16 帧看清楚
    bf = bf[:N]
    nb = nb[:N]

    x = list(range(N))
    bf_pts = [int(r["pts"]) for r in bf]
    bf_dts = [int(r["dts"]) for r in bf]
    nb_pts = [int(r["pts"]) for r in nb]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12.4, 5.0), sharey=True)

    # 左：带 B 帧
    ax1.plot(x, bf_dts, "-o", color=C_BLUE, markersize=5, label="DTS 解码顺序（单调递增）")
    ax1.plot(x, bf_pts, "-s", color=C_RED, markersize=5, label="PTS 显示顺序（跳来跳去）")
    # 标出 B 帧点
    for i, r in enumerate(bf):
        if r["pict_type"] == "B":
            ax1.annotate("B", (i, bf_pts[i]), textcoords="offset points",
                         xytext=(0, 8), ha="center", fontsize=8, color=C_RED)
    ax1.axhline(0, color=C_GRAY, lw=0.8, ls="--")
    ax1.annotate("首帧 DTS = -1024\n(时间戳为负)", (0, bf_dts[0]),
                 textcoords="offset points", xytext=(28, -6), fontsize=9,
                 color=C_BLUE, arrowprops=dict(arrowstyle="->", color=C_BLUE))
    ax1.set_title("带 B 帧（-bf 3）：PTS 与 DTS 错位", fontsize=12.5, pad=8)
    ax1.set_xlabel("解码顺序（第几个被解码）", fontsize=10)
    ax1.set_ylabel("时间戳（tick，1/15360 秒）", fontsize=10)
    ax1.legend(fontsize=9, loc="upper left")
    for s in ("top", "right"):
        ax1.spines[s].set_visible(False)

    # 右：无 B 帧
    ax2.plot(x, nb_pts, "-o", color=C_GREEN, markersize=5,
             label="PTS == DTS（完全重合）")
    ax2.set_title("无 B 帧（-bf 0）：PTS 与 DTS 完全重合", fontsize=12.5, pad=8)
    ax2.set_xlabel("解码顺序（第几个被解码）", fontsize=10)
    ax2.legend(fontsize=9, loc="upper left")
    for s in ("top", "right"):
        ax2.spines[s].set_visible(False)

    fig.suptitle("有 B 帧就必须 DTS≠PTS：解码顺序单调、显示顺序错开（ffmpeg 实测）",
                 fontsize=13, y=1.0)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "pts_dts_scatter.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)


# 图2: IBBP 前 8 帧，解码顺序 vs 显示顺序 两行对照（实测 pict_type）
def chart_reorder():
    bf = load("bframe_frames.csv")[:8]
    # 解码顺序：as-is
    dec = [(int(r["decode_order"]), r["pict_type"], int(r["pts"])) for r in bf]
    # 显示顺序：按 pts 排序
    disp = sorted(dec, key=lambda t: t[2])
    # 每帧在解码序里的位置（用 pts 做键）
    pts_to_decidx = {pts: d for (d, t, pts) in dec}

    color = {"I": C_ORANGE, "P": C_BLUE, "B": C_RED}
    fig, ax = plt.subplots(figsize=(11.5, 4.6))

    box_w, box_h = 0.86, 0.7
    y_dec, y_disp = 2.1, 0.5

    # 解码顺序行
    for i, (d, t, pts) in enumerate(dec):
        ax.add_patch(plt.Rectangle((i, y_dec), box_w, box_h,
                     facecolor=color[t], edgecolor="white", alpha=0.9))
        ax.text(i + box_w / 2, y_dec + box_h / 2, t, ha="center", va="center",
                color="white", fontsize=15, fontweight="bold")
        ax.text(i + box_w / 2, y_dec - 0.16, f"PTS={pts}", ha="center",
                fontsize=7.5, color="#444")

    # 显示顺序行
    for j, (d, t, pts) in enumerate(disp):
        ax.add_patch(plt.Rectangle((j, y_disp), box_w, box_h,
                     facecolor=color[t], edgecolor="white", alpha=0.9))
        ax.text(j + box_w / 2, y_disp + box_h / 2, t, ha="center", va="center",
                color="white", fontsize=15, fontweight="bold")
        ax.text(j + box_w / 2, y_disp - 0.16, f"PTS={pts}", ha="center",
                fontsize=7.5, color="#444")

    # 连线：同一帧从解码位置连到显示位置，被重排的画红线
    for i, (d, t, pts) in enumerate(dec):
        j = [k for k, (dd, tt, pp) in enumerate(disp) if pp == pts][0]
        moved = (i != j)
        ax.annotate("", xy=(j + box_w / 2, y_disp + box_h),
                    xytext=(i + box_w / 2, y_dec),
                    arrowprops=dict(arrowstyle="-", lw=1.6 if moved else 0.7,
                                    color=C_RED if moved else C_GRAY,
                                    alpha=0.85 if moved else 0.5))

    ax.text(-0.55, y_dec + box_h / 2, "解码顺序\n(DTS 序)", ha="right",
            va="center", fontsize=10.5, fontweight="bold")
    ax.text(-0.55, y_disp + box_h / 2, "显示顺序\n(PTS 序)", ha="right",
            va="center", fontsize=10.5, fontweight="bold")

    # 图例
    for k, (name, c) in enumerate([("I 帧", C_ORANGE), ("P 帧", C_BLUE), ("B 帧", C_RED)]):
        ax.add_patch(plt.Rectangle((5.2 + k * 1.05, 3.35), 0.3, 0.28,
                     facecolor=c, edgecolor="none"))
        ax.text(5.55 + k * 1.05, 3.49, name, va="center", fontsize=9)

    ax.set_title("同一段 IBBP，解码顺序 ≠ 显示顺序：红线标出被重排的帧（实测）",
                 fontsize=12.5, pad=12)
    ax.set_xlim(-2.0, 8.2)
    ax.set_ylim(0.0, 3.8)
    ax.axis("off")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "reorder_ibbp.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    chart_pts_dts()
    chart_reorder()
    print("charts generated:",
          [f for f in os.listdir(OUT) if f.endswith(".png")])
