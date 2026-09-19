#!/usr/bin/env python3
"""A1 配图生成：读取 codec-from-scratch C++ 模块导出的真实数据绘图。

数据来源（真实运行 color_transform_demo 的输出）：
  third-party/codec-from-scratch/jpeg/encoder/debug_data/
所有图上的数字都是 C++ 代码真实跑出来的，不手动编造。
"""
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.font_manager as fm
fm._load_fontmanager(try_read_cache=False)
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import LinearSegmentedColormap

plt.rcParams["font.family"] = "Noto Sans CJK JP"
plt.rcParams["axes.unicode_minus"] = False

HERE = os.path.dirname(os.path.abspath(__file__))
# 相对定位到仓库的 debug_data
REPO = os.path.normpath(os.path.join(
    HERE, "../../../../../third-party/codec-from-scratch"))
DATA = os.path.join(REPO, "jpeg/encoder/debug_data")


def load_matrix(name):
    return np.loadtxt(os.path.join(DATA, name), dtype=int)


def load_stats():
    d = {}
    with open(os.path.join(DATA, "stats.txt")) as f:
        for line in f:
            k, v = line.split()
            d[k] = int(v)
    return d


def annotate_grid(ax, mat, fontsize=11, fmt="{:d}"):
    for (i, j), v in np.ndenumerate(mat):
        # 根据背景明暗选择文字颜色
        norm = (v - mat.min()) / (mat.max() - mat.min() + 1e-9)
        color = "white" if norm < 0.45 else "black"
        ax.text(j, i, fmt.format(int(v)), ha="center", va="center",
                color=color, fontsize=fontsize)


# 图1：一个像素的 RGB → YCbCr 分解（真实系数计算，展示公式落地）
def chart_pixel_decompose():
    # 取一组代表性 RGB，用文章讲的 BT.601 系数真实计算
    samples = [
        ("纯红", 255, 0, 0),
        ("纯绿", 0, 255, 0),
        ("纯蓝", 0, 0, 255),
        ("肤色", 235, 180, 150),
    ]
    names, ys, cbs, crs = [], [], [], []
    for name, r, g, b in samples:
        y = 0.299 * r + 0.587 * g + 0.114 * b
        cb = -0.168736 * r - 0.331264 * g + 0.5 * b + 128
        cr = 0.5 * r - 0.418688 * g - 0.081312 * b + 128
        names.append(name)
        ys.append(y)
        cbs.append(cb)
        crs.append(cr)

    x = np.arange(len(names))
    w = 0.25
    fig, ax = plt.subplots(figsize=(8, 4.5))
    ax.bar(x - w, ys, w, label="Y 亮度", color="#5B8FF9")
    ax.bar(x, cbs, w, label="Cb 蓝色度", color="#61DDAA")
    ax.bar(x + w, crs, w, label="Cr 红色度", color="#F6BD16")
    ax.axhline(128, color="gray", ls="--", lw=1, alpha=0.7)
    ax.text(len(names) - 0.5, 132, "128（色度中点）", fontsize=9, color="gray")
    ax.set_xticks(x)
    ax.set_xticklabels(names)
    ax.set_ylabel("分量值 (0-255)")
    ax.set_title("同一个像素，BT.601 系数算出的 Y / Cb / Cr")
    ax.legend(loc="upper right")
    ax.set_ylim(0, 260)
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "pixel_decompose.png"), dpi=110)
    plt.close()


# 图2：真实 Cb 块 8x8（抽样前）→ 4x4（4:2:0 抽样后），并排 + 数值
def chart_chroma_subsample():
    cb_full = load_matrix("cb_full_block.txt")   # 8x8
    cb_420 = load_matrix("cb_420_block.txt")     # 4x4
    cmap = "viridis"

    fig, axes = plt.subplots(1, 2, figsize=(11, 5.2))
    im0 = axes[0].imshow(cb_full, cmap=cmap, vmin=0, vmax=255)
    annotate_grid(axes[0], cb_full, fontsize=10)
    axes[0].set_title("抽样前：全分辨率 Cb（8×8）")
    axes[0].set_xticks([]); axes[0].set_yticks([])

    im1 = axes[1].imshow(cb_420, cmap=cmap, vmin=0, vmax=255)
    annotate_grid(axes[1], cb_420, fontsize=14)
    axes[1].set_title("4:2:0 抽样后：每 2×2 取平均（4×4）")
    axes[1].set_xticks([]); axes[1].set_yticks([])

    fig.colorbar(im1, ax=axes, fraction=0.025, pad=0.02, label="Cb 值")
    fig.suptitle("真实数据：色度平面被砍成四分之一（数字为代码实际输出）",
                 fontsize=13)
    plt.savefig(os.path.join(HERE, "chroma_subsample.png"), dpi=110,
                bbox_inches="tight")
    plt.close()


