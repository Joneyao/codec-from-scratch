#!/usr/bin/env python3
"""C5 H.264 整数变换 + 反量化配图。

数据来源全部为 codec-from-scratch/h264/decoder 真实跑出的结果：
  third-party/codec-from-scratch/h264/decoder/chart_data/transform_dump.txt
不手编数字。
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
DUMP = os.path.join(REPO, "h264/decoder/chart_data/transform_dump.txt")

C_BLUE = "#2563eb"
C_GREEN = "#16a34a"
C_ORANGE = "#ea580c"
C_RED = "#dc2626"
C_GRAY = "#94a3b8"
C_PURPLE = "#7c3aed"


def load_dump():
    """把 transform_dump.txt 解析成一个 dict。"""
    d = {"blocks": {}, "dequant_qp": {}, "scalar": {}}
    with open(DUMP) as f:
        lines = [ln.rstrip("\n") for ln in f]
    i = 0

    def read_block(start):
        rows = []
        for k in range(start, start + 4):
            rows.append([int(x) for x in lines[k].split()])
        return np.array(rows, dtype=int), start + 4

    while i < len(lines):
        ln = lines[i]
        if not ln or ln.startswith("#"):
            i += 1
            continue
        parts = ln.split()
        tag = parts[0]
        if tag in ("residual", "forward_W", "quant_c", "dequant_d", "recon_r"):
            blk, i = read_block(i + 1)
            d["blocks"][tag] = blk
        elif tag == "dequant_qp":
            qp = int(parts[1])
            dc = int(parts[2])
            blk, i = read_block(i + 1)
            d["dequant_qp"][qp] = {"dc": dc, "block": blk}
        elif tag in ("qP", "max_err", "int_identical"):
            d["scalar"][tag] = int(parts[1])
            i += 1
        elif tag == "fp_max_err":
            d["scalar"][tag] = float(parts[1])
            i += 1
        else:
            i += 1
    return d


def sync_stage_map():
    import sys
    sys.path.insert(0, os.path.join(HERE, ".."))
    from pipeline_map import draw_h264_decode_map
    draw_h264_decode_map("transform", os.path.join(HERE, "stage_map.png"))
    print("stage_map.png done")


def chart_matrix_compare():
    """精确层①：H.264 整数变换矩阵（只含 ±1/±2/±1/2）对比标准 4 点 DCT 矩阵
    （满是无理数 cos）。左整数、右无理数，一眼看出为什么整数版能钉死一致。"""
    # H.264 4x4 核变换（前向核 Cf，spec 8.6 对偶），全整数：
    cf = np.array([[1, 1, 1, 1],
                   [2, 1, -1, -2],
                   [1, -1, -1, 1],
                   [1, -2, 2, -1]], dtype=int)
    # 标准 4 点 DCT-II 矩阵：a=1/2, b=cos(pi/8)/sqrt(2), c=cos(3pi/8)/sqrt(2)
    a = 0.5
    b = np.cos(np.pi / 8) / np.sqrt(2)
    c = np.cos(3 * np.pi / 8) / np.sqrt(2)
    dct = np.array([[a, a, a, a],
                    [b, c, -c, -b],
                    [a, -a, -a, a],
                    [c, -b, b, -c]])

    fig, (axl, axr) = plt.subplots(1, 2, figsize=(11, 4.6))

    def draw_grid(ax, mat, texts, title, hi_int):
        ax.set_xlim(0, 4)
        ax.set_ylim(0, 4)
        ax.invert_yaxis()
        ax.axis("off")
        for r in range(4):
            for col in range(4):
                fc = "#dbeafe" if hi_int else "#fde8e8"
                ax.add_patch(plt.Rectangle((col, r), 1, 1, facecolor=fc,
                                           edgecolor="white", linewidth=2))
                ax.text(col + 0.5, r + 0.5, texts[r][col], ha="center",
                        va="center", fontsize=13 if hi_int else 11,
                        fontweight="bold" if hi_int else "normal",
                        color="#1e293b")
        ax.set_title(title, fontsize=12.5, fontweight="bold", pad=12)

    int_txt = [[str(v) for v in row] for row in cf]
    dct_txt = [[f"{v:+.3f}" for v in row] for row in dct]
    draw_grid(axl, cf, int_txt, "H.264 整数变换矩阵\n只含 ±1 ±2：加法+移位就能算",
              True)
    draw_grid(axr, dct, dct_txt, "标准 DCT 矩阵\n满是无理数 cos：必须浮点乘法",
              False)

    fig.text(0.5, 0.03,
             "左：整数矩阵在任何 CPU 上算出完全相同的结果；"
             "右：无理数系数只能近似存成浮点，不同硬件可能有微小差异",
             ha="center", fontsize=10, color=C_ORANGE)
    plt.tight_layout(rect=[0, 0.05, 1, 1])
    out = os.path.join(HERE, "matrix_compare.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("matrix_compare.png done")


def _heatmap(ax, mat, title, cmap, vmax=None, fmt="{:d}"):
    a = np.array(mat)
    vmax = vmax if vmax is not None else max(1, np.abs(a).max())
    ax.imshow(np.abs(a), cmap=cmap, vmin=0, vmax=vmax)
    for i in range(a.shape[0]):
        for j in range(a.shape[1]):
            v = a[i, j]
            ax.text(j, i, fmt.format(v), ha="center", va="center",
                    fontsize=11,
                    color="white" if abs(v) > vmax * 0.55 else "#1e293b",
                    fontweight="bold")
    ax.set_xticks([]); ax.set_yticks([])
    ax.set_title(title, fontsize=11.5, fontweight="bold")


def chart_relay():
    """精确层②：量化系数 -> 反量化 -> 反变换残差 的接力矩阵（真实数字）。"""
    d = load_dump()
    c = d["blocks"]["quant_c"]
    dq = d["blocks"]["dequant_d"]
    r = d["blocks"]["recon_r"]
    qp = d["scalar"]["qP"]
    max_err = d["scalar"]["max_err"]

    fig, axes = plt.subplots(1, 3, figsize=(11.5, 4.2))
    _heatmap(axes[0], c, "① 量化系数 c\n（码流里解出来的）", "Blues")
    _heatmap(axes[1], dq, f"② 反量化 d = Scaling(c, QP={qp})\n"
             "每个系数乘缩放因子再左移", "Greens")
    _heatmap(axes[2], r, "③ 4x4 整数反变换后\n重建残差 r", "Oranges")

    for ax, arr in zip(axes, [c, dq, r]):
        pass
    # 箭头连接
    fig.text(0.365, 0.5, "→", ha="center", va="center", fontsize=26,
             color=C_GRAY)
    fig.text(0.635, 0.5, "→", ha="center", va="center", fontsize=26,
             color=C_GRAY)
    fig.suptitle("残差重建接力：稀疏的量化系数 → 反量化放大 → 反变换成残差块",
                 fontsize=13, fontweight="bold", y=1.02)
    fig.text(0.5, -0.02,
             f"重建残差与原残差最大逐点误差仅 {max_err}"
             "（这点误差来自量化，不是变换——反变换本身无损）",
             ha="center", fontsize=10, color=C_ORANGE)
    plt.tight_layout()
    out = os.path.join(HERE, "relay.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("relay.png done")


def chart_qp_doubling():
    """精确层③：同一系数在 QP=22/28/34 反量化出的值，展示 QP+6 翻倍。"""
    d = load_dump()
    qps = sorted(d["dequant_qp"].keys())
    dcs = [d["dequant_qp"][q]["dc"] for q in qps]
    c_dc = d["blocks"]["quant_c"][0][0]

    fig, ax = plt.subplots(figsize=(8.4, 4.4))
    xs = np.arange(len(qps))
    bars = ax.bar(xs, dcs, width=0.55, color=[C_GREEN, C_BLUE, C_ORANGE],
                  zorder=3)
    ax.set_xticks(xs)
    ax.set_xticklabels([f"QP={q}" for q in qps], fontsize=11)
    ax.set_ylabel("反量化后 DC 系数值 d[0][0]")
    ax.set_title(f"QP 每 +6，反量化值翻倍（同一量化系数 c[0][0]={c_dc}）",
                 fontweight="bold", fontsize=12.5)
    ax.set_ylim(0, max(dcs) * 1.2)
    ax.grid(axis="y", alpha=0.3, zorder=0)
    for x, v in zip(xs, dcs):
        ax.text(x, v + max(dcs) * 0.02, f"{v}", ha="center", va="bottom",
                fontsize=11, fontweight="bold", color="#1e293b")
    # 翻倍标注
    for k in range(len(qps) - 1):
        ratio = dcs[k + 1] / dcs[k]
        ax.annotate(f"×{ratio:.0f}", xy=(k + 0.5, (dcs[k] + dcs[k + 1]) / 2),
                    ha="center", va="center", fontsize=13, color=C_RED,
                    fontweight="bold")
    plt.tight_layout()
    out = os.path.join(HERE, "qp_doubling.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("qp_doubling.png done  (%s -> %s)" % (qps, dcs))


def chart_int_vs_float():
    """精确层④：整数变换完全确定，浮点 DCT 往返有微小舍入痕迹。"""
    d = load_dump()
    identical = d["scalar"]["int_identical"]
    fp_err = d["scalar"]["fp_max_err"]

    fig, (axl, axr) = plt.subplots(1, 2, figsize=(10.5, 4.2))

    # 左：整数路径——三次运行结果完全相同（画三条重叠的“0 误差”）
    axl.axhline(0, color=C_GREEN, linewidth=3, zorder=3)
    axl.scatter([1, 2, 3], [0, 0, 0], s=140, color=C_GREEN, zorder=4)
    axl.set_ylim(-1e-14, 3e-14)
    axl.set_xlim(0.5, 3.5)
    axl.set_xticks([1, 2, 3])
    axl.set_xticklabels(["第1次", "第2次", "第3次"])
    axl.set_ylabel("与首次运行的偏差")
    axl.set_title("H.264 整数反变换\n重复运行：偏差恒为 0（逐比特一致）",
                  fontsize=12, fontweight="bold", color=C_GREEN)
    axl.text(2, 1.4e-14,
             "逐比特一致：%s" % ("是" if identical else "否"),
             ha="center", fontsize=11, color=C_GREEN, fontweight="bold")
    axl.grid(alpha=0.3)

    # 右：浮点路径——往返有非零舍入误差（对数尺度突出“非零”）
    axr.bar([1], [fp_err], width=0.4, color=C_RED, zorder=3)
    axr.set_yscale("log")
    axr.set_ylim(1e-17, 1e-12)
    axr.set_xlim(0.4, 1.6)
    axr.set_xticks([1])
    axr.set_xticklabels(["浮点 DCT 往返"])
    axr.set_ylabel("最大逐点误差（对数轴）")
    axr.set_title("浮点 DCT 往返\n误差非零：舍入无法根除",
                  fontsize=12, fontweight="bold", color=C_RED)
    axr.text(1, fp_err * 2, f"{fp_err:.1e}", ha="center", va="bottom",
             fontsize=11, color=C_RED, fontweight="bold")
    axr.grid(axis="y", alpha=0.3)

    fig.suptitle("同一残差块：整数变换钉死，浮点变换总带一丝舍入尾巴",
                 fontsize=13, fontweight="bold", y=1.02)
    plt.tight_layout()
    out = os.path.join(HERE, "int_vs_float.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("int_vs_float.png done  (fp_err=%.2e)" % fp_err)


def chart_drift_illustration():
    """形象层：为什么改用整数变换。
    上：浮点 DCT——编码端与解码端各算各的，微小舍入差异在预测环路里累积成漂移。
    下：整数变换——两端算出完全相同的残差，环路里永不漂移。"""
    fig, (axt, axb) = plt.subplots(2, 1, figsize=(10, 6.2))

    def box(ax, x, y, w, h, text, fc, ec="#1e293b", tc="#1e293b", fs=10):
        ax.add_patch(plt.Rectangle((x, y), w, h, facecolor=fc, edgecolor=ec,
                                   linewidth=1.6, zorder=3))
        ax.text(x + w / 2, y + h / 2, text, ha="center", va="center",
                fontsize=fs, fontweight="bold", color=tc, zorder=4)

    def arrow(ax, x1, y1, x2, y2, color, lw=2, style="->"):
        ax.annotate("", xy=(x2, y2), xytext=(x1, y1),
                    arrowprops=dict(arrowstyle=style, color=color, lw=lw))

    # ---- 上：浮点 DCT 漂移 ----
    axt.set_xlim(0, 10)
    axt.set_ylim(0, 3)
    axt.axis("off")
    axt.set_title("浮点 DCT：编解码两端各算各的，误差在预测环路里越滚越大（drift）",
                  fontsize=11.5, fontweight="bold", color=C_RED)
    box(axt, 0.3, 1.1, 1.9, 0.9, "编码端\n浮点变换", "#fde8e8", tc=C_RED)
    box(axt, 3.0, 1.1, 1.9, 0.9, "解码端\n浮点变换", "#fde8e8", tc=C_RED)
    arrow(axt, 2.2, 1.55, 3.0, 1.55, C_GRAY)
    axt.text(2.6, 1.85, "码流", ha="center", fontsize=8.5, color="#64748b")
    box(axt, 5.7, 1.1, 2.0, 0.9, "两端残差\n差一点点", "#fee2e2", tc=C_RED)
    arrow(axt, 4.9, 1.55, 5.7, 1.55, C_RED)
    box(axt, 8.0, 1.1, 1.7, 0.9, "预测环路\n累积漂移", "#fca5a5", tc="#7f1d1d")
    arrow(axt, 7.7, 1.55, 8.0, 1.55, C_RED, lw=2.4)
    # 反馈回环箭头
    arrow(axt, 8.85, 1.1, 8.85, 0.5, C_RED, lw=1.8)
    arrow(axt, 8.85, 0.5, 1.25, 0.5, C_RED, lw=1.8)
    arrow(axt, 1.25, 0.5, 1.25, 1.1, C_RED, lw=1.8)
    axt.text(5, 0.32, "误差反馈进下一帧预测 → 画面逐渐跑偏", ha="center",
             fontsize=9, color=C_RED)

    # ---- 下：整数变换钉死 ----
    axb.set_xlim(0, 10)
    axb.set_ylim(0, 3)
    axb.axis("off")
    axb.set_title("H.264 整数变换：两端逐比特一致，残差完全相同，环路永不漂移",
                  fontsize=11.5, fontweight="bold", color=C_GREEN)
    box(axb, 0.3, 1.1, 1.9, 0.9, "编码端\n整数变换", "#dcfce7", tc=C_GREEN)
    box(axb, 3.0, 1.1, 1.9, 0.9, "解码端\n整数变换", "#dcfce7", tc=C_GREEN)
    arrow(axb, 2.2, 1.55, 3.0, 1.55, C_GRAY)
    axb.text(2.6, 1.85, "码流", ha="center", fontsize=8.5, color="#64748b")
    box(axb, 5.7, 1.1, 2.0, 0.9, "两端残差\n完全相同", "#bbf7d0", tc=C_GREEN)
    arrow(axb, 4.9, 1.55, 5.7, 1.55, C_GREEN)
    box(axb, 8.0, 1.1, 1.7, 0.9, "预测环路\n零漂移", "#86efac", tc="#14532d")
    arrow(axb, 7.7, 1.55, 8.0, 1.55, C_GREEN, lw=2.4)
    arrow(axb, 8.85, 1.1, 8.85, 0.5, C_GREEN, lw=1.8)
    arrow(axb, 8.85, 0.5, 1.25, 0.5, C_GREEN, lw=1.8)
    arrow(axb, 1.25, 0.5, 1.25, 1.1, C_GREEN, lw=1.8)
    axb.text(5, 0.32, "残差每一帧都对得上 → 画面稳稳重建", ha="center",
             fontsize=9, color=C_GREEN)

    plt.tight_layout()
    out = os.path.join(HERE, "why_integer.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("why_integer.png done")


if __name__ == "__main__":
    sync_stage_map()
    chart_matrix_compare()
    chart_relay()
    chart_qp_doubling()
    chart_int_vs_float()
    chart_drift_illustration()
    print("all charts done")
