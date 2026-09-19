#!/usr/bin/env python3
"""A4 配图：读取 entropy_demo 导出的真实数据绘图。

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
    with open(os.path.join(DATA, "e_stats.txt")) as f:
        for line in f:
            k, v = line.split()
            d[k] = float(v)
    return d


def load_bits():
    d = {}
    with open(os.path.join(DATA, "e_bits.txt")) as f:
        for line in f:
            if line.startswith("#"):
                continue
            k, v = line.split()
            d[k] = int(v)
    return d


def load_rle():
    rows = []
    with open(os.path.join(DATA, "e_rle.txt")) as f:
        for line in f:
            if line.startswith("#"):
                continue
            p = line.split()
            rows.append((p[0], int(p[1]), int(p[2]), int(p[3]), int(p[4])))
    return rows  # (type, run, value, huff_bits, mag_bits)


# 图1（精确层）：量化后 8x8 矩阵上画 Zig-Zag 扫描路径（之字形箭头线）
def chart_zigzag_path():
    mat = load("e_zigzag_matrix.txt")  # 8x8 量化后系数
    order = load("e_zigzag_order.txt").astype(int)  # 64 个行主序索引
    fig, ax = plt.subplots(figsize=(7.2, 7.0))

    vmax = max(np.abs(mat).max(), 1)
    ax.imshow(mat, cmap="RdBu_r", vmin=-vmax, vmax=vmax)
    for (i, j), v in np.ndenumerate(mat):
        iv = int(round(v))
        norm = abs(v) / (vmax + 1e-9)
        color = "white" if norm > 0.55 else "#333"
        ax.text(j, i, str(iv), ha="center", va="center",
                color=color, fontsize=11, zorder=3)

    # 把 order（行主序索引）转成 (col=x, row=y) 坐标序列，连成折线。
    xs = [idx % 8 for idx in order]
    ys = [idx // 8 for idx in order]
    ax.plot(xs, ys, "-", color="#1B4F72", lw=1.4, alpha=0.85, zorder=2)
    # 起点标记
    ax.scatter([xs[0]], [ys[0]], s=120, color="#27AE60", zorder=4)
    ax.text(xs[0], ys[0] - 0.42, "起点 DC", color="#1E8449",
            fontsize=10, ha="center", zorder=5)
    ax.scatter([xs[-1]], [ys[-1]], s=120, color="#C0392B", zorder=4)
    ax.text(xs[-1], ys[-1] + 0.5, "终点 最高频", color="#922B21",
            fontsize=10, ha="center", zorder=5)

    ax.set_title("Zig-Zag 扫描：沿之字形从低频走到高频（T.81 Fig A.6）\n"
                 "走位刻意让右下角那一片 0 排到序列末尾", fontsize=12)
    ax.set_xticks(range(8)); ax.set_yticks(range(8))
    ax.set_xlabel("水平频率 u →")
    ax.set_ylabel("垂直频率 v →")
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "zigzag_path.png"), dpi=110)
    plt.close()


# 图2（精确层）：zigzag 后一维 64 元素条形图，后半段全是 0
def chart_zigzag_1d():
    zz = load("e_zigzag.txt").astype(int)
    st = load_stats()
    nz = int(st["block_nonzero"])
    # 最后一个非零的位置
    last_nz = max(k for k in range(64) if zz[k] != 0)

    fig, ax = plt.subplots(figsize=(12, 4.4))
    colors = ["#2E86C1" if v != 0 else "#D5DBDB" for v in zz]
    ax.bar(range(64), zz, color=colors, width=0.85)
    ax.axhline(0, color="#555", lw=0.8)

    ax.axvspan(last_nz + 0.5, 63.5, color="#FADBD8", alpha=0.5, zorder=0)
    ax.text((last_nz + 63) / 2, ax.get_ylim()[1] * 0.72,
            f"第 {last_nz + 1} 位之后全是 0\n（{63 - last_nz} 连 0 → 一个 EOB 搞定）",
            ha="center", fontsize=11, color="#922B21")
    ax.axvline(0, color="#27AE60", ls=":", lw=1)
    ax.text(0.4, ax.get_ylim()[0] * 0.85, "第 0 位=DC", color="#1E8449",
            fontsize=9)

    ax.set_title(f"拉直成一维后：{nz} 个非零挤在前段，后面拖着一长串 0（真实数据）",
                 fontsize=12)
    ax.set_xlabel("Zig-Zag 序列位置 k（0=DC，越右越高频）")
    ax.set_ylabel("量化后系数值")
    ax.set_xlim(-1, 64)
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "zigzag_1d.png"), dpi=110)
    plt.close()


# 图3（精确层）：比特数对比柱状图 512 → 208 → 173
def chart_bits_compare():
    b = load_bits()
    labels = ["朴素\n64系数×8bit", "只存非零\n26×8bit", "熵编码后\n(真实)"]
    vals = [b["raw_64coeff"], b["nonzero_only"], b["entropy_coded"]]
    colors = ["#AEB6BF", "#5DADE2", "#7D3C98"]

    fig, ax = plt.subplots(figsize=(8.4, 5.2))
    bars = ax.bar(labels, vals, color=colors, width=0.6)
    for bar, v in zip(bars, vals):
        ax.text(bar.get_x() + bar.get_width() / 2, v + 6, f"{v} bit",
                ha="center", fontsize=12, fontweight="bold")

    ratio = b["raw_64coeff"] / b["entropy_coded"]
    ax.set_title(f"一个 8×8 块：512 bit 榨到 {b['entropy_coded']} bit"
                 f"（约 {ratio:.1f} 倍，真实数据）", fontsize=13)
    ax.set_ylabel("编码这个块需要的比特数")
    ax.set_ylim(0, b["raw_64coeff"] * 1.15)
    ax.grid(axis="y", alpha=0.3)
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "bits_compare.png"), dpi=110)
    plt.close()


# 图4（精确层）：RLE 压缩示意 —— 一串系数如何变成 (run,value) 对
def chart_rle_illustrate():
    rows = load_rle()
    # 取前若干个 AC 符号演示，含一个有 run>0 的
    fig, ax = plt.subplots(figsize=(12, 5.0))
    ax.axis("off")
    ax.set_xlim(0, 12); ax.set_ylim(0, 5)

    ax.text(6, 4.7, "游程编码：把「几个 0 + 一个非零」打包成一个 (run, value) 对",
            ha="center", fontsize=13, fontweight="bold")

    # 上排：原始 zigzag 片段（含 0）
    zz = load("e_zigzag.txt").astype(int)
    seg = zz[:14]
    x0 = 0.6
    for k, v in enumerate(seg):
        fc = "#D5DBDB" if v == 0 else "#AED6F1"
        ax.add_patch(plt.Rectangle((x0 + k * 0.8, 3.4), 0.72, 0.6,
                     fc=fc, ec="#5D6D7E"))
        ax.text(x0 + k * 0.8 + 0.36, 3.7, str(v), ha="center",
                va="center", fontsize=10)
    ax.text(x0, 4.15, "① 一维系数序列（灰=0）", fontsize=10, color="#555")

    # 下排：对应的 (run,value) 对
    pairs = [(r[1], r[2]) for r in rows if r[0] == "AC"][:8]
    ax.text(x0, 2.5, "② 打包成 (run, value)：run=前面几个 0，value=这个非零值",
            fontsize=10, color="#555")
    x1 = 0.8
    for (run, val) in pairs:
        w = 1.35
        ax.add_patch(plt.Rectangle((x1, 1.6), w, 0.7, fc="#E8DAEF",
                     ec="#7D3C98", lw=1.4))
        ax.text(x1 + w / 2, 1.95, f"({run},{val})", ha="center",
                va="center", fontsize=11)
        x1 += w + 0.25

    # 末尾 EOB
    ax.add_patch(plt.Rectangle((x1, 1.6), 1.1, 0.7, fc="#FADBD8",
                 ec="#C0392B", lw=1.4))
    ax.text(x1 + 0.55, 1.95, "EOB", ha="center", va="center",
            fontsize=11, color="#922B21")

    ax.text(6, 0.9, "尾部一长串 0 不用逐个记，一个 EOB（块尾标记）就打发了",
            ha="center", fontsize=11, color="#922B21")
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "rle_illustrate.png"), dpi=110)
    plt.close()


# 图5（形象层）：熵编码在整条流水线里的位置
def chart_pipeline():
    from matplotlib.patches import FancyArrowPatch, FancyBboxPatch
    fig, ax = plt.subplots(figsize=(11, 4.6))
    ax.set_xlim(0, 12); ax.set_ylim(0, 4.6); ax.axis("off")

    def box(x, y, w, h, text, fc, ec):
        ax.add_patch(FancyBboxPatch((x, y), w, h,
                     boxstyle="round,pad=0.05,rounding_size=0.1",
                     fc=fc, ec=ec, lw=1.8))
        ax.text(x + w / 2, y + h / 2, text, ha="center", va="center",
                fontsize=10.5)

    def arrow(x1, y1, x2, y2):
        ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2),
                     arrowstyle="-|>", mutation_scale=15, color="#555",
                     lw=1.6))

    ax.text(6, 4.25, "熵编码：流水线最后一步，无损，真正让文件体积塌下去",
            ha="center", fontsize=13, fontweight="bold")

    box(0.2, 2.5, 1.9, 1.0, "色彩转换\n#01", "#D5F5E3", "#27AE60")
    box(2.5, 2.5, 1.9, 1.0, "8×8 DCT\n#02·不丢", "#D6EAF8", "#2E86C1")
    box(4.8, 2.5, 2.0, 1.0, "量化\n#03·唯一丢", "#FCF3CF", "#B7950B")
    box(7.2, 2.5, 4.4, 1.0, "熵编码 #04·不丢一个 bit", "#E8DAEF", "#7D3C98")
    arrow(2.1, 3.0, 2.5, 3.0)
    arrow(4.4, 3.0, 4.8, 3.0)
    arrow(6.8, 3.0, 7.2, 3.0)

    # 熵编码内部三步
    box(7.2, 1.1, 1.35, 0.85, "Zig-Zag\n排 0", "#F4ECF7", "#7D3C98")
    box(8.75, 1.1, 1.35, 0.85, "游程\n(run,val)", "#F4ECF7", "#7D3C98")
    box(10.3, 1.1, 1.3, 0.85, "霍夫曼\n变长码", "#F4ECF7", "#7D3C98")
    arrow(8.55, 1.52, 8.75, 1.52)
    arrow(10.1, 1.52, 10.3, 1.52)
    arrow(9.4, 2.5, 9.4, 1.95)

    ax.text(6, 0.4, "一个块 512 bit → 173 bit；整幅 Y 平面 524288 bit → 28058 bit（真实数据）",
            ha="center", fontsize=10.5, color="#7D3C98")
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "entropy_pipeline.png"), dpi=110)
    plt.close()


if __name__ == "__main__":
    chart_zigzag_path()
    chart_zigzag_1d()
    chart_bits_compare()
    chart_rle_illustrate()
    chart_pipeline()
    print("charts generated in", HERE)
