#!/usr/bin/env python3
"""C3 H.264 slice header + 宏块网格划分配图。

数据来源全部为 codec-from-scratch/h264/decoder 真实跑出的结果：
  - 宏块网格与类型分布：macroblock_demo 写出的 chart_data/mb_dump.txt
不手编数字。
"""
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.font_manager as fm
fm._load_fontmanager(try_read_cache=False)
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch

plt.rcParams["font.family"] = "Noto Sans CJK JP"
plt.rcParams["axes.unicode_minus"] = False

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(
    HERE, "../../../../../third-party/codec-from-scratch"))
DEC = os.path.join(REPO, "h264/decoder")
DUMP = os.path.join(DEC, "chart_data/mb_dump.txt")

C_BLUE = "#2563eb"
C_CYAN = "#0891b2"
C_RED = "#dc2626"
C_ORANGE = "#ea580c"
C_GREEN = "#16a34a"
C_PURPLE = "#7c3aed"
C_GRAY = "#64748b"
C_DARK = "#1e293b"


def load_dump():
    d = {}
    with open(DUMP) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            p = line.split()
            d[p[0]] = p[1]
    return d


def sync_stage_map():
    import sys
    sys.path.insert(0, os.path.join(HERE, ".."))
    from pipeline_map import draw_h264_decode_map
    draw_h264_decode_map("slice", os.path.join(HERE, "stage_map.png"))
    print("stage_map.png done")


