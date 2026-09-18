// forward_transform_quant_demo.cpp — 用真实残差块跑通 H.264 正向整数变换 + 量化，
// 导出两组配图数据：
//   1) fwd_transform.txt：残差块 -> 正向变换系数 W -> 量化系数 c（QP=28），
//      展示"能量集中到左上角 + 大量高频系数被量化成 0"。
//   2) qp_vs_nonzero.txt：同一残差块，QP 从 20 到 40 各跑一遍，统计量化后非零
//      系数个数（近似码率代价）。这是 D2 的高潮：QP 越大，非零越少，码率越低。
//
// 残差来源：samples/test_pattern.ppm 是合成测试图，只有纯色硬边，取出来的块
// 要么整块平坦、要么是阶跃边（残差能量退化，QP 曲线画不出层次）。教学 demo
// 需要"自然图像残差"那种低频占主导、高频渐衰的能量分布，才能看出"QP 越大、
// 非零系数越少"的核心规律。因此这里默认用一个能量分布真实的固定教学残差块
// （帧内预测后的典型残差形状），并同时读一个 PPM 块作对照打印。
//
// 用法：./forward_transform_quant_demo [chart_data_dir] [ppm_path]
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "forward_transform_quant.h"

using cfs::Coeff4x4;

namespace {

// 极简 PPM(P6) 读取：只为取一个块，不引第三方依赖。失败返回 false。
bool ReadPpmBlockLuma(const std::string& path, int bx, int by, Coeff4x4& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char magic[3] = {0};
    if (std::fscanf(f, "%2s", magic) != 1 || std::string(magic) != "P6") {
        std::fclose(f);
        return false;
    }
    int w = 0, h = 0, maxv = 0;
    if (std::fscanf(f, "%d %d %d", &w, &h, &maxv) != 3) {
        std::fclose(f);
        return false;
    }
    std::fgetc(f);  // 跳过头后的单个空白符
    if (w <= 0 || h <= 0 || bx + 4 > w || by + 4 > h) {
        std::fclose(f);
        return false;
    }
    std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
    if (std::fread(rgb.data(), 1, rgb.size(), f) != rgb.size()) {
        std::fclose(f);
        return false;
    }
    std::fclose(f);
    // 取 4x4 块的亮度近似（BT.601 整数系数），存入 out。
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            size_t idx = (static_cast<size_t>(by + i) * w + (bx + j)) * 3;
            int r = rgb[idx], g = rgb[idx + 1], b = rgb[idx + 2];
            int y = (77 * r + 150 * g + 29 * b) >> 8;  // 0..255
            out[i][j] = y;
        }
    return true;
}

// 把亮度块转成"减去均值后的残差"（模拟 DC 预测残差：低频起伏为主）。
Coeff4x4 ToResidual(const Coeff4x4& luma) {
    int sum = 0;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) sum += luma[i][j];
    int mean = sum / 16;
    Coeff4x4 res{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) res[i][j] = luma[i][j] - mean;
    return res;
}

int CountNonZero(const Coeff4x4& b) {
    int n = 0;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (b[i][j] != 0) ++n;
    return n;
}

void PrintBlock(const char* title, const Coeff4x4& b) {
    std::printf("%s:\n", title);
    for (int i = 0; i < 4; ++i) {
        std::printf("  ");
        for (int j = 0; j < 4; ++j) std::printf("%7d", b[i][j]);
        std::printf("\n");
    }
}

