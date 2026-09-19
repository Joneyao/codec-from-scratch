#!/usr/bin/env python3
"""C6 H.264 CAVLC 残差解码配图。

数据来源全部为 codec-from-scratch/h264/decoder 真实跑出的结果：
  third-party/codec-from-scratch/h264/decoder/chart_data/cavlc_dump.txt
不手编数字。样例本身是 H.264 教科书（Richardson）的标准 CAVLC 示例。
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
DUMP = os.path.join(REPO, "h264/decoder/chart_data/cavlc_dump.txt")

C_BLUE = "#2563eb"
C_GREEN = "#16a34a"
C_ORANGE = "#ea580c"
C_RED = "#dc2626"
C_GRAY = "#94a3b8"
C_PURPLE = "#7c3aed"


def load_dump():
    """把 cavlc_dump.txt 解析成一个 dict。"""
    d = {"scalar": {}, "main": {}, "ctx": {}}
    with open(DUMP) as f:
        for ln in f:
            ln = ln.strip()
            if not ln or ln.startswith("#"):
                continue
            parts = ln.split()
            tag = parts[0]
            if tag == "bits":
                d["scalar"]["bits"] = parts[1]
            elif tag == "correct":
                d["scalar"]["correct"] = int(parts[1])
            elif tag == "ctx_nc":
                d["ctx"][int(parts[1])] = (int(parts[2]), int(parts[3]))
            elif tag.startswith("main_"):
                key = tag[len("main_"):]
                vals = [int(x) for x in parts[1:]]
                d["main"][key] = vals[0] if len(vals) == 1 else vals
    return d


def sync_stage_map():
    import sys
    sys.path.insert(0, os.path.join(HERE, ".."))
    from pipeline_map import draw_h264_decode_map
    draw_h264_decode_map("transform", os.path.join(HERE, "stage_map.png"))
    print("stage_map.png done")


def chart_block_decode():
    """精确层①：一个真实 4x4 块的 CAVLC 完整解码流程。
    coeff_token -> 尾部±1 -> level -> total_zeros -> run_before -> 16 系数。"""
    d = load_dump()
    m = d["main"]
    tc, t1 = m["total_coeff"], m["trailing_ones"]
    tz = m["total_zeros"]
    levels = m["levels"]
    runs = m["runs"]
    coeff = m["coeff"]
    bits = d["scalar"]["bits"]

    fig, ax = plt.subplots(figsize=(10.2, 6.4))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.axis("off")

    def step(y, label, value, color):
        ax.add_patch(plt.Rectangle((0.3, y), 2.6, 0.82, facecolor=color,
                                   edgecolor="white", lw=1.5, alpha=0.9))
        ax.text(1.6, y + 0.41, label, ha="center", va="center", fontsize=10.5,
                fontweight="bold", color="white")
        ax.text(3.2, y + 0.41, value, ha="left", va="center", fontsize=11,
                color="#1e293b")

    ax.text(5, 9.5, f"输入比特串（{len(bits)} bit）：{bits}", ha="center",
            fontsize=11, fontweight="bold", color=C_PURPLE)

    step(8.3, "① coeff_token",
         f"TotalCoeff={tc}（非零系数个数），TrailingOnes={t1}（尾部±1个数）", C_BLUE)
    step(7.2, "② 尾部±1符号",
         f"末尾 {t1} 个 ±1，各读 1 位符号 → {levels[:t1]}", C_GREEN)
    step(6.1, "③ level 幅值",
         f"其余系数 level_prefix+suffix → {levels[t1:]}", C_GREEN)
    step(5.0, "④ total_zeros", f"非零系数之间的 0 总共 {tz} 个", C_ORANGE)
    step(3.9, "⑤ run_before",
         f"每个非零系数前的 0：{runs}", C_ORANGE)

    # 最终 16 系数格子
    ax.text(5, 3.1, "组合还原 → 16 个量化系数（扫描顺序，低频→高频）", ha="center",
            fontsize=10.5, fontweight="bold", color="#1e293b")
    x0, y0, w = 0.7, 1.6, 0.56
    for i, v in enumerate(coeff):
        fc = "#dbeafe" if v != 0 else "#f1f5f9"
        ax.add_patch(plt.Rectangle((x0 + i * w, y0), w, 0.9, facecolor=fc,
                                   edgecolor="#cbd5e1", lw=1))
        ax.text(x0 + i * w + w / 2, y0 + 0.45, str(v), ha="center",
                va="center", fontsize=10,
                fontweight="bold" if v != 0 else "normal",
                color=C_RED if v != 0 else C_GRAY)
    ax.text(5, 0.9,
            "非零系数挤在低频端、尾部是 ±1、后面一长串 0——"
            "CAVLC 正是为这个形状设计的", ha="center", fontsize=9.5,
            color=C_ORANGE)

    ax.set_title("CAVLC 解一个真实 4x4 块：五个语法元素接力还原 16 个系数",
                 fontsize=13, fontweight="bold", pad=10)
    plt.tight_layout()
    out = os.path.join(HERE, "block_decode.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("block_decode.png done")


def chart_context_select():
    """精确层②：同一段比特，nC 不同 -> 选不同 coeff_token 表 -> 解出不同结果。
    这是 CAVLC "上下文自适应" 的核心，真实跑出的对照。"""
    d = load_dump()
    ctx = d["ctx"]
    ncs = sorted(ctx.keys())
    bucket = {0: "0≤nC<2\nTable 9-5 第0档",
              2: "2≤nC<4\n第1档",
              4: "4≤nC<8\n第2档",
              8: "nC≥8\n定长6位码"}
    bits = d["scalar"]["bits"]

    fig, ax = plt.subplots(figsize=(9.6, 5.2))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.axis("off")

    ax.text(5, 9.3, f"同一段开头比特：{bits[:10]}…", ha="center", fontsize=11.5,
            fontweight="bold", color=C_PURPLE)
    ax.text(5, 8.6, "喂给解码器，只有 nC（上下文）不同", ha="center", fontsize=10,
            color="#475569")

    xs = [1.4, 3.7, 6.0, 8.3]
    colors = [C_BLUE, C_GREEN, C_ORANGE, C_RED]
    for x, nc, col in zip(xs, ncs, colors):
        tc, t1 = ctx[nc]
        ax.add_patch(plt.Rectangle((x - 0.95, 5.6), 1.9, 1.5, facecolor=col,
                                   alpha=0.12, edgecolor=col, lw=2))
        ax.text(x, 6.75, f"nC={nc}", ha="center", fontsize=12,
                fontweight="bold", color=col)
        ax.text(x, 6.1, bucket[nc], ha="center", fontsize=8.5, color="#334155")
        # 箭头
        ax.annotate("", xy=(x, 4.6), xytext=(x, 5.5),
                    arrowprops=dict(arrowstyle="->", color=col, lw=1.8))
        ax.add_patch(plt.Rectangle((x - 0.95, 3.0), 1.9, 1.5, facecolor="white",
                                   edgecolor=col, lw=2))
        ax.text(x, 4.1, f"TotalCoeff={tc}", ha="center", fontsize=10.5,
                fontweight="bold", color="#1e293b")
        ax.text(x, 3.5, f"TrailingOnes={t1}", ha="center", fontsize=10.5,
                color="#1e293b")

    ax.text(5, 1.9,
            "同样的比特，选错表就解成完全不同的系数个数——"
            "所以选表必须由邻居决定，不能拍脑袋", ha="center", fontsize=9.8,
            color=C_ORANGE)
    ax.text(5, 1.2, "这就是 “上下文自适应”：看邻居块的忙闲，动态挑最省比特的码表",
            ha="center", fontsize=9.8, fontweight="bold", color=C_PURPLE)

    ax.set_title("上下文自适应：同一段比特，nC 不同解出不同结果",
                 fontsize=13, fontweight="bold", pad=8)
    plt.tight_layout()
    out = os.path.join(HERE, "context_select.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("context_select.png done")


def chart_token_table():
    """精确层③：coeff_token 码表片段（Table 9-5），展示 nC 分档如何影响码长。
    数字为 spec Table 9-5 的真实码字比特数（选几组有代表性的 (T1s,TC)）。"""
    # 行：(TrailingOnes, TotalCoeff)；列：三个 nC 档的码长（bit）。
    # 值来自 ITU-T H.264 Table 9-5（与解码器硬编码表一致）。
    rows = [
        ("0", "1", [6, 6, 6]),
        ("1", "1", [2, 2, 4]),
        ("2", "2", [3, 3, 4]),
        ("3", "3", [5, 4, 4]),
        ("3", "5", [6, 5, 4]),
        ("0", "8", [13, 11, 8]),
    ]
    cols = ["0≤nC<2\n(第0档)", "2≤nC<4\n(第1档)", "4≤nC<8\n(第2档)"]

    fig, ax = plt.subplots(figsize=(8.8, 4.8))
    ax.set_xlim(0, len(cols) + 2)
    ax.set_ylim(0, len(rows) + 2)
    ax.invert_yaxis()
    ax.axis("off")

    ax.text((len(cols) + 2) / 2, 0.5,
            "coeff_token 码长（bit）· 越小越省 · 数字来自 spec Table 9-5",
            ha="center", fontsize=11, fontweight="bold", color="#1e293b")
    # 表头
    ax.text(0.5, 1.6, "T1s", ha="center", fontsize=9.5, fontweight="bold")
    ax.text(1.5, 1.6, "TC", ha="center", fontsize=9.5, fontweight="bold")
    for j, c in enumerate(cols):
        ax.text(2.5 + j, 1.55, c, ha="center", va="center", fontsize=8.5,
                fontweight="bold", color=C_BLUE)

    for i, (t1, tc, lens) in enumerate(rows):
        y = 2.5 + i
        ax.text(0.5, y, t1, ha="center", va="center", fontsize=10)
        ax.text(1.5, y, tc, ha="center", va="center", fontsize=10)
        mn = min(lens)
        for j, L in enumerate(lens):
            col = C_GREEN if L == mn else "#334155"
            ax.add_patch(plt.Rectangle((2.0 + j, y - 0.42), 1.0, 0.84,
                         facecolor="#e8f5e9" if L == mn else "#f8fafc",
                         edgecolor="#cbd5e1", lw=1))
            ax.text(2.5 + j, y, str(L), ha="center", va="center", fontsize=11,
                    fontweight="bold" if L == mn else "normal", color=col)

    ax.text((len(cols) + 2) / 2, len(rows) + 1.4,
            "低 nC 档给 “少非零” 组合更短的码；高 nC 档给 “多非零” 组合更短",
            ha="center", fontsize=9.3, color=C_ORANGE)
    ax.set_title("同一个 coeff_token，在不同 nC 档里码长不同",
                 fontsize=13, fontweight="bold", pad=8)
    plt.tight_layout()
    out = os.path.join(HERE, "token_table.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("token_table.png done")


# 4x4 zigzag：扫描序号 -> (row, col)，与解码器 InverseZigzag4x4 完全一致。
ZZ_ROW = [0, 0, 1, 2, 1, 0, 0, 1, 2, 3, 3, 2, 1, 2, 3, 3]
ZZ_COL = [0, 1, 0, 0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 3, 2, 3]


def chart_zigzag_fill():
    """精确层④：解出的 16 个扫描顺序系数，按 zigzag 反扫描摆回 4x4。"""
    d = load_dump()
    coeff = d["main"]["coeff"]
    grid = np.zeros((4, 4), dtype=int)
    for k in range(16):
        grid[ZZ_ROW[k]][ZZ_COL[k]] = coeff[k]

    fig, (axl, axr) = plt.subplots(1, 2, figsize=(10.4, 4.6))

    # 左：一维扫描顺序
    axl.set_xlim(0, 16)
    axl.set_ylim(0, 3)
    axl.axis("off")
    axl.set_title("① 扫描顺序的 16 个系数（低频→高频）", fontsize=11,
                  fontweight="bold")
    for i, v in enumerate(coeff):
        fc = "#dbeafe" if v != 0 else "#f1f5f9"
        axl.add_patch(plt.Rectangle((i, 1.2), 0.92, 0.9, facecolor=fc,
                                    edgecolor="#cbd5e1", lw=1))
        axl.text(i + 0.46, 1.65, str(v), ha="center", va="center", fontsize=8.5,
                 fontweight="bold" if v != 0 else "normal",
                 color=C_RED if v != 0 else C_GRAY)
        axl.text(i + 0.46, 0.9, str(i), ha="center", va="center", fontsize=6.5,
                 color=C_GRAY)

    # 右：反扫描回 4x4
    axr.set_xlim(0, 4)
    axr.set_ylim(0, 4)
    axr.invert_yaxis()
    axr.axis("off")
    axr.set_title("② zigzag 反扫描 → 4x4 块（左上是 DC）", fontsize=11,
                  fontweight="bold")
    for y in range(4):
        for x in range(4):
            v = grid[y][x]
            fc = "#dbeafe" if v != 0 else "#f1f5f9"
            axr.add_patch(plt.Rectangle((x, y), 0.94, 0.94, facecolor=fc,
                          edgecolor="#cbd5e1", lw=1.5))
            axr.text(x + 0.47, y + 0.47, str(v), ha="center", va="center",
                     fontsize=13, fontweight="bold" if v != 0 else "normal",
                     color=C_RED if v != 0 else C_GRAY)

    fig.suptitle("解出的系数按 zigzag 摆回二维块：非零全挤在左上低频角",
                 fontsize=12.5, fontweight="bold", y=1.02)
    plt.tight_layout()
    out = os.path.join(HERE, "zigzag_fill.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("zigzag_fill.png done")


def chart_context_idea():
    """形象层：CAVLC "数邻居用了几个非零系数来选表" 的上下文自适应原理。
    左邻 nA + 上邻 nB -> nC -> 选码表。这是 CABAC 上下文建模的雏形。"""
    fig, ax = plt.subplots(figsize=(9.6, 5.6))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.axis("off")

    # 宏块网格中的当前块 + 左邻 + 上邻
    def block(x, y, label, sub, color, big=False):
        w = 1.5
        ax.add_patch(plt.Rectangle((x, y), w, w, facecolor=color, alpha=0.18,
                                   edgecolor=color, lw=2.2))
        ax.text(x + w / 2, y + w * 0.62, label, ha="center", va="center",
                fontsize=11 if big else 10, fontweight="bold", color=color)
        ax.text(x + w / 2, y + w * 0.28, sub, ha="center", va="center",
                fontsize=8.5, color="#334155")

    block(3.0, 6.6, "上邻块 B", "非零数 nB=3", C_GREEN)
    block(1.2, 4.7, "左邻块 A", "非零数 nA=5", C_BLUE)
    block(3.0, 4.7, "当前块", "要解它", C_RED, big=True)

    # 箭头汇聚
    ax.annotate("", xy=(3.9, 6.4), xytext=(3.75, 6.3),
                arrowprops=dict(arrowstyle="-", color=C_GRAY))
    ax.annotate("", xy=(3.6, 5.9), xytext=(3.75, 6.55),
                arrowprops=dict(arrowstyle="->", color=C_GREEN, lw=1.8))
    ax.annotate("", xy=(3.4, 5.6), xytext=(2.7, 5.45),
                arrowprops=dict(arrowstyle="->", color=C_BLUE, lw=1.8))

    ax.text(6.7, 5.9, "nC = (nA + nB + 1) >> 1\n= (5 + 3 + 1) >> 1 = 4",
            ha="center", va="center", fontsize=11, color="#1e293b",
            bbox=dict(boxstyle="round,pad=0.5", fc="#fff7ed", ec=C_ORANGE))
    ax.annotate("", xy=(6.7, 4.9), xytext=(6.7, 5.4),
                arrowprops=dict(arrowstyle="->", color=C_ORANGE, lw=2))
    ax.text(6.7, 4.4, "nC=4 → 选 “4≤nC<8” 那张 coeff_token 表",
            ha="center", va="center", fontsize=10.5, fontweight="bold",
            color=C_ORANGE)

    ax.text(5, 2.9,
            "邻居忙（非零多），当前块大概率也忙 → 挑一张对 “多非零” 更省的表",
            ha="center", fontsize=10, color="#475569")
    ax.text(5, 2.1,
            "固定表（JPEG 霍夫曼）对所有块一视同仁；CAVLC 看邻居动态选表",
            ha="center", fontsize=10, color="#475569")
    ax.text(5, 1.2,
            "“看上下文动态选” 这一步，正是后来 CABAC 上下文建模的雏形",
            ha="center", fontsize=10.5, fontweight="bold", color=C_PURPLE)

    ax.set_title("CAVLC 的上下文自适应：数邻居用了几个非零系数来选表",
                 fontsize=13, fontweight="bold", pad=8)
    plt.tight_layout()
    out = os.path.join(HERE, "context_idea.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("context_idea.png done")


if __name__ == "__main__":
    sync_stage_map()
    chart_block_decode()
    chart_context_select()
    chart_token_table()
    chart_zigzag_fill()
    chart_context_idea()
    print("all charts done")
