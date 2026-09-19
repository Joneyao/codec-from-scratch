#!/usr/bin/env python3
"""C1 H.264 NAL 切分器配图。

数据来源全部为 codec-from-scratch/h264/decoder 真实跑出的结果：
  - 逐 NAL 表：nal_splitter_demo 写出的 chart_data/nal_dump.txt
  - emulation prevention 真实字节：直接从 samples/test.h264 的 SPS 里抠出
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
DEC = os.path.join(REPO, "h264/decoder")
DUMP = os.path.join(DEC, "chart_data/nal_dump.txt")
H264 = os.path.join(REPO, "samples/test.h264")

# NAL 类型 -> 颜色（SPS/PPS 参数、SEI 辅助、IDR 关键帧、非IDR 普通帧）。
TYPE_COLOR = {
    "SPS": "#2563eb",
    "PPS": "#0891b2",
    "SEI": "#94a3b8",
    "IDR slice": "#dc2626",
    "non-IDR slice": "#ea580c",
}
C_GRAY = "#64748b"


def load_dump():
    """读 nal_splitter_demo 写出的逐 NAL 表。"""
    units = []
    total_ep3b = 0
    with open(DUMP) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                if line.startswith("# total_ep3b"):
                    total_ep3b = int(line.split()[-1])
                continue
            p = line.split()
            # idx type_name(可能含空格) ... 用固定列反向解析更稳。
            idx = int(p[0])
            ep3b = int(p[-1]); rbsp = int(p[-2]); nal_size = int(p[-3])
            nal_off = int(p[-4]); sc_len = int(p[-5]); ref_idc = int(p[-6])
            type_id = int(p[-7])
            type_name = " ".join(p[1:-7])
            units.append(dict(idx=idx, type=type_name, type_id=type_id,
                              ref_idc=ref_idc, sc_len=sc_len, off=nal_off,
                              size=nal_size, rbsp=rbsp, ep3b=ep3b))
    return units, total_ep3b


def sync_stage_map():
    import sys
    sys.path.insert(0, os.path.join(HERE, ".."))
    from pipeline_map import draw_h264_decode_map
    draw_h264_decode_map("nal", os.path.join(HERE, "stage_map.png"))
    print("stage_map.png done")


def chart_nal_timeline():
    """真实 .h264 的 NAL 序列时间线：按文件字节偏移横向排开，
    每个 NAL 一个色块，宽度正比于其大小，标类型和起始偏移。"""
    units, _ = load_dump()
    total = units[-1]["off"] + units[-1]["size"]

    fig, ax = plt.subplots(figsize=(11, 3.6))
    for u in units:
        x = u["off"] / total
        w = u["size"] / total
        col = TYPE_COLOR.get(u["type"], C_GRAY)
        ax.add_patch(plt.Rectangle((x, 0.35), w, 0.42, facecolor=col,
                                   edgecolor="white", linewidth=1.0, zorder=3))
        # 类型名（小块只标 idx，避免文字挤成一团）
        cx = x + w / 2
        if w > 0.045:
            ax.text(cx, 0.56, u["type"].replace(" slice", ""),
                    ha="center", va="center", color="white",
                    fontsize=8.5, fontweight="bold", zorder=4, rotation=0)
        ax.text(cx, 0.30, f"@{u['off']}", ha="center", va="top",
                fontsize=6.8, color="#475569", rotation=90)

    # 图例
    handles = [plt.Rectangle((0, 0), 1, 1, facecolor=c) for c in TYPE_COLOR.values()]
    ax.legend(handles, list(TYPE_COLOR.keys()), loc="upper center",
              ncol=5, bbox_to_anchor=(0.5, 1.16), fontsize=9, frameon=False)

    ax.text(0.5, 0.12,
            f"共 {len(units)} 个 NAL，首尾拼成 {total:,} 字节的裸流；"
            f"两个 IDR 各带一套 SPS/PPS（可随机接入）",
            ha="center", fontsize=9.5, color="#ea580c")
    ax.set_xlim(-0.01, 1.01)
    ax.set_ylim(0, 1.0)
    ax.axis("off")
    ax.set_title("真实 test.h264 的 NAL 序列：起始码把字节流切成一段段单元",
                 fontweight="bold", fontsize=13)
    plt.tight_layout()
    out = os.path.join(HERE, "nal_timeline.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("nal_timeline.png done  (%d units, %d bytes)" % (len(units), total))


def chart_emulation_prevention():
    """emulation prevention 还原示意：用 SPS 里真实出现的字节序列
    04 40 00 00 03 00 40 展示 00 00 03 00 -> 00 00 00 的变换。"""
    # 从真实 SPS(NAL#0, offset 4, size 22) 抠出这段字节。
    data = open(H264, "rb").read()
    nal = data[4:4 + 22]
    # 定位 00 00 03，取前后各 2 字节作为展示窗口。
    pos = None
    for i in range(len(nal) - 2):
        if nal[i] == 0 and nal[i + 1] == 0 and nal[i + 2] == 3:
            pos = i
            break
    win = nal[pos - 2: pos + 4]  # 04 40 00 00 03 00
    raw = list(win) + [0x40]     # 补一字节让 after 更直观
    # after：吞掉那个 0x03
    after = [b for j, b in enumerate(raw) if j != 4]  # index4 是 03

    fig, ax = plt.subplots(figsize=(10, 3.8))

    def draw_row(y, bytes_list, drop_idx, title):
        n = len(bytes_list)
        bw = 0.11
        gap = 0.015
        x0 = 0.5 - (n * bw + (n - 1) * gap) / 2
        for j, b in enumerate(bytes_list):
            x = x0 + j * (bw + gap)
            is_drop = (drop_idx is not None and j == drop_idx)
            fc = "#dc2626" if is_drop else "#e2e8f0"
            tc = "white" if is_drop else "#1e293b"
            ax.add_patch(plt.Rectangle((x, y), bw, 0.2, facecolor=fc,
                                       edgecolor="#94a3b8", linewidth=1.2,
                                       zorder=3))
            ax.text(x + bw / 2, y + 0.1, f"{b:02X}", ha="center",
                    va="center", fontsize=13, fontweight="bold",
                    color=tc, family="monospace", zorder=4)
        ax.text(x0 - 0.02, y + 0.1, title, ha="right", va="center",
                fontsize=10.5, color="#334155")

    draw_row(0.62, raw, 4, "NAL 载荷（转义后）")
    draw_row(0.18, after, None, "还原出的 RBSP")

    # 中间箭头
    ax.annotate("", xy=(0.5, 0.55), xytext=(0.5, 0.45),
                arrowprops=dict(arrowstyle="-|>", color="#dc2626", lw=2.2))
    ax.text(0.56, 0.5, "吞掉 0x03", ha="left", va="center", fontsize=10,
            color="#dc2626", fontweight="bold")
    ax.text(0.5, 0.90,
            "遇到 00 00 03 就把这个 03 丢掉：它是编码器插入的防冲突字节，"
            "不属于真实数据",
            ha="center", fontsize=10, color="#334155")
    ax.text(0.5, 0.04, "取自 test.h264 里 SPS 的真实字节（NAL#0 @字节4）",
            ha="center", fontsize=8.5, color=C_GRAY, style="italic")
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")
    ax.set_title("Emulation Prevention 还原：00 00 03 00 变回 00 00 00",
                 fontweight="bold", fontsize=13)
    plt.tight_layout()
    out = os.path.join(HERE, "emulation_prevention.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("emulation_prevention.png done  (window=%s)"
          % " ".join("%02X" % b for b in raw))


def chart_type_sizes():
    """各 NAL 类型的字节占比：真实 test.h264 里每类 NAL 的总字节。
    体现 slice（图像数据）占绝大多数，参数集只占几十字节。"""
    units, _ = load_dump()
    agg = {}
    for u in units:
        agg.setdefault(u["type"], [0, 0])
        agg[u["type"]][0] += u["size"]
        agg[u["type"]][1] += 1
    # 按字节从大到小
    order = sorted(agg.items(), key=lambda kv: -kv[1][0])
    names = [f"{k}\n({v[1]}个)" for k, v in order]
    sizes = [v[0] for _, v in order]
    colors = [TYPE_COLOR.get(k, C_GRAY) for k, _ in order]

    total = sum(sizes)
    fig, ax = plt.subplots(figsize=(9, 4.4))
    bars = ax.bar(names, sizes, color=colors, width=0.62, zorder=3)
    for b, s in zip(bars, sizes):
        ax.text(b.get_x() + b.get_width() / 2, s, f"{s:,}\n{s/total*100:.1f}%",
                ha="center", va="bottom", fontsize=9, color="#1e293b")
    ax.set_ylabel("该类型 NAL 总字节数")
    ax.set_title("NAL 类型字节分布：图像 slice 占绝大多数，参数集微不足道",
                 fontweight="bold", fontsize=12.5)
    ax.set_ylim(0, max(sizes) * 1.2)
    ax.grid(axis="y", alpha=0.3, zorder=0)
    plt.tight_layout()
    out = os.path.join(HERE, "nal_type_sizes.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("nal_type_sizes.png done  (total=%d bytes)" % total)


def chart_nal_structure():
    """形象层：NAL Unit 结构拆解（起始码 + header + RBSP），
    header 的 1+2+5 位再拆开画。"""
    fig, ax = plt.subplots(figsize=(10.5, 4.4))

    # 顶层：起始码 | NAL header | RBSP
    segs = [("起始码\n00 00 01", 0.02, 0.16, "#94a3b8"),
            ("NAL header\n1 字节", 0.20, 0.16, "#2563eb"),
            ("RBSP\n去转义后的载荷", 0.38, 0.58, "#16a34a")]
    for name, x, w, col in segs:
        ax.add_patch(plt.Rectangle((x, 0.62), w, 0.24, facecolor=col,
                                   edgecolor="white", linewidth=2, zorder=3))
        ax.text(x + w / 2, 0.74, name, ha="center", va="center",
                color="white", fontsize=10.5, fontweight="bold", zorder=4)

    # 下层：把 header 的 8 个 bit 拆开（1 forbidden + 2 ref_idc + 5 type）
    bit_groups = [("F", 1, "#0f172a"), ("nal_ref_idc", 2, "#7c3aed"),
                  ("nal_unit_type", 5, "#dc2626")]
    bx0 = 0.14
    bw = 0.09
    x = bx0
    # 连接线：header -> bit 展开区
    ax.annotate("", xy=(bx0 + 4 * bw, 0.42), xytext=(0.28, 0.60),
                arrowprops=dict(arrowstyle="->", color="#64748b", lw=1.4))
    for name, nbits, col in bit_groups:
        gw = bw * nbits
        ax.add_patch(plt.Rectangle((x, 0.20), gw, 0.2, facecolor=col,
                                   edgecolor="white", linewidth=1.5, zorder=3))
        ax.text(x + gw / 2, 0.30, f"{name}\n{nbits} bit", ha="center",
                va="center", color="white", fontsize=9.5,
                fontweight="bold", zorder=4)
        x += gw

    ax.text(0.5, 0.06,
            "以真实 SPS 的 header 字节 0x67 为例："
            "0 11 00111 → type=7(SPS)，ref_idc=3（会被参考）",
            ha="center", fontsize=9.5, color="#ea580c")
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")
    ax.set_title("一个 NAL Unit 的拆解：起始码分隔 + 1 字节头 + RBSP 载荷",
                 fontweight="bold", fontsize=13)
    plt.tight_layout()
    out = os.path.join(HERE, "nal_structure.png")
    plt.savefig(out, dpi=130, bbox_inches="tight")
    plt.close()
    print("nal_structure.png done")


if __name__ == "__main__":
    sync_stage_map()
    chart_nal_timeline()
    chart_emulation_prevention()
    chart_type_sizes()
    chart_nal_structure()
    print("all charts done")