def chart_mb_grid():
    """精确层：真实一帧的 11x9 宏块网格，每格按类型着色 + 标 mbAddr。
    本帧是 IDR，全部为 Intra 宏块（第一个是 I_4x4，其余按 IDR 全帧内语义）。"""
    d = load_dump()
    w = int(d["width_in_mbs"])
    h = int(d["height_in_mbs"])
    total = int(d["total_mbs"])
    first_type = d.get("first_mb_class", "I_4x4")

    fig, ax = plt.subplots(figsize=(9.2, 8.0))
    cell = 1.0
    for y in range(h):
        for x in range(w):
            addr = y * w + x
            # 全帧内：第一个宏块用真实解出的类型高亮，其余统一 Intra 蓝。
            if addr == 0:
                fc, ec, tc = C_ORANGE, "white", "white"
            else:
                fc, ec, tc = "#dbeafe", C_BLUE, C_DARK
            # 画在图像坐标：y 向下增长，故用 (h-1-y)。
            yy = h - 1 - y
            ax.add_patch(plt.Rectangle((x * cell, yy * cell), cell, cell,
                                       facecolor=fc, edgecolor=ec, lw=1.2,
                                       zorder=2))
            ax.text(x * cell + cell / 2, yy * cell + cell / 2, str(addr),
                    ha="center", va="center", fontsize=8.5, color=tc,
                    zorder=3)
    ax.set_xlim(-0.3, w * cell + 0.3)
    ax.set_ylim(-0.9, h * cell + 0.5)
    ax.set_aspect("equal")
    ax.axis("off")
    ax.set_title("真实一帧的宏块网格：%d 列 × %d 行 = %d 个宏块（每格 16×16 像素）"
                 % (w, h, total), fontweight="bold", fontsize=13)
    # 图例
    ax.add_patch(plt.Rectangle((0, -0.75), 0.5, 0.4, facecolor=C_ORANGE,
                               edgecolor="white", zorder=2))
    ax.text(0.6, -0.55, "mbAddr=0（真实解出 mb_type=0 → %s）" % first_type,
            ha="left", va="center", fontsize=9.5, color=C_DARK)
    ax.add_patch(plt.Rectangle((6.0, -0.75), 0.5, 0.4, facecolor="#dbeafe",
                               edgecolor=C_BLUE, zorder=2))
    ax.text(6.6, -0.55, "其余：Intra（IDR 帧全帧内）", ha="left", va="center",
            fontsize=9.5, color=C_DARK)
    plt.tight_layout()
    out = os.path.join(HERE, "mb_grid.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("mb_grid.png done")


def chart_raster_scan():
    """精确层：宏块光栅扫描顺序 0→98，用箭头画出'从左到右、到行尾折返下一行行首'。"""
    d = load_dump()
    w = int(d["width_in_mbs"])
    h = int(d["height_in_mbs"])

    fig, ax = plt.subplots(figsize=(9.6, 8.4))
    cell = 1.0
    centers = {}
    for y in range(h):
        for x in range(w):
            addr = y * w + x
            yy = h - 1 - y
            cx, cy = x * cell + cell / 2, yy * cell + cell / 2
            centers[addr] = (cx, cy)
            # 高亮头两行的行首/行尾，突出"折返"这一动作。
            hot = addr in (0, w - 1, w, 2 * w - 1, h * w - 1)
            fc = "#fef3c7" if hot else "#f8fafc"
            ax.add_patch(plt.Rectangle((x * cell, yy * cell), cell, cell,
                                       facecolor=fc, edgecolor="#cbd5e1",
                                       lw=1.0, zorder=2))
            ax.text(cx, cy, str(addr), ha="center", va="center", fontsize=8.5,
                    color=C_DARK, zorder=3)

    # 行内箭头（从左到右）：画在每行格子底部偏下，避开数字。
    for y in range(h):
        yy = h - 1 - y
        ybar = yy * cell + 0.16
        ax.add_patch(FancyArrowPatch((0.12, ybar),
                                     (w * cell - 0.12, ybar),
                                     arrowstyle="-|>", mutation_scale=12,
                                     color=C_BLUE, lw=1.6, zorder=4))
    # 折返箭头：只在头两行之间画一条醒目的红色折返，示意"到行尾回到下一行行首"。
    for y in (0, 1):
        yy_end = (h - 1 - y) * cell
        yy_next = (h - 1 - (y + 1)) * cell
        ax.add_patch(FancyArrowPatch((w * cell - 0.5, yy_end + 0.16),
                                     (0.5, yy_next + cell - 0.16),
                                     arrowstyle="-|>", mutation_scale=13,
                                     color=C_RED, lw=1.8,
                                     connectionstyle="arc3,rad=0.18",
                                     zorder=5))
    ax.set_xlim(-0.3, w * cell + 0.3)
    ax.set_ylim(-0.7, h * cell + 0.4)
    ax.set_aspect("equal")
    ax.axis("off")
    ax.set_title("宏块的光栅扫描顺序：mbAddr 从 0 数到 %d" % (w * h - 1),
                 fontweight="bold", fontsize=13)
    ax.text(w / 2, -0.45,
            "蓝色箭头：行内从左到右　红色箭头：到行尾折返下一行行首　"
            "这就是 slice 里宏块的排列顺序",
            ha="center", fontsize=9.5, color=C_GRAY)
    plt.tight_layout()
    out = os.path.join(HERE, "raster_scan.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("raster_scan.png done")


def chart_type_stats():
    """精确层：本帧宏块类型分布柱状图（真实数据：IDR 全 99 个 Intra）。"""
    d = load_dump()
    intra = int(d["intra_mbs"])
    inter = int(d["inter_mbs"])
    skip = int(d["skip_mbs"])
    total = int(d["total_mbs"])

    labels = ["Intra\n(帧内)", "P\n(帧间)", "Skip\n(跳过)"]
    vals = [intra, inter, skip]
    colors = [C_BLUE, C_GREEN, C_GRAY]

    fig, ax = plt.subplots(figsize=(8.4, 5.0))
    bars = ax.bar(labels, vals, color=colors, width=0.55, zorder=3)
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width() / 2, v + total * 0.02,
                "%d 个" % v, ha="center", va="bottom", fontsize=12,
                fontweight="bold", color=C_DARK)
    ax.set_ylim(0, total * 1.18)
    ax.set_ylabel("宏块数量", fontsize=11)
    ax.grid(axis="y", ls="--", alpha=0.4, zorder=0)
    ax.set_title("本帧（IDR 关键帧）宏块类型分布：%d 个全部帧内" % intra,
                 fontweight="bold", fontsize=13)
    ax.text(1.0, total * 1.05,
            "IDR 是关键帧，不参考任何其它帧，所以整帧只有帧内宏块；\n"
            "到 P 帧才会出现帧间和 skip 宏块（后续篇章）",
            ha="center", va="center", fontsize=9.5, color=C_ORANGE)
    plt.tight_layout()
    out = os.path.join(HERE, "type_stats.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("type_stats.png done")


def chart_hierarchy():
    """形象层：序列→帧→切片→宏块→子块的层级关系。"""
    fig, ax = plt.subplots(figsize=(10.2, 5.2))
    ax.set_xlim(0, 10.2)
    ax.set_ylim(0.4, 5.5)
    ax.axis("off")

    levels = [
        ("序列 Sequence", "整段视频，共享一套 SPS", C_PURPLE, 4.5),
        ("帧 Picture", "一幅画面，本例 176×144", C_CYAN, 3.6),
        ("切片 Slice", "一帧切成 1 个或多个，容错+并行的单位", C_BLUE, 2.7),
        ("宏块 Macroblock", "16×16 像素，预测/变换的基本单位", C_ORANGE, 1.8),
        ("子块 Sub-block", "宏块内再切 4×4 / 8×8，各自预测", C_GREEN, 0.9),
    ]
    x0, bw, bh = 0.5, 3.2, 0.62
    centers = []
    for name, desc, col, y in levels:
        ax.add_patch(FancyBboxPatch((x0, y), bw, bh,
                                    boxstyle="round,pad=0.03,rounding_size=0.08",
                                    fc=col, ec="white", lw=2, zorder=3))
        ax.text(x0 + bw / 2, y + bh / 2, name, ha="center", va="center",
                color="white", fontsize=12, fontweight="bold", zorder=4)
        ax.text(x0 + bw + 0.35, y + bh / 2, desc, ha="left", va="center",
                fontsize=10.5, color=C_DARK, zorder=4)
        centers.append((x0 + bw / 2, y))
    # 层级向下的"包含"箭头
    for i in range(len(levels) - 1):
        ax.add_patch(FancyArrowPatch((centers[i][0], centers[i][1]),
                                     (centers[i + 1][0], centers[i + 1][1] + bh),
                                     arrowstyle="-|>", mutation_scale=14,
                                     color=C_GRAY, lw=1.8, zorder=2))
    ax.set_title("H.264 的画面层级：一帧怎么一层层拆到 16×16 的宏块",
                 fontweight="bold", fontsize=13, pad=16)
    plt.tight_layout()
    out = os.path.join(HERE, "hierarchy.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("hierarchy.png done")


if __name__ == "__main__":
    sync_stage_map()
    chart_hierarchy()
    chart_mb_grid()
    chart_raster_scan()
    chart_type_stats()
    print("all charts done")