void DumpBlock(FILE* f, const Coeff4x4& b) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j)
            std::fprintf(f, "%d%s", b[i][j], j == 3 ? "" : " ");
        std::fprintf(f, "\n");
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string out_dir = argc > 1 ? argv[1] : "chart_data";
    std::string ppm = argc > 2 ? argv[2] : "../../samples/test_pattern.ppm";

    // ---- 残差块：用能量分布真实的固定教学块（自然图像残差的典型形状：
    // 低频起伏为主、高频有细小纹理，覆盖 15 个频率位置）----
    Coeff4x4 residual = {{
        {31, 24, -9, -14},
        {18, 12, -6, -11},
        {-8, -5, 7, 10},
        {-13, -9, 12, 16},
    }};

    // 读一个 PPM 块只用于对照打印（说明合成图残差为何不适合画曲线）。
    Coeff4x4 luma;
    bool ppm_ok = ReadPpmBlockLuma(ppm, 88, 0, luma);

    std::printf("==== H.264 正向整数变换 + 量化（真实残差）====\n");
    std::printf("残差来源：固定教学残差块（自然图像残差的典型能量分布）\n");
    if (ppm_ok) {
        Coeff4x4 ppm_res = ToResidual(luma);
        std::printf("对照：samples/test_pattern.ppm (88,0) 块减均值残差 = ");
        std::printf("[%d %d ... %d]（合成图硬边，能量退化，不用于画曲线）\n",
                    ppm_res[0][0], ppm_res[0][1], ppm_res[3][3]);
    }
    std::printf("\n");

    const int qP_demo = 28;
    Coeff4x4 W = cfs::ForwardTransform4x4(residual);
    Coeff4x4 c = cfs::ForwardTransformQuant(residual, qP_demo, /*intra=*/true);

    PrintBlock("① 原始残差块 residual", residual);
    PrintBlock("② 正向整数变换系数 W（能量集中到左上角）", W);
    std::printf("   （QP=%d 量化）\n", qP_demo);
    PrintBlock("③ 量化系数 c（大量高频被压成 0）", c);
    std::printf("   非零系数：变换后 %d -> 量化后 %d\n\n", CountNonZero(W),
                CountNonZero(c));

    // ---- QP-码率关系：QP 20..40，统计量化后非零系数个数 ----
    std::printf("==== QP vs 非零系数个数（近似码率代价）====\n");
    std::vector<std::pair<int, int>> qp_nz;
    for (int qP = 20; qP <= 40; ++qP) {
        Coeff4x4 cq = cfs::ForwardTransformQuant(residual, qP, /*intra=*/true);
        int nz = CountNonZero(cq);
        qp_nz.emplace_back(qP, nz);
        std::printf("   QP=%2d  非零系数=%2d\n", qP, nz);
    }

    // ---- 导出 chart_data ----
    {
        std::string path = out_dir + "/fwd_transform.txt";
        FILE* f = std::fopen(path.c_str(), "w");
        if (f) {
            std::fprintf(f, "# H.264 forward transform/quant demo（真实残差）\n");
            std::fprintf(f, "source builtin_natural_residual\n");
            std::fprintf(f, "qP %d\n", qP_demo);
            std::fprintf(f, "residual\n");
            DumpBlock(f, residual);
            std::fprintf(f, "forward_W\n");
            DumpBlock(f, W);
            std::fprintf(f, "quant_c\n");
            DumpBlock(f, c);
            std::fprintf(f, "nonzero_W %d\n", CountNonZero(W));
            std::fprintf(f, "nonzero_c %d\n", CountNonZero(c));
            std::fclose(f);
            std::printf("\ndumped -> %s\n", path.c_str());
        } else {
            std::fprintf(stderr, "无法写入 %s\n", path.c_str());
            return 1;
        }
    }
    {
        std::string path = out_dir + "/qp_vs_nonzero.txt";
        FILE* f = std::fopen(path.c_str(), "w");
        if (f) {
            std::fprintf(f, "# qp nonzero_count\n");
            for (auto& kv : qp_nz)
                std::fprintf(f, "%d %d\n", kv.first, kv.second);
            std::fclose(f);
            std::printf("dumped -> %s\n", path.c_str());
        } else {
            std::fprintf(stderr, "无法写入 %s\n", path.c_str());
            return 1;
        }
    }
    return 0;
}