# 图3：数据量对比（真实统计）—— RGB vs YCbCr444 vs YCbCr420
def chart_data_size():
    s = load_stats()
    labels = ["原始 RGB", "YCbCr\n4:4:4", "YCbCr\n4:2:0"]
    values = [s["rgb_bytes"], s["ycbcr_444_bytes"], s["ycbcr_420_bytes"]]
    kb = [v / 1024 for v in values]
    colors = ["#8C8C8C", "#5B8FF9", "#61DDAA"]

    fig, ax = plt.subplots(figsize=(7.5, 4.5))
    bars = ax.bar(labels, kb, color=colors, width=0.55)
    for b, v in zip(bars, values):
        ax.text(b.get_x() + b.get_width() / 2, b.get_height() + 1,
                f"{v/1024:.0f} KB", ha="center", fontsize=11)
    base = values[0]
    ax.text(2, kb[2] / 2, f"仅为原始的\n{values[2]*100//base}%",
            ha="center", va="center", fontsize=12, color="white",
            fontweight="bold")
    ax.set_ylabel("数据量 (KB)")
    ax.set_title(f"一张 {s['width']}×{s['height']} 图：色度抽样还没进正式压缩就省了一半")
    ax.set_ylim(0, max(kb) * 1.15)
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "data_size.png"), dpi=110)
    plt.close()


# 图4：Y 平面（亮度，携带主要细节）vs Cb 平面（色度，抽样后模糊）真实灰度图
def chart_planes():
    from PIL import Image
    y = np.array(Image.open(os.path.join(DATA, "y_plane.ppm")).convert("L"))
    cb = np.array(Image.open(os.path.join(DATA, "cb_plane.ppm")).convert("L"))
    cr = np.array(Image.open(os.path.join(DATA, "cr_plane.ppm")).convert("L"))

    fig, axes = plt.subplots(1, 3, figsize=(12, 4.3))
    for ax, img, t in zip(
            axes, [y, cb, cr],
            ["Y 亮度（全分辨率，细节都在这）",
             "Cb 蓝色度（已抽样，128×128）",
             "Cr 红色度（已抽样，128×128）"]):
        ax.imshow(img, cmap="gray", vmin=0, vmax=255)
        ax.set_title(t, fontsize=11)
        ax.axis("off")
    fig.suptitle("同一张图拆成三个平面：眼睛主要看 Y，Cb/Cr 糊一点看不出来",
                 fontsize=13)
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "three_planes.png"), dpi=110,
                bbox_inches="tight")
    plt.close()


# 图5（形象层）：颜色"掰开"的整体流程示意
def chart_pipeline():
    from matplotlib.patches import FancyArrowPatch, FancyBboxPatch

    fig, ax = plt.subplots(figsize=(11, 4.6))
    ax.set_xlim(0, 11)
    ax.set_ylim(0, 5)
    ax.axis("off")

    def box(x, y, w, h, text, fc, ec):
        p = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.05,rounding_size=0.12",
                           fc=fc, ec=ec, lw=1.8)
        ax.add_patch(p)
        ax.text(x + w / 2, y + h / 2, text, ha="center", va="center",
                fontsize=11.5)

    def arrow(x1, y1, x2, y2, color):
        ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2),
                     arrowstyle="-|>", mutation_scale=16, color=color, lw=1.8))

    ax.text(5.5, 4.7, "RGB 的颜色被掰开成 亮度 + 两路色度，色度再偷偷缩小",
            ha="center", fontsize=14, fontweight="bold")

    box(0.2, 2.0, 1.9, 1.1, "原始 RGB\nR,G,B 各 8bit\n三者高度相关",
        "#FADBD8", "#C0392B")
    box(2.8, 2.0, 1.9, 1.1, "RGB→YCbCr\nBT.601 系数\n线性组合·无损",
        "#D6EAF8", "#2E86C1")
    box(5.4, 3.3, 1.9, 0.95, "Y 亮度\n全分辨率保留", "#D5F5E3", "#27AE60")
    box(5.4, 2.05, 1.9, 0.95, "Cb 蓝色度\n4:2:0 砍到 1/4", "#FCF3CF", "#B7950B")
    box(5.4, 0.8, 1.9, 0.95, "Cr 红色度\n4:2:0 砍到 1/4", "#FCF3CF", "#B7950B")
    box(8.1, 2.0, 2.0, 1.1, "送入 DCT/量化\n（后续文章）\n数据已先省一半",
        "#E8DAEF", "#7D3C98")

    arrow(2.1, 2.55, 2.8, 2.55, "#333333")
    arrow(4.7, 2.75, 5.4, 3.75, "#27AE60")
    arrow(4.7, 2.55, 5.4, 2.5, "#B7950B")
    arrow(4.7, 2.35, 5.4, 1.25, "#B7950B")
    arrow(7.3, 3.75, 8.1, 2.75, "#7D3C98")
    arrow(7.3, 2.5, 8.1, 2.55, "#7D3C98")
    arrow(7.3, 1.25, 8.1, 2.35, "#7D3C98")

    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "color_pipeline.png"), dpi=110)
    plt.close()


if __name__ == "__main__":
    chart_pixel_decompose()
    chart_chroma_subsample()
    chart_data_size()
    chart_planes()
    chart_pipeline()
    print("charts generated in", HERE)
