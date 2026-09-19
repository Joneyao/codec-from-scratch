#!/usr/bin/env python3
"""A5 配图：读取 encoder_main / verify_jpg.py 导出的真实数据绘图。

数据来源：third-party/codec-from-scratch/jpeg/encoder/
  out.jpg              真实编码出的 .jpg（256x256, quality=90）
  chart_data/markers.txt        out.jpg 各标记段的字节偏移与段长（真实解析）
  chart_data/verify_results.txt 不同 quality 的文件大小与 PSNR（真实测量）
  chart_data/orig.png           原始测试图
  chart_data/decoded_q85.png    q85 编码后再解码回来的图
所有数字都来自 C++ 编码器 + libjpeg 解码的真实输出，不是手写示例值。
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
ENC = os.path.join(REPO, "jpeg/encoder")
DATA = os.path.join(ENC, "chart_data")


def load_markers():
    rows = []
    with open(os.path.join(DATA, "markers.txt")) as f:
        for line in f:
            if line.startswith("#"):
                continue
            p = line.split()
            rows.append((int(p[0]), p[1], int(p[2]), int(p[3])))
    return rows  # (offset, name, marker_byte, seglen)


def load_verify():
    rows = []
    with open(os.path.join(DATA, "verify_results.txt")) as f:
        for line in f:
            if line.startswith("#"):
                continue
            q, s, p = line.split()
            rows.append((int(q), int(s), float(p)))
    return rows  # (quality, size_bytes, psnr_db)


# 图1（形象层）：JFIF 文件结构分层图 —— 真实 out.jpg 的段序 + 字节 + 作用
# 熵数据占 95% 体积，用等高块表达"逻辑顺序"，字节大小写在文字里（不按比例，
# 否则头部会被压成一条线）。右侧单独用一个细条示意真实体积占比。
def chart_jfif_structure():
    markers = load_markers()
    total = markers[-1][0] + 2  # 到 EOI 结束
    desc = {
        "SOI": ("文件开始", "#27AE60"),
        "APP0": ("JFIF 标识 / 版本 / 密度", "#5DADE2"),
        "DQT": ("量化表（表内按 zig-zag 顺序）", "#F4D03F"),
        "SOF0": ("帧头：宽高 / 分量数 / 抽样因子", "#EB984E"),
        "DHT": ("霍夫曼表（BITS + HUFFVAL）", "#AF7AC5"),
        "SOS": ("扫描头：分量 ↔ 量化/霍夫曼表映射", "#5499C7"),
        "ENTROPY": ("熵编码数据（每个 0xFF 后填 0x00）", "#EC7063"),
        "EOI": ("文件结束", "#27AE60"),
    }
    # 合并连续同名段（两张 DQT、四张 DHT）为一个逻辑块，累加字节。
    logical = []
    for k, (off, name, _mb, _sl) in enumerate(markers):
        end = markers[k + 1][0] if k + 1 < len(markers) else total
        size = end - off
        if logical and logical[-1][0] == name:
            logical[-1][1] += size
            logical[-1][2] += 1
        else:
            logical.append([name, size, 1])

    n = len(logical)
    fig, ax = plt.subplots(figsize=(10.5, 7.6))
    ax.set_xlim(0, 12)
    ax.set_ylim(0, n + 1.2)
    ax.axis("off")
    ax.set_title("一个真 .jpg 的骨架：先是一叠「说明书」标记段，最后才是熵数据\n"
                 "（out.jpg 真实解析，256×256，quality=90，共 11786 字节）",
                 fontsize=13)

    for i, (name, size, cnt) in enumerate(logical):
        y = n - 1 - i  # 顶部=文件头
        text, col = desc.get(name, (name, "#BBB"))
        ax.add_patch(plt.Rectangle((0.5, y + 0.12), 3.3, 0.76,
                     fc=col, ec="#34495E", lw=1.3))
        tag = name if name != "ENTROPY" else "熵数据"
        if cnt > 1:
            tag += f" ×{cnt}"
        ax.text(2.15, y + 0.5, tag, ha="center", va="center",
                fontsize=11, fontweight="bold")
        ax.text(4.1, y + 0.5, f"{size} 字节", ha="left", va="center",
                fontsize=9.5, color="#566573")
        ax.text(6.0, y + 0.5, text, ha="left", va="center",
                fontsize=10, color="#2C3E50")
        if i < n - 1:
            ax.annotate("", xy=(2.15, y + 0.06), xytext=(2.15, y + 0.14),
                        arrowprops=dict(arrowstyle="-|>", color="#7F8C8D"))
    ax.text(2.15, n + 0.25, "↓ 字节 0 → 文件末尾（解码器从上往下读）",
            ha="center", fontsize=9.5, color="#7F8C8D")
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "jfif_structure.png"), dpi=110)
    plt.close()


# 图2（精确层）：真实 out.jpg 头部十六进制 dump，高亮出 marker 字节
def chart_hex_dump():
    data = open(os.path.join(ENC, "out.jpg"), "rb").read()
    nbytes = 64  # 展示前 64 字节（覆盖 SOI/APP0/第一张 DQT 开头）
    cols = 16
    rows = nbytes // cols
    fig, ax = plt.subplots(figsize=(11.5, 4.6))
    ax.set_xlim(-1.5, cols)
    ax.set_ylim(-0.5, rows)
    ax.axis("off")
    ax.set_title("真实 out.jpg 头部十六进制：查看器就是靠这些 marker 认路的\n"
                 "（红=段起始 marker，蓝=段长字段，绿=JFIF 标识串）",
                 fontsize=12.5)

    # 需要高亮的字节位置。
    red = {0, 1, 2, 3, 20, 21}          # SOI, APP0 marker, DQT marker
    blue = {4, 5, 22, 23}               # APP0 段长, DQT 段长
    green = set(range(6, 11))           # "JFIF\0"
    note = {0: "SOI", 2: "APP0", 6: "JFIF", 20: "DQT"}

    for k in range(nbytes):
        r = k // cols
        c = k % cols
        y = rows - 1 - r
        b = data[k]
        if k in red:
            fc = "#F5B7B1"
        elif k in blue:
            fc = "#AED6F1"
        elif k in green:
            fc = "#ABEBC6"
        else:
            fc = "#F4F6F6"
        ax.add_patch(plt.Rectangle((c, y), 0.92, 0.9, fc=fc, ec="#D5D8DC"))
        ax.text(c + 0.46, y + 0.45, f"{b:02X}", ha="center", va="center",
                fontsize=9.5, family="monospace")
        if k in note:
            ax.text(c + 0.46, y + 1.02, note[k], ha="center", va="bottom",
                    fontsize=8.5, color="#922B21")
    # 行首偏移
    for r in range(rows):
        y = rows - 1 - r
        ax.text(-0.3, y + 0.45, f"{r*cols:04X}", ha="right", va="center",
                fontsize=8.5, color="#7F8C8D", family="monospace")
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "hex_dump.png"), dpi=110)
    plt.close()


# 图3（精确层）：不同 quality 下真实 .jpg 文件大小对比柱状图
def chart_quality_size():
    rows = load_verify()
    qs = [r[0] for r in rows]
    sizes = [r[1] for r in rows]
    fig, ax = plt.subplots(figsize=(9.0, 5.2))
    bars = ax.bar([str(q) for q in qs], sizes, color="#5499C7", width=0.6)
    for bar, s in zip(bars, sizes):
        ax.text(bar.get_x() + bar.get_width() / 2, s + 120,
                f"{s}B", ha="center", fontsize=10.5, fontweight="bold")
    ax.set_title("同一张图，quality 越高文件越大（真实编码测量，256×256）",
                 fontsize=12.5)
    ax.set_xlabel("quality 参数")
    ax.set_ylabel("输出 .jpg 文件大小（字节）")
    ax.set_ylim(0, max(sizes) * 1.18)
    ax.grid(axis="y", alpha=0.3)
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "quality_size.png"), dpi=110)
    plt.close()


# 图4（精确层）：原图 vs 解码回来的图 并排 + PSNR 曲线
def chart_before_after():
    orig = plt.imread(os.path.join(DATA, "orig.png"))
    dec = plt.imread(os.path.join(DATA, "decoded_q85.png"))
    rows = load_verify()
    # 找到 q85 的 PSNR
    psnr85 = next(p for q, s, p in rows if q == 85)

    fig, axes = plt.subplots(1, 3, figsize=(13.5, 4.8),
                             gridspec_kw={"width_ratios": [1, 1, 1.15]})
    axes[0].imshow(orig)
    axes[0].set_title("原始 PPM（编码前）", fontsize=11)
    axes[0].axis("off")
    axes[1].imshow(dec)
    axes[1].set_title(f"编码成 .jpg 再解码回来\n(quality=85, PSNR={psnr85:.1f} dB)",
                      fontsize=11)
    axes[1].axis("off")

    # 右图：PSNR 随 quality 变化曲线
    qs = [r[0] for r in rows]
    ps = [r[2] for r in rows]
    ax = axes[2]
    ax.plot(qs, ps, "o-", color="#7D3C98", lw=2, markersize=7)
    for q, p in zip(qs, ps):
        ax.text(q, p + 0.12, f"{p:.1f}", ha="center", fontsize=9,
                color="#5B2C6F")
    ax.set_title("PSNR 随 quality 上升\n（真实：编码→libjpeg 解码→比原图）",
                 fontsize=11)
    ax.set_xlabel("quality")
    ax.set_ylabel("PSNR (dB)")
    ax.grid(alpha=0.3)
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "before_after.png"), dpi=110)
    plt.close()


if __name__ == "__main__":
    chart_jfif_structure()
    chart_hex_dump()
    chart_quality_size()
    chart_before_after()
    print("charts generated in", HERE)
