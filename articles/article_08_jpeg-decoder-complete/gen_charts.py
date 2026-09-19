#!/usr/bin/env python3
"""A8 收官配图：读手搓解码器输出的真实 PPM，和 libjpeg(PIL) 解码结果逐像素对比。

数据来源（同目录 chart_data/，均由本任务真实跑出）：
  out_ours.ppm / out_ref.ppm   - 我们的解码 vs libjpeg 解码同一张 out.jpg(256x256)
  cam_ours.ppm / cam_ref.ppm   - 相机风格图 camera_photo.jpg(300x200) 同样对比
  original.ppm                 - 喂给 A5 编码器的原始 PPM（往返链的起点）

输出 5 张 PNG：
  compare_libjpeg.png   手搓 vs libjpeg 并排 + 差异放大图 + PSNR（精确层）
  chroma_upsample.png   4:2:0 半分辨率 Cb 放大对齐 Y 的示意（精确层）
  diff_histogram.png    逐像素差直方图（精确层）
  reconstruct.png       原图 -> 解码图 的重建效果（精确层）
  series_overview.png   JPEG 系列 A1-A8 编解码镜像总览（形象层，收尾总图）
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
DATA = os.path.join(HERE, "chart_data")


def load_ppm(name):
    """读 P6 PPM 成 HxWx3 的 uint8 数组（不依赖 PIL，纯手写解析）。"""
    with open(os.path.join(DATA, name), "rb") as f:
        raw = f.read()
    # 头：P6\n<w> <h>\n<maxval>\n<data>
    assert raw[:2] == b"P6", name
    idx = 2
    vals = []
    while len(vals) < 3:
        while idx < len(raw) and raw[idx] in b" \t\n\r":
            idx += 1
        if idx < len(raw) and raw[idx:idx + 1] == b"#":
            while idx < len(raw) and raw[idx] not in b"\n":
                idx += 1
            continue
        start = idx
        while idx < len(raw) and raw[idx] not in b" \t\n\r":
            idx += 1
        vals.append(int(raw[start:idx]))
    w, h, _ = vals
    idx += 1  # 单个空白符后即像素数据
    arr = np.frombuffer(raw[idx:idx + w * h * 3], dtype=np.uint8)
    return arr.reshape(h, w, 3).astype(np.int32)


def psnr(a, b):
    mse = np.mean((a.astype(np.float64) - b.astype(np.float64)) ** 2)
    if mse == 0:
        return float("inf")
    return 10 * np.log10(255.0 ** 2 / mse)


# 图1（精确层）：手搓 vs libjpeg 并排 + 差异放大图 + PSNR。
def chart_compare_libjpeg():
    ours = load_ppm("out_ours.ppm")
    ref = load_ppm("out_ref.ppm")
    p = psnr(ref, ours)
    diff = np.abs(ref - ours).max(axis=-1)  # 每像素三通道里最大的差
    maxd = int(diff.max())

    fig, axes = plt.subplots(1, 3, figsize=(14, 5))
    axes[0].imshow(ours.astype(np.uint8))
    axes[0].set_title("手搓解码器输出", fontsize=12)
    axes[1].imshow(ref.astype(np.uint8))
    axes[1].set_title("libjpeg / PIL 输出", fontsize=12)
    # 差异图放大 40 倍，让肉眼几乎看不见的差别显形。
    im = axes[2].imshow(np.clip(diff * 40, 0, 255), cmap="inferno")
    axes[2].set_title(f"逐像素差异（放大 40×）\n最大差仅 {maxd}/255",
                      fontsize=12)
    for ax in axes:
        ax.set_xticks([]); ax.set_yticks([])
    fig.colorbar(im, ax=axes[2], fraction=0.046, pad=0.04)
    fig.suptitle(f"同一张 out.jpg：手搓解码器 vs libjpeg，PSNR = {p:.1f} dB"
                 f"（差异全在色度上采样与 IDCT 取整）", fontsize=13.5)
    plt.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(HERE, "compare_libjpeg.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print(f"[chart] compare_libjpeg.png  PSNR={p:.2f} maxdiff={maxd}")


# 图2（精确层）：色度上采样示意——半分辨率 Cb 放大 2× 对齐 Y。
def chart_chroma_upsample():
    # 从解码结果里取一小块，模拟 4:2:0：Cb 是 Y 的一半分辨率。
    # 用 PIL 的 YCbCr 提取真实 Cb 平面，抽样成半分辨率再展示两种放大方式。
    ref = load_ppm("out_ref.ppm")
    # 转 YCbCr 取 Cb 通道（BT.601 full-range 近似即可，只为示意）。
    R, G, B = ref[..., 0], ref[..., 1], ref[..., 2]
    Cb = (-0.168736 * R - 0.331264 * G + 0.5 * B + 128)
    # 取左上 32x32 区域，box 抽样成 16x16（模拟 4:2:0 存储的半分辨率 Cb）。
    region = Cb[:32, :32]
    half = region.reshape(16, 2, 16, 2).mean(axis=(1, 3))  # 半分辨率
    # 两种上采样回 32x32。
    nn = np.repeat(np.repeat(half, 2, axis=0), 2, axis=1)  # 最近邻
    # 双线性：用 numpy 线性插值到 32 格。
    yy = (np.arange(32) + 0.5) / 2 - 0.5
    xx = (np.arange(32) + 0.5) / 2 - 0.5
    y0 = np.clip(np.floor(yy).astype(int), 0, 15)
    x0 = np.clip(np.floor(xx).astype(int), 0, 15)
    y1 = np.clip(y0 + 1, 0, 15); x1 = np.clip(x0 + 1, 0, 15)
    ty = np.clip(yy - y0, 0, 1)[:, None]; tx = np.clip(xx - x0, 0, 1)[None, :]
    a = half[np.ix_(y0, x0)]; b = half[np.ix_(y0, x1)]
    c = half[np.ix_(y1, x0)]; d = half[np.ix_(y1, x1)]
    bil = (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty

    fig, axes = plt.subplots(1, 3, figsize=(13, 4.6))
    for ax, mat, title in [
        (axes[0], half, "① 存储的半分辨率 Cb\n（16×16，Y 的四分之一）"),
        (axes[1], nn, "② 最近邻放大 2×\n（块状，色度边缘有台阶）"),
        (axes[2], bil, "③ 双线性放大 2×\n（平滑，更接近 libjpeg）"),
    ]:
        im = ax.imshow(mat, cmap="coolwarm", vmin=half.min(), vmax=half.max())
        ax.set_title(title, fontsize=10.5)
        ax.set_xticks([]); ax.set_yticks([])
    fig.suptitle("4:2:0 的坑：半分辨率 Cb 要放大回全分辨率才能和 Y 合成颜色",
                 fontsize=12.5)
    plt.tight_layout(rect=[0, 0, 1, 0.93])
    out = os.path.join(HERE, "chroma_upsample.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("[chart] chroma_upsample.png")


# 图3（精确层）：逐像素差异直方图——绝大多数像素差为 0 或 1。
def chart_diff_histogram():
    pairs = [("out_ours.ppm", "out_ref.ppm", "out.jpg (256×256, 4:2:0)"),
             ("cam_ours.ppm", "cam_ref.ppm", "camera_photo.jpg (300×200)")]
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.4))
    for ax, (a, b, label) in zip(axes, pairs):
        oa, ob = load_ppm(a), load_ppm(b)
        d = np.abs(oa - ob).ravel()
        maxd = int(d.max())
        bins = np.arange(0, max(maxd + 2, 4))
        counts, _ = np.histogram(d, bins=bins)
        pct = counts / counts.sum() * 100
        ax.bar(bins[:-1], pct, width=0.8, color="#2a6fb0")
        for i, v in enumerate(pct):
            if v > 1:
                ax.text(bins[i], v + 1, f"{v:.1f}%", ha="center", fontsize=8)
        ax.set_title(f"{label}\nPSNR={psnr(oa, ob):.1f} dB，最大差={maxd}",
                     fontsize=10.5)
        ax.set_xlabel("逐通道绝对差（像素值 0-255）")
        ax.set_ylabel("占比 %")
        ax.set_ylim(0, 100)
        ax.set_xticks(bins[:-1])
    fig.suptitle("逐像素差异直方图：九成以上的像素和 libjpeg 完全一样，"
                 "差的也只差 1-2", fontsize=12.5)
    plt.tight_layout(rect=[0, 0, 1, 0.93])
    out = os.path.join(HERE, "diff_histogram.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("[chart] diff_histogram.png")


# 图4（精确层）：完整重建效果——原图 -> 编码 -> 解码。
def chart_reconstruct():
    orig = load_ppm("original.ppm")
    ours = load_ppm("out_ours.ppm")
    p = psnr(orig, ours)
    diff = np.abs(orig - ours).mean(axis=-1)
    fig, axes = plt.subplots(1, 3, figsize=(14, 5))
    axes[0].imshow(orig.astype(np.uint8))
    axes[0].set_title("原始 PPM（编码器输入）", fontsize=12)
    axes[1].imshow(ours.astype(np.uint8))
    axes[1].set_title("A5 编码 → A8 解码后", fontsize=12)
    im = axes[2].imshow(diff, cmap="magma")
    axes[2].set_title(f"往返差异（含量化损失）\nPSNR = {p:.1f} dB", fontsize=12)
    for ax in axes:
        ax.set_xticks([]); ax.set_yticks([])
    fig.colorbar(im, ax=axes[2], fraction=0.046, pad=0.04)
    fig.suptitle("完整闭环：一张图进 A5 编码器，出 A8 解码器，"
                 "差异来自 JPEG 有损压缩本身（量化）", fontsize=13)
    plt.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(HERE, "reconstruct.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print(f"[chart] reconstruct.png  round-trip PSNR={p:.2f}")


# 图5（形象层）：JPEG 系列 A1-A8 编码/解码镜像总览（收尾总图）。
def chart_series_overview():
    from matplotlib.patches import FancyBboxPatch, FancyArrowPatch
    fig, ax = plt.subplots(figsize=(13, 6.5))
    ax.set_xlim(0, 12); ax.set_ylim(0, 8); ax.axis("off")

    enc = [("A1 色彩变换", "RGB→YCbCr\n4:2:0 抽样"),
           ("A2 DCT", "8×8 频域"),
           ("A3 量化", "除量化表\n丢高频"),
           ("A4 熵编码", "Zig-Zag+游程\n+霍夫曼"),
           ("A5 JFIF 封装", "写 marker\n出 .jpg")]
    dec = [("A6 解析", "读 marker\n重建表"),
           ("A7 反变换", "反量化\n+IDCT 出块"),
           ("A8 整机", "逐MCU+上采样\n+YCbCr→RGB")]

    def box(x, y, w, h, title, sub, color):
        p = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.04,rounding_size=0.12",
                           linewidth=1.4, edgecolor="#333",
                           facecolor=color, alpha=0.92)
        ax.add_patch(p)
        ax.text(x + w / 2, y + h * 0.66, title, ha="center", va="center",
                fontsize=10.5, fontweight="bold")
        ax.text(x + w / 2, y + h * 0.28, sub, ha="center", va="center",
                fontsize=8.2, color="#222")

    # 编码链（上排，从左到右）
    for i, (t, s) in enumerate(enc):
        box(0.3 + i * 2.34, 5.2, 2.0, 1.5, t, s, "#cfe3f7")
    # 解码链（下排，从右到左，体现"镜像/反向"）
    for i, (t, s) in enumerate(dec):
        box(2.6 + i * 2.34, 1.1, 2.0, 1.5, t, s, "#d9f0da")

    # 编码横向箭头
    for i in range(len(enc) - 1):
        ax.add_patch(FancyArrowPatch(
            (0.3 + i * 2.34 + 2.0, 5.95), (0.3 + (i + 1) * 2.34, 5.95),
            arrowstyle="-|>", mutation_scale=16, color="#2a6fb0", lw=1.6))
    # 解码横向箭头（向左）
    for i in range(len(dec) - 1):
        ax.add_patch(FancyArrowPatch(
            (2.6 + (i + 1) * 2.34, 1.85), (2.6 + i * 2.34 + 2.0, 1.85),
            arrowstyle="-|>", mutation_scale=16, color="#2f8f3f", lw=1.6))

    # 中间的 .jpg 文件（编码终点 = 解码起点）
    box(9.4, 3.0, 2.2, 1.5, ".jpg 文件", "字节流\n(可被任意查看器打开)", "#ffe6b0")
    ax.add_patch(FancyArrowPatch((11.0, 5.2), (10.5, 4.5), arrowstyle="-|>",
                 mutation_scale=16, color="#2a6fb0", lw=1.6))
    ax.add_patch(FancyArrowPatch((10.5, 3.0), (9.6, 2.6), arrowstyle="-|>",
                 mutation_scale=16, color="#2f8f3f", lw=1.6))
    # 起点/终点 PPM
    box(0.3, 3.0, 1.9, 1.5, "原始像素", "PPM / RGB", "#eeeeee")
    ax.add_patch(FancyArrowPatch((1.25, 4.5), (1.3, 5.2), arrowstyle="-|>",
                 mutation_scale=16, color="#2a6fb0", lw=1.6))
    ax.add_patch(FancyArrowPatch((2.6, 1.85), (2.2, 3.0), arrowstyle="-|>",
                 mutation_scale=16, color="#2f8f3f", lw=1.6))

    ax.text(6, 7.4, "手搓 JPEG 全系列：编码 5 篇（→）与解码 3 篇（←）互为镜像，"
            "PPM ↔ .jpg 双向跑通，零第三方库",
            ha="center", fontsize=13, fontweight="bold")
    ax.text(1.25, 5.95, "编码", ha="center", fontsize=9, color="#2a6fb0")
    ax.text(1.25, 1.85, "解码", ha="center", fontsize=9, color="#2f8f3f")
    out = os.path.join(HERE, "series_overview.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("[chart] series_overview.png")


if __name__ == "__main__":
    chart_compare_libjpeg()
    chart_chroma_upsample()
    chart_diff_histogram()
    chart_reconstruct()
    chart_series_overview()
    print("done.")
