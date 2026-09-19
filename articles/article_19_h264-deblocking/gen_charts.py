#!/usr/bin/env python3
"""C8 H.264 去块滤波（in-loop deblocking）配图。

数据来源全部为 codec-from-scratch/h264/decoder 真实跑出的结果：
  third-party/codec-from-scratch/h264/decoder/chart_data/deblock_dump.txt
不手编数字。重建行取自真实测试图 camera_photo.ppm 的亮度平面 + 人为块效应台阶。
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
DUMP = os.path.join(REPO, "h264/decoder/chart_data/deblock_dump.txt")

C_BLUE = "#2563eb"
C_GREEN = "#16a34a"
C_ORANGE = "#ea580c"
C_RED = "#dc2626"
C_GRAY = "#94a3b8"
C_PURPLE = "#7c3aed"


def load_dump():
    d = {"edges": {}, "scalar": {}, "curve": {}, "bs": {}, "row": {}}
    for ln in open(DUMP):
        ln = ln.rstrip("\n").strip()
        if not ln or ln.startswith("#"):
            continue
        p = ln.split()
        tag = p[0]
        if tag == "qp":
            d["scalar"]["qp"] = int(p[1])
            d["scalar"]["alpha"] = int(p[3])
            d["scalar"]["beta"] = int(p[5])
            d["scalar"]["bs"] = int(p[7])
        elif tag in ("orig", "recon", "filt", "edge_in", "edge_out"):
            d["edges"][tag] = [int(x) for x in p[1:9]]
        elif tag in ("flag1", "flag2"):
            d["scalar"][tag] = int(p[1])
        elif tag == "alpha_curve":
            d["curve"]["alpha"] = [int(x) for x in p[1:]]
        elif tag == "beta_curve":
            d["curve"]["beta"] = [int(x) for x in p[1:]]
        elif tag in ("bs1", "bs2", "bs3", "bs4"):
            d["bs"][tag] = (int(p[2]), int(p[4]))
        elif tag in ("recon_row", "orig_row"):
            d["row"][tag] = [int(x) for x in p[1:]]
    return d


def sync_stage_map():
    import sys
    sys.path.insert(0, os.path.join(HERE, ".."))
    from pipeline_map import draw_h264_decode_map
    draw_h264_decode_map("deblock", os.path.join(HERE, "stage_map.png"))
    print("stage_map.png done")


def chart_edge_before_after():
    """精确层①：一条块效应边界，滤波前有台阶、滤波后被磨平（标真实像素值）。"""
    d = load_dump()
    recon = d["edges"]["recon"]   # p3..p0 q0..q3（带块效应）
    filt = d["edges"]["filt"]     # 滤波后
    orig = d["edges"]["orig"]     # 无块效应真值（参考）
    labels = ["p3", "p2", "p1", "p0", "q0", "q1", "q2", "q3"]
    xs = np.arange(8)

    fig, ax = plt.subplots(figsize=(9.8, 5.2))
    ax.plot(xs, recon, "-o", color=C_RED, lw=2.2, ms=8,
            label="滤波前（带块效应）")
    ax.plot(xs, filt, "-o", color=C_GREEN, lw=2.4, ms=8,
            label="滤波后（磨平）")
    ax.plot(xs, orig, "--", color=C_GRAY, lw=1.6, label="无块效应真值（参考）")

    # 边界位置（p0|q0 之间）竖线。
    ax.axvline(3.5, color=C_BLUE, lw=1.4, ls=":")
    ax.text(3.5, max(recon) + 3, "块边界", ha="center", color=C_BLUE,
            fontsize=10, fontweight="bold")

    # 标注滤波前后 p0/q0 的台阶。
    for i in (3, 4):
        ax.annotate(str(recon[i]), (i, recon[i]), textcoords="offset points",
                    xytext=(0, 10), ha="center", fontsize=9.5, color=C_RED)
        ax.annotate(str(filt[i]), (i, filt[i]), textcoords="offset points",
                    xytext=(0, -16), ha="center", fontsize=9.5, color=C_GREEN)

    step_b = abs(recon[3] - recon[4])
    step_a = abs(filt[3] - filt[4])
    ax.set_xticks(xs)
    ax.set_xticklabels(labels, fontsize=10)
    ax.set_ylabel("亮度像素值")
    ax.set_title(
        f"块效应边界：跨界台阶 |p0−q0| 从 {step_b} 磨到 {step_a}"
        f"（QP={d['scalar']['qp']}, bS={d['scalar']['bs']}）",
        fontsize=12.5, fontweight="bold")
    ax.legend(loc="lower right", fontsize=9.5)
    ax.grid(alpha=0.3)
    plt.tight_layout()
    out = os.path.join(HERE, "edge_before_after.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("edge_before_after.png done  step %d -> %d" % (step_b, step_a))


def chart_preserve_vs_smooth():
    """精确层②：保边 vs 平滑——真边缘不动、块效应被抹平（同一 α/β 阈值）。"""
    d = load_dump()
    ei = d["edges"]["edge_in"]    # 真边缘输入
    eo = d["edges"]["edge_out"]   # 真边缘输出（应不变）
    recon = d["edges"]["recon"]
    filt = d["edges"]["filt"]
    alpha = d["scalar"]["alpha"]
    labels = ["p3", "p2", "p1", "p0", "q0", "q1", "q2", "q3"]
    xs = np.arange(8)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11.0, 4.8))

    # 左：真边缘（差 >= α）→ 不滤，保边。
    ax1.plot(xs, ei, "-o", color=C_BLUE, lw=2.2, ms=7, label="滤波前")
    ax1.plot(xs, eo, "--x", color=C_GREEN, lw=2.0, ms=9, label="滤波后")
    ax1.axvline(3.5, color=C_GRAY, lw=1.2, ls=":")
    ax1.set_title(f"真实图像边缘：|p0−q0|={abs(ei[3]-ei[4])} ≥ α={alpha}\n"
                  "→ 判为真边缘，放手不滤（保边）",
                  fontsize=11, fontweight="bold", color=C_BLUE)
    ax1.set_xticks(xs)
    ax1.set_xticklabels(labels, fontsize=9)
    ax1.set_ylabel("亮度像素值")
    ax1.legend(fontsize=9)
    ax1.grid(alpha=0.3)

    # 右：块效应（差 < α，且内部平缓）→ 滤，平滑。
    ax2.plot(xs, recon, "-o", color=C_RED, lw=2.2, ms=7, label="滤波前")
    ax2.plot(xs, filt, "-o", color=C_GREEN, lw=2.4, ms=7, label="滤波后")
    ax2.axvline(3.5, color=C_GRAY, lw=1.2, ls=":")
    ax2.set_title(f"块效应假边界：|p0−q0|={abs(recon[3]-recon[4])} < α={alpha}\n"
                  "→ 判为块效应，加权平滑（磨平）",
                  fontsize=11, fontweight="bold", color=C_ORANGE)
    ax2.set_xticks(xs)
    ax2.set_xticklabels(labels, fontsize=9)
    ax2.legend(fontsize=9)
    ax2.grid(alpha=0.3)

    fig.suptitle("同一套阈值，两种结局：靠 α/β 区分“该保”和“该磨”",
                 fontsize=13, fontweight="bold", y=1.02)
    plt.tight_layout()
    out = os.path.join(HERE, "preserve_vs_smooth.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("preserve_vs_smooth.png done")


def chart_alpha_beta_curve():
    """精确层③：α/β 阈值随 QP 变化曲线（Table 8-14 真实查表值）。"""
    d = load_dump()
    alpha = d["curve"]["alpha"]
    beta = d["curve"]["beta"]
    qp = np.arange(len(alpha))

    fig, ax = plt.subplots(figsize=(9.8, 5.2))
    ax.plot(qp, alpha, "-o", color=C_ORANGE, lw=2.2, ms=4, label="α（判真边缘）")
    ax.plot(qp, beta, "-s", color=C_BLUE, lw=2.2, ms=4, label="β（判内部平缓）")

    # 标注 QP<16 的"不滤区"。
    ax.axvspan(0, 15.5, color=C_GRAY, alpha=0.12)
    ax.text(7.5, max(alpha) * 0.55, "QP<16\nα=β=0\n几乎不滤", ha="center",
            fontsize=9.5, color="#64748b")

    # 标几个关键点。
    for q in (20, 28, 36, 44):
        ax.annotate(f"α={alpha[q]}", (q, alpha[q]), textcoords="offset points",
                    xytext=(4, 6), fontsize=8.5, color=C_ORANGE)

    ax.set_xlabel("量化参数 QP")
    ax.set_ylabel("阈值")
    ax.set_title("α/β 随 QP 增大：压得越狠、块效应越重，滤得越积极（Table 8-14）",
                 fontsize=12, fontweight="bold")
    ax.legend(loc="upper left", fontsize=10)
    ax.grid(alpha=0.3)
    plt.tight_layout()
    out = os.path.join(HERE, "alpha_beta_curve.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("alpha_beta_curve.png done")


def chart_bs_tree():
    """精确层④：边界强度 bS 决策树（什么情况 bS=4/3/2/1/0，spec 8.7.2.1）。"""
    fig, ax = plt.subplots(figsize=(9.8, 6.2))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.axis("off")

    ax.text(5, 9.5, "边界强度 bS 怎么定：从强到弱逐条判（spec 8.7.2.1）",
            ha="center", fontsize=12.5, fontweight="bold")

    # 决策条目：(条件, bS, 颜色)。
    rows = [
        ("两侧任一是帧内块，且这条边是宏块边界", "bS=4", C_RED, "强滤波（可动 p2/q2）"),
        ("两侧任一是帧内块（宏块内部边界）", "bS=3", C_ORANGE, "弱滤波（较强）"),
        ("两侧任一 4×4 块含非零残差系数", "bS=2", "#ca8a04", "弱滤波（中）"),
        ("参考帧不同 或 MV 差 ≥ 1 整像素", "bS=1", C_BLUE, "弱滤波（弱）"),
        ("以上都不满足（静止、无残差、同参考）", "bS=0", C_GRAY, "完全不滤"),
    ]
    y = 8.1
    for cond, bs, col, act in rows:
        ax.add_patch(plt.Rectangle((0.5, y - 0.45), 5.9, 0.82,
                                   facecolor=col, alpha=0.13, edgecolor=col,
                                   lw=1.6))
        ax.text(0.75, y - 0.04, cond, ha="left", va="center", fontsize=10,
                color="#1e293b")
        ax.add_patch(plt.Rectangle((6.7, y - 0.45), 1.1, 0.82, facecolor=col,
                                   edgecolor=col, lw=1.6))
        ax.text(7.25, y - 0.04, bs, ha="center", va="center", fontsize=11,
                fontweight="bold", color="white")
        ax.text(8.1, y - 0.04, act, ha="left", va="center", fontsize=9,
                color=col)
        if y > 3.5:
            ax.annotate("", xy=(3.4, y - 0.55), xytext=(3.4, y - 0.95),
                        arrowprops=dict(arrowstyle="->", color=C_GRAY, lw=1.3))
            ax.text(3.7, y - 0.75, "否则", fontsize=8, color=C_GRAY)
        y -= 1.4

    ax.text(5, 0.5,
            "越靠上强度越大：帧内块边界最可能有台阶（bS=4），静止无残差最不可能（bS=0）",
            ha="center", fontsize=9.5, color="#475569")
    plt.tight_layout()
    out = os.path.join(HERE, "bs_tree.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("bs_tree.png done")


def chart_inloop_position():
    """形象层：去块滤波在解码环路内的位置——为什么滤波后的帧要当参考帧。"""
    fig, ax = plt.subplots(figsize=(10.0, 5.6))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.axis("off")

    ax.text(5, 9.5, "去块滤波在“环路内”：滤波后的帧既显示、又当参考",
            ha="center", fontsize=12.5, fontweight="bold")

    def box(x, y, w, h, text, fc, ec):
        ax.add_patch(plt.Rectangle((x, y), w, h, facecolor=fc, edgecolor=ec,
                                   lw=2))
        ax.text(x + w / 2, y + h / 2, text, ha="center", va="center",
                fontsize=9.8, color="#1e293b", fontweight="bold")

    # 主链路：残差重建 → 预测相加 → 去块滤波 → 重建帧。
    box(0.4, 6.6, 2.0, 1.1, "预测 + 残差\n(块各画各的)", "#dbeafe", C_BLUE)
    box(2.9, 6.6, 2.0, 1.1, "重建帧\n(有块效应)", "#fee2e2", C_RED)
    box(5.4, 6.6, 2.1, 1.1, "去块滤波\n(in-loop)", "#dcfce7", C_GREEN)
    box(8.0, 6.6, 1.6, 1.1, "干净帧", "#dcfce7", C_GREEN)

    ax.annotate("", xy=(2.9, 7.15), xytext=(2.4, 7.15),
                arrowprops=dict(arrowstyle="->", color=C_GRAY, lw=1.8))
    ax.annotate("", xy=(5.4, 7.15), xytext=(4.9, 7.15),
                arrowprops=dict(arrowstyle="->", color=C_GRAY, lw=1.8))
    ax.annotate("", xy=(8.0, 7.15), xytext=(7.5, 7.15),
                arrowprops=dict(arrowstyle="->", color=C_GRAY, lw=1.8))

    # 两个去向：显示 + 当参考帧（回流）。
    box(8.0, 4.4, 1.6, 0.9, "送去显示", "#f1f5f9", C_GRAY)
    ax.annotate("", xy=(8.8, 5.3), xytext=(8.8, 6.55),
                arrowprops=dict(arrowstyle="->", color=C_GRAY, lw=1.8))

    box(3.0, 3.4, 3.0, 1.0, "参考帧缓冲（DPB）", "#ede9fe", C_PURPLE)
    # 回流箭头：干净帧 → 参考帧缓冲。
    ax.annotate("", xy=(6.0, 3.9), xytext=(8.8, 6.55),
                arrowprops=dict(arrowstyle="-|>", color=C_PURPLE, lw=2.2,
                                connectionstyle="arc3,rad=0.3"))
    ax.text(6.6, 5.2, "滤波后的帧\n回流当参考", fontsize=9.2, color=C_PURPLE,
            fontweight="bold", ha="center")
    # 参考帧 → 下一帧预测。
    ax.annotate("", xy=(1.4, 6.55), xytext=(3.2, 4.35),
                arrowprops=dict(arrowstyle="-|>", color=C_PURPLE, lw=2.2,
                                connectionstyle="arc3,rad=0.3"))
    ax.text(1.6, 5.2, "下一帧\n运动补偿\n以它为参考", fontsize=9, color=C_PURPLE,
            ha="center")

    ax.text(5, 1.4,
            "若参考的是带块效应的帧，误差会顺着预测链一直传下去 —— 所以必须滤在环路内",
            ha="center", fontsize=10, color=C_RED, fontweight="bold")
    ax.text(5, 0.6,
            "这跟 JPEG“只在输出时后处理”根本不同：那种后处理不影响任何后续解码",
            ha="center", fontsize=9.5, color="#475569")
    plt.tight_layout()
    out = os.path.join(HERE, "inloop_position.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("inloop_position.png done")


if __name__ == "__main__":
    sync_stage_map()
    chart_inloop_position()
    chart_edge_before_after()
    chart_preserve_vs_smooth()
    chart_alpha_beta_curve()
    chart_bs_tree()
    print("all charts done")
