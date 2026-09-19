#!/usr/bin/env python3
"""A2 配图：读取 dct8x8_demo 导出的真实数据绘图 + DCT 基图（公式生成）。

数据来源：third-party/codec-from-scratch/jpeg/encoder/debug_data/
"""
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
DATA = os.path.join(REPO, "jpeg/encoder/debug_data")


def load(name):
    return np.loadtxt(os.path.join(DATA, name))


def load_stats():
    d = {}
    with open(os.path.join(DATA, "dct_stats.txt")) as f:
        for line in f:
            k, v = line.split()
            d[k] = float(v)
    return d


def annotate(ax, mat, fs=9, threshold_dark=None):
    vmax = np.abs(mat).max()
    for (i, j), v in np.ndenumerate(mat):
        iv = int(round(v))
        norm = abs(v) / (vmax + 1e-9)
        color = "white" if norm > 0.55 else "black"
        ax.text(j, i, str(iv), ha="center", va="center", color=color,
                fontsize=fs)


# 图1：DCT 基图 —— 8x8=64 个频率成分各长什么样（公式生成）
def chart_basis():
    fig, axes = plt.subplots(8, 8, figsize=(7.5, 8))
    xs = np.arange(8)
    for v in range(8):
        for u in range(8):
            cu = 1 / np.sqrt(2) if u == 0 else 1
            cv = 1 / np.sqrt(2) if v == 0 else 1
            bx = np.cos((2 * xs + 1) * u * np.pi / 16)
            by = np.cos((2 * xs + 1) * v * np.pi / 16)
            patch = cv * cu * np.outer(by, bx)
            ax = axes[v][u]
            ax.imshow(patch, cmap="gray", vmin=-1, vmax=1)
            ax.set_xticks([]); ax.set_yticks([])
    fig.suptitle("DCT 的 64 个基图：左上是低频（大色块），右下是高频（细密纹理）",
                 fontsize=13, y=0.995)
    axes[0][0].set_title("DC", fontsize=9, color="#C0392B")
    fig.text(0.5, 0.02, "任何 8×8 像素块 = 这 64 个图案按 DCT 系数加权叠加",
             ha="center", fontsize=11)
    plt.tight_layout(rect=[0, 0.03, 1, 0.97])
    plt.savefig(os.path.join(HERE, "dct_basis.png"), dpi=110)
    plt.close()


# 图2：真实边缘块 —— 像素块 → DCT 系数矩阵（并排 + 数值）
def chart_pixels_to_coeffs():
    px = load("dct_pixels.txt")
    co = load("dct_coeffs.txt")
    fig, axes = plt.subplots(1, 2, figsize=(11.5, 5.3))

    im0 = axes[0].imshow(px, cmap="gray", vmin=0, vmax=255)
    annotate(axes[0], px, fs=9)
    axes[0].set_title("输入：真实 8×8 像素块（一条硬边缘）")
    axes[0].set_xticks([]); axes[0].set_yticks([])

    # 系数用发散色标，0 为中性色
    vmax = np.abs(co).max()
    im1 = axes[1].imshow(co, cmap="RdBu_r", vmin=-vmax, vmax=vmax)
    annotate(axes[1], co, fs=9)
    axes[1].set_title("输出：DCT 系数（左上 DC，越往右下越高频）")
    axes[1].set_xticks([]); axes[1].set_yticks([])
    # 框出 DC
    axes[1].add_patch(plt.Rectangle((-0.5, -0.5), 1, 1, fill=False,
                      edgecolor="#00A000", lw=2.5))

    fig.suptitle("真实数据：一个像素块经 DCT 变成 64 个频率系数（数字为代码实际输出）",
                 fontsize=13)
    plt.tight_layout(rect=[0, 0, 1, 0.96])
    plt.savefig(os.path.join(HERE, "pixels_to_coeffs.png"), dpi=110)
    plt.close()


