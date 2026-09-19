#!/usr/bin/env python3
"""A3 配图：读取 quantize_demo 导出的真实数据绘图。

数据来源：third-party/codec-from-scratch/jpeg/encoder/debug_data/
所有数字都来自 C++ 代码的真实输出，不是手写的示例值。
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
    with open(os.path.join(DATA, "q_stats.txt")) as f:
        for line in f:
            k, v = line.split()
            d[k] = float(v)
    return d


def annotate(ax, mat, fs=9):
    vmax = np.abs(mat).max()
    for (i, j), v in np.ndenumerate(mat):
        iv = int(round(v))
        norm = abs(v) / (vmax + 1e-9)
        color = "white" if norm > 0.55 else "black"
        ax.text(j, i, str(iv), ha="center", va="center", color=color,
                fontsize=fs)


# 图1：Q=50 亮度量化表热力图 —— 左上步长小、右下步长大
def chart_quant_table():
    t = load("q_luma_q50.txt")
    fig, ax = plt.subplots(figsize=(6.6, 6.0))
    im = ax.imshow(t, cmap="YlOrRd", vmin=0, vmax=t.max())
    annotate(ax, t, fs=11)
    ax.set_title("T.81 Annex K 亮度量化表（质量 50 基准）\n"
                 "左上=低频步长小（保得多），右下=高频步长大（丢得狠）",
                 fontsize=12)
    ax.set_xticks(range(8)); ax.set_yticks(range(8))
    ax.set_xlabel("水平频率 u →（越右越高频）")
    ax.set_ylabel("垂直频率 v →（越下越高频）")
    fig.colorbar(im, ax=ax, fraction=0.046, pad=0.04, label="量化步长")
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "quant_table_q50.png"), dpi=110)
    plt.close()


# 图2：真实块 DCT系数 ÷ 量化表 → 量化后系数（展示大批变 0）
def chart_dct_to_quant():
    dct = load("q_coeffs_dct.txt")
    quant = load("q_quant_q50.txt")
    tbl = load("q_table_q50.txt")
    fig, axes = plt.subplots(1, 3, figsize=(15, 5.2))

    vmax = np.abs(dct).max()
    axes[0].imshow(dct, cmap="RdBu_r", vmin=-vmax, vmax=vmax)
    annotate(axes[0], dct, fs=8)
    nz0 = int(np.sum(np.round(dct) != 0))
    axes[0].set_title("① DCT 系数（量化前）")
    axes[0].set_xlabel(f"非零：{nz0} / 64")

    axes[1].imshow(tbl, cmap="YlOrRd", vmin=0, vmax=tbl.max())
    annotate(axes[1], tbl, fs=8)
    axes[1].set_title("② 除以 Q=50 量化表")
    axes[1].set_xlabel("每格 = 该频率的步长")

    vmax2 = max(np.abs(quant).max(), 1)
    axes[2].imshow(quant, cmap="RdBu_r", vmin=-vmax2, vmax=vmax2)
    annotate(axes[2], quant, fs=9)
    nz1 = int(np.sum(np.round(quant) != 0))
    axes[2].set_title("③ 四舍五入 → 量化后系数")
    axes[2].set_xlabel(f"非零：{nz1} / 64（大批变 0）")

    for ax in axes:
        ax.set_xticks([]); ax.set_yticks([])
    fig.suptitle("量化三步：系数 ÷ 步长 再取整，高频小系数被直接压成 0（真实数据）",
                 fontsize=13)
    plt.tight_layout(rect=[0, 0, 1, 0.95])
    plt.savefig(os.path.join(HERE, "dct_to_quant.png"), dpi=110)
    plt.close()


# 图3：Q=90 / 50 / 10 下同一块量化后系数对比 —— 滑块拧小，非零急剧减少
def chart_quality_compare():
    st = load_stats()
    qs = [90, 50, 10]
    fig, axes = plt.subplots(1, 3, figsize=(15, 5.2))
    for ax, q in zip(axes, qs):
        m = load(f"q_quant_q{q}.txt")
        vmax = max(np.abs(m).max(), 1)
        ax.imshow(m, cmap="RdBu_r", vmin=-vmax, vmax=vmax)
        annotate(ax, m, fs=9)
        nz = int(st[f"nonzero_q{q}"])
        ax.set_title(f"质量 Q={q}")
        ax.set_xlabel(f"非零系数：{nz} / 64", fontsize=12)
        ax.set_xticks([]); ax.set_yticks([])
    fig.suptitle("同一个块，拧动画质滑块：Q 越小，活下来的系数越少（真实数据）",
                 fontsize=13)
    plt.tight_layout(rect=[0, 0, 1, 0.95])
    plt.savefig(os.path.join(HERE, "quality_compare.png"), dpi=110)
    plt.close()


# 图4：量化误差 vs 质量 曲线（双轴：非零个数 + 系数 RMSE）
def chart_error_curve():
    sweep = load("q_sweep.txt")  # 列：quality nonzero rmse
    q = sweep[:, 0]
    nz = sweep[:, 1]
    rmse = sweep[:, 2]

    fig, ax1 = plt.subplots(figsize=(8.4, 4.8))
    c1, c2 = "#2E86C1", "#C0392B"
    ax1.plot(q, nz, "-", color=c1, lw=2, label="非零系数个数")
    ax1.set_xlabel("质量因子 Q（滑块位置）")
    ax1.set_ylabel("非零系数个数 / 64", color=c1)
    ax1.tick_params(axis="y", labelcolor=c1)
    ax1.set_ylim(0, 52)

    ax2 = ax1.twinx()
    ax2.plot(q, rmse, "--", color=c2, lw=2, label="系数均方根误差")
    ax2.set_ylabel("量化误差（系数 RMSE）", color=c2)
    ax2.tick_params(axis="y", labelcolor=c2)

    for qx in (10, 50, 90):
        ax1.axvline(qx, color="gray", ls=":", lw=1)
        ax1.text(qx, 1.0, f"Q={qx}", color="gray", fontsize=9,
                 ha="center")
    ax1.set_title("拧小 Q：保留的系数越来越少，误差越来越大（真实曲线）")
    ax1.grid(alpha=0.3)
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "error_curve.png"), dpi=110)
    plt.close()


# 图5（形象层）：画质滑块 → 缩放量化表 → 量化，在流水线里的位置
def chart_pipeline():
    from matplotlib.patches import FancyArrowPatch, FancyBboxPatch
    fig, ax = plt.subplots(figsize=(11, 4.4))
    ax.set_xlim(0, 12); ax.set_ylim(0, 4.4); ax.axis("off")

    def box(x, y, w, h, text, fc, ec):
        ax.add_patch(FancyBboxPatch((x, y), w, h,
                     boxstyle="round,pad=0.05,rounding_size=0.1",
                     fc=fc, ec=ec, lw=1.8))
        ax.text(x + w / 2, y + h / 2, text, ha="center", va="center",
                fontsize=11)

    def arrow(x1, y1, x2, y2):
        ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2),
                     arrowstyle="-|>", mutation_scale=15, color="#555",
                     lw=1.6))

    ax.text(6, 4.1, "画质滑块的真身：一个数字缩放整张量化表",
            ha="center", fontsize=13, fontweight="bold")

    # 流水线主线
    box(0.2, 1.7, 2.0, 1.1, "色彩转换\n#01", "#D5F5E3", "#27AE60")
    box(2.7, 1.7, 2.0, 1.1, "8×8 DCT\n#02·不丢数据", "#D6EAF8", "#2E86C1")
    box(5.2, 1.7, 2.6, 1.1, "量化\n#03·唯一丢数据", "#FCF3CF", "#B7950B")
    box(8.3, 1.7, 2.0, 1.1, "熵编码\n#04·不丢数据", "#E8DAEF", "#7D3C98")
    arrow(2.2, 2.25, 2.7, 2.25)
    arrow(4.7, 2.25, 5.2, 2.25)
    arrow(7.8, 2.25, 8.3, 2.25)

    # 滑块 → 缩放公式 → 量化表
    box(4.6, 3.0, 3.8, 0.9, "画质滑块 Q（1–100）", "#FDEDEC", "#E74C3C")
    arrow(6.5, 3.0, 6.5, 2.8)
    ax.text(8.6, 3.45, "scale = 200 − 2Q（Q≥50）", fontsize=9,
            color="#555", va="center")

    # 底部：Q 越小步长越大
    ax.text(6.5, 1.3, "Q 越小 → 步长越大 → 越多系数变 0 → 文件越小、越糊",
            ha="center", fontsize=10.5, color="#B7950B")
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "quant_pipeline.png"), dpi=110)
    plt.close()


if __name__ == "__main__":
    chart_quant_table()
    chart_dct_to_quant()
    chart_quality_compare()
    chart_error_curve()
    chart_pipeline()
    print("charts generated in", HERE)
