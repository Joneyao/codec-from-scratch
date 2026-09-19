#!/usr/bin/env python3
"""A7 配图：读取解码器 demo/test 导出的真实数据绘图。

数据来源：third-party/codec-from-scratch/jpeg/decoder/chart_data/
  stage_quant_camera.txt   - 熵解码后、反 Zig-Zag 摆回 8x8 的量化系数
  stage_dequant_camera.txt - 反量化后的系数
  stage_pixels_camera.txt  - IDCT 后的像素
  zigzag_coeffs_camera.txt - 一维 zigzag 系数（64 个）
  roundtrip_original.txt / roundtrip_recon.txt / roundtrip_diff.txt - 往返验证
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
DATA = os.path.join(REPO, "jpeg/decoder/chart_data")


def load(name):
    return np.loadtxt(os.path.join(DATA, name))


def annotate(ax, mat, thresh_ratio=0.55, fmt="{:.0f}"):
    amax = np.abs(mat).max() or 1
    for (i, j), v in np.ndenumerate(mat):
        big = abs(v) > amax * thresh_ratio
        ax.text(j, i, fmt.format(v), ha="center", va="center",
                fontsize=7.5, color="white" if big else "black")


# 图1（精确层）：解码全流程 8x8 矩阵接力——数字一步步变回像素
def chart_pipeline_relay():
    quant = load("stage_quant_camera.txt")
    dequant = load("stage_dequant_camera.txt")
    pixels = load("stage_pixels_camera.txt")

    fig, axes = plt.subplots(1, 3, figsize=(13, 4.6))
    panels = [
        (quant, "① 熵解码后的量化系数\n（反 Zig-Zag 摆回 8x8）", "PuBu"),
        (dequant, "② 反量化后\n（系数 × 量化步长）", "BuPu"),
        (pixels, "③ IDCT 后的像素\n（+128、clamp 到 0-255）", "gray"),
    ]
    for ax, (mat, title, cmap) in zip(axes, panels):
        im = ax.imshow(mat, cmap=cmap)
        annotate(ax, mat)
        ax.set_title(title, fontsize=10.5)
        ax.set_xticks([]); ax.set_yticks([])
    fig.suptitle("解码后半段：一个块的 64 个数字，一步步变回 8×8 像素"
                 "（真实相机 .jpg 的第一个亮度块）", fontsize=12.5)
    plt.tight_layout(rect=[0, 0, 1, 0.94])
    plt.savefig(os.path.join(HERE, "pipeline_relay.png"), dpi=110)
    plt.close()


# 图2（精确层）：比特流 → (category/run,size) → 系数 的解码示意
def chart_bitstream_decode():
    from matplotlib.patches import FancyBboxPatch, FancyArrowPatch
    fig, ax = plt.subplots(figsize=(11.5, 5.2))
    ax.set_xlim(0, 12); ax.set_ylim(0, 6); ax.axis("off")
    ax.text(6, 5.6, "变长解码：读一点，才知道下一步读多少", ha="center",
            fontsize=13, fontweight="bold")

    # 比特流（示意，真实 DC 码用标准亮度 DC 表：category=6 的码字是 '1110'）。
    bits = "1110" + "110001" + "..."
    ax.text(6, 4.8, "熵数据比特流：", ha="center", fontsize=10, color="#555")
    xs = 1.2
    seg_colors = ["#2E86C1"] * 4 + ["#E74C3C"] * 6 + ["#95A5A6"] * 3
    for i, ch in enumerate(bits):
        c = seg_colors[i] if i < len(seg_colors) else "#95A5A6"
        ax.text(xs + i * 0.42, 4.25, ch, ha="center", fontsize=13,
                family="monospace", color=c, fontweight="bold")

    def box(x, y, w, h, text, fc, ec):
        ax.add_patch(FancyBboxPatch((x, y), w, h,
                     boxstyle="round,pad=0.04,rounding_size=0.08",
                     fc=fc, ec=ec, lw=1.6))
        ax.text(x + w / 2, y + h / 2, text, ha="center", va="center",
                fontsize=9.2)

    def arrow(x1, y1, x2, y2):
        ax.add_patch(FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="-|>",
                     mutation_scale=13, color="#666", lw=1.5))

    box(0.6, 2.7, 3.1, 1.0,
        "① 逐位读 '1110'\n在 DC 表里凑成合法码字\n→ 符号 = category 6",
        "#D6EAF8", "#2E86C1")
    box(4.4, 2.7, 3.1, 1.0,
        "② 再读 6 位幅值 '110001'\n最高位为 1 → 正数\n→ 值 = 49",
        "#FADBD8", "#E74C3C")
    box(8.3, 2.7, 3.1, 1.0,
        "③ 差分还原\nDC = 上块DC + 49\n→ 填入系数[0]",
        "#D5F5E3", "#27AE60")
    arrow(3.7, 3.2, 4.4, 3.2)
    arrow(7.5, 3.2, 8.3, 3.2)

    ax.text(6, 1.7,
            "AC 系数同理：先解 (run, size) 一个符号，跳过 run 个 0，"
            "再读 size 位幅值。遇 EOB 提前收尾。",
            ha="center", fontsize=9.6, color="#333")
    ax.text(6, 1.1,
            "陷阱：size 位幅值最高位为 0 时是负数，要按 value = bits − 2^size + 1 还原"
            "（T.81 F.2.2.1）。",
            ha="center", fontsize=9.6, color="#C0392B")
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "bitstream_decode.png"), dpi=110)
    plt.close()


# 图3（精确层）：往返验证的误差热力图（关键证据）
def chart_roundtrip_error():
    orig = load("roundtrip_original.txt")
    recon = load("roundtrip_recon.txt")
    diff = load("roundtrip_diff.txt")

    fig, axes = plt.subplots(1, 3, figsize=(13, 4.5))
    im0 = axes[0].imshow(orig, cmap="gray", vmin=0, vmax=255)
    annotate(axes[0], orig)
    axes[0].set_title("原始像素块（编码前）", fontsize=10.5)

    im1 = axes[1].imshow(recon, cmap="gray", vmin=0, vmax=255)
    annotate(axes[1], recon)
    axes[1].set_title("解码还原后的像素块", fontsize=10.5)

    vmax = max(abs(diff.min()), abs(diff.max())) or 1
    im2 = axes[2].imshow(diff, cmap="coolwarm", vmin=-vmax, vmax=vmax)
    annotate(axes[2], diff, thresh_ratio=2.0)  # 差值小，文字统一黑色
    axes[2].set_title("逐像素差（还原 − 原始）", fontsize=10.5)
    fig.colorbar(im2, ax=axes[2], fraction=0.046, pad=0.04)

    for ax in axes:
        ax.set_xticks([]); ax.set_yticks([])
    fig.suptitle("端到端往返验证：编码→解码后，误差全落在量化引入的范围内"
                 "（最大逐像素差 6，RMSE 3.56）", fontsize=12)
    plt.tight_layout(rect=[0, 0, 1, 0.94])
    plt.savefig(os.path.join(HERE, "roundtrip_error.png"), dpi=110)
    plt.close()


# 图4（形象层）：解码流水线，与编码流水线镜像
def chart_decode_pipeline():
    from matplotlib.patches import FancyBboxPatch, FancyArrowPatch
    fig, ax = plt.subplots(figsize=(12, 4.2))
    ax.set_xlim(0, 13); ax.set_ylim(0, 4); ax.axis("off")
    ax.text(6.5, 3.5, "解码流水线：编码流水线的镜像，每一步都是逆运算",
            ha="center", fontsize=13, fontweight="bold")

    steps = [
        ("熵编码\n比特流", "#EBDEF0", "#7D3C98"),
        ("霍夫曼\n解码", "#D6EAF8", "#2E86C1"),
        ("反量化", "#FADBD8", "#E74C3C"),
        ("反\nZig-Zag", "#FCF3CF", "#B7950B"),
        ("IDCT\n(+128)", "#D5F5E3", "#27AE60"),
        ("8×8\n像素块", "#FDEBD0", "#CA6F1E"),
    ]
    w = 1.7
    gap = 0.42
    x = 0.4
    centers = []
    for text, fc, ec in steps:
        ax.add_patch(FancyBboxPatch((x, 1.5), w, 1.2,
                     boxstyle="round,pad=0.05,rounding_size=0.1",
                     fc=fc, ec=ec, lw=1.8))
        ax.text(x + w / 2, 2.1, text, ha="center", va="center", fontsize=9.8)
        centers.append(x + w / 2)
        x += w + gap
    for i in range(len(steps) - 1):
        ax.add_patch(FancyArrowPatch(
            (centers[i] + w / 2, 2.1), (centers[i + 1] - w / 2, 2.1),
            arrowstyle="-|>", mutation_scale=14, color="#666", lw=1.6))

    ax.text(6.5, 0.75,
            "对照编码：DCT→量化→Zig-Zag→游程→霍夫曼编码。"
            "解码把这条链反着走了一遍。",
            ha="center", fontsize=10, color="#444")
    plt.tight_layout()
    plt.savefig(os.path.join(HERE, "decode_pipeline.png"), dpi=110)
    plt.close()


if __name__ == "__main__":
    chart_pipeline_relay()
    chart_bitstream_decode()
    chart_roundtrip_error()
    chart_decode_pipeline()
    print("charts generated in", HERE)