# 图3：平滑块 vs 边缘块的系数对比 —— 揭示"平滑压得狠"
def chart_smooth_vs_edge():
    sm_co = load("dct_smooth_coeffs.txt")
    ed_co = load("dct_coeffs.txt")
    fig, axes = plt.subplots(1, 2, figsize=(11.5, 5.3))

    for ax, co, t in zip(
            axes, [sm_co, ed_co],
            ["平滑块：只剩 DC + 两三个小系数",
             "边缘块：系数散得到处都是"]):
        vmax = max(np.abs(co).max(), 1)
        ax.imshow(co, cmap="RdBu_r", vmin=-vmax, vmax=vmax)
        annotate(ax, co, fs=9)
        ax.set_title(t)
        ax.set_xticks([]); ax.set_yticks([])
        nz = int(np.sum(np.round(co) != 0))
        ax.set_xlabel(f"非零系数：{nz} / 64", fontsize=11)

    fig.suptitle("同样是 8×8，平滑区域 DCT 后几乎全是零 —— 这就是 JPEG 能狠压的入口",
                 fontsize=13)
    plt.tight_layout(rect=[0, 0, 1, 0.96])
    plt.savefig(os.path.join(HERE, "smooth_vs_edge.png"), dpi=110)
    plt.close()


# 图4：能量集中 —— 系数能量随"到左上角距离"的衰减
def chart_energy_concentration():
    co = load("dct_coeffs.txt")
    sm = load("dct_smooth_coeffs.txt")
    # 按 zigzag 顺序统计累计能量占比
    def cumulative(coeffs):
        # 简单按 (u+v) 频率带聚合能量
        bands = np.zeros(15)
        for v in range(8):
            for u in range(8):
                bands[u + v] += coeffs[v][u] ** 2
        cum = np.cumsum(bands)
        return cum / cum[-1] * 100

    ce = cumulative(co)
    cs = cumulative(sm)
    x = np.arange(15)
    fig, ax = plt.subplots(figsize=(8, 4.6))
    ax.plot(x, cs, "o-", color="#5B8FF9", label="平滑块")
    ax.plot(x, ce, "s-", color="#F6416C", label="边缘块")
    ax.axhline(90, color="gray", ls="--", lw=1)
    ax.text(10, 91.5, "90% 能量线", color="gray", fontsize=9)
    ax.set_xlabel("频率带（0=DC，越大越高频）")
    ax.set_ylabel("累计能量占比 (%)")
    ax.set_title("能量向低频集中：平滑块几个低频系数就装下 99% 能量")
    ax.legend()
    ax.set_ylim(0, 105)
    ax.grid(alpha=0.3)
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "energy_concentration.png"), dpi=110)
    plt.close()


# 图5（形象层）：DCT 在 JPEG 流水线里的位置
def chart_pipeline():
    from matplotlib.patches import FancyArrowPatch, FancyBboxPatch
    fig, ax = plt.subplots(figsize=(11, 3.2))
    ax.set_xlim(0, 12); ax.set_ylim(0, 3); ax.axis("off")

    def box(x, text, fc, ec, w=2.0):
        ax.add_patch(FancyBboxPatch((x, 1.0), w, 1.1,
                     boxstyle="round,pad=0.05,rounding_size=0.1",
                     fc=fc, ec=ec, lw=1.8))
        ax.text(x + w / 2, 1.55, text, ha="center", va="center", fontsize=11)

    def arrow(x1, x2):
        ax.add_patch(FancyArrowPatch((x1, 1.55), (x2, 1.55),
                     arrowstyle="-|>", mutation_scale=15, color="#555", lw=1.6))

    ax.text(6, 2.7, "DCT 在 JPEG 流水线的位置：不压缩数据，只重排能量",
            ha="center", fontsize=13, fontweight="bold")
    box(0.2, "色彩转换\n(上一篇)", "#D5F5E3", "#27AE60")
    box(3.0, "8×8 DCT\n(本篇·重排能量)", "#D6EAF8", "#2E86C1", w=2.4)
    box(6.2, "量化\n(下一篇·真正丢数据)", "#FCF3CF", "#B7950B", w=2.6)
    box(9.4, "熵编码\n(打包)", "#E8DAEF", "#7D3C98")
    arrow(2.2, 3.0); arrow(5.4, 6.2); arrow(8.8, 9.4)
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "dct_pipeline.png"), dpi=110)
    plt.close()


if __name__ == "__main__":
    chart_basis()
    chart_pixels_to_coeffs()
    chart_smooth_vs_edge()
    chart_energy_concentration()
    chart_pipeline()
    print("charts generated in", HERE)
