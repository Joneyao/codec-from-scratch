// cavlc_demo.cpp — 用一段已知的 CAVLC 比特串，走通 H.264 残差解码（spec 9.2），
//                 并演示“上下文自适应”：同一段比特、不同的 nC，选出不同的表、
//                 解出不同的结果。
//
// 数据来源说明：
//   本篇用的是 H.264 领域公认的教科书样例（Iain Richardson,《H.264 and MPEG-4
//   Video Compression》4x4 CAVLC 编码示例）。该块系数（扫描顺序，低频->高频）为
//     0, 3, 0, 1, -1, -1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0
//   即 TotalCoeff=5、TrailingOnes=3、非尾部系数是 +1 和 +3、total_zeros=3。
//   在 nC=0 档下，标准编出的比特串是（含 coeff_token/符号/level/total_zeros/
//   run_before 全部字段）：0000 1000 1110 0101 1110 1101
//   我们把这串比特喂进解码器，看能不能原样还原出上面那 16 个系数。
//   走通真实宏块定位到某个块的 residual 需要先完成 CABAC/CAVLC 之外的整套宏块
//   语法解析（本系列后续再拼），因此这里用标准样例把 CAVLC 的解码逻辑讲透，
//   数据可被任何一本 H.264 教材核对。
//
// 用法：
//   ./cavlc_demo [chart_data]
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "cavlc_decode.h"
#include "rbsp_bit_reader.h"

namespace {

using cfs::CavlcResult;

// 把一串 '0'/'1' 字符打包成字节（MSB 在前），供 RbspBitReader 读取。
std::vector<uint8_t> PackBits(const std::string& bits) {
    std::vector<uint8_t> out((bits.size() + 7) / 8, 0);
    for (size_t i = 0; i < bits.size(); ++i)
        if (bits[i] == '1') out[i >> 3] |= (0x80u >> (i & 7));
    return out;
}

void PrintScanCoeff(const char* title, const cfs::Cavlc4x4& c) {
    std::printf("%s:\n  [", title);
    for (int i = 0; i < 16; ++i)
        std::printf("%d%s", c[i], i == 15 ? "" : ", ");
    std::printf("]\n");
}

void PrintBlock2D(const char* title,
                  const std::array<std::array<int, 4>, 4>& b) {
    std::printf("%s（zigzag 反扫描回 4x4）:\n", title);
    for (int y = 0; y < 4; ++y) {
        std::printf("  ");
        for (int x = 0; x < 4; ++x) std::printf("%5d", b[y][x]);
        std::printf("\n");
    }
}

void DumpResult(FILE* f, const char* tag, const CavlcResult& r) {
    std::fprintf(f, "%s_nc %d\n", tag, r.nc);
    std::fprintf(f, "%s_total_coeff %d\n", tag, r.token.total_coeff);
    std::fprintf(f, "%s_trailing_ones %d\n", tag, r.token.trailing_ones);
    std::fprintf(f, "%s_total_zeros %d\n", tag, r.total_zeros);
    std::fprintf(f, "%s_bits %zu\n", tag, r.bits_used);
    std::fprintf(f, "%s_levels", tag);
    for (int i = 0; i < r.token.total_coeff; ++i)
        std::fprintf(f, " %d", r.levels[i]);
    std::fprintf(f, "\n");
    std::fprintf(f, "%s_runs", tag);
    for (int i = 0; i < r.token.total_coeff; ++i)
        std::fprintf(f, " %d", r.runs[i]);
    std::fprintf(f, "\n");
    std::fprintf(f, "%s_coeff", tag);
    for (int i = 0; i < 16; ++i) std::fprintf(f, " %d", r.coeff[i]);
    std::fprintf(f, "\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::string out_dir = argc > 1 ? argv[1] : "chart_data";

    // 教科书样例的完整比特串（nC=0 档）。
    const std::string kBits = "000010001110010111101101";

    std::printf("==== H.264 CAVLC 残差解码（真实标准样例）====\n\n");
    std::printf("输入比特串（%zu bit）：%s\n\n", kBits.size(), kBits.c_str());
    std::printf("目标系数（扫描顺序，低频->高频）：\n");
    std::printf("  [0, 3, 0, 1, -1, -1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0]\n");
    std::printf("  即 TotalCoeff=5, TrailingOnes=3, total_zeros=3\n\n");

    std::vector<uint8_t> packed = PackBits(kBits);

    // ---- 主解码：nC=0 档 ----
    cfs::RbspBitReader br(packed.data(), packed.size());
    CavlcResult r0 = cfs::DecodeResidual4x4(br, /*nc=*/0);

    std::printf("---- 解码流程逐步展开（nC=%d，选 Table 9-5 第 0 档）----\n",
                r0.nc);
    std::printf("① coeff_token -> TotalCoeff=%d, TrailingOnes=%d\n",
                r0.token.total_coeff, r0.token.trailing_ones);
    std::printf("② 各非零系数幅值 level（高频->低频）：");
    for (int i = 0; i < r0.token.total_coeff; ++i)
        std::printf(" %d", r0.levels[i]);
    std::printf("\n");
    std::printf("③ total_zeros = %d\n", r0.total_zeros);
    std::printf("④ run_before（每个非零系数前的 0 数）：");
    for (int i = 0; i < r0.token.total_coeff; ++i)
        std::printf(" %d", r0.runs[i]);
    std::printf("\n");
    std::printf("⑤ 共消耗 %zu bit\n\n", r0.bits_used);

    PrintScanCoeff("⑥ 还原出的 16 个系数（扫描顺序）", r0.coeff);
    auto blk = cfs::InverseZigzag4x4(r0.coeff);
    PrintBlock2D("⑦ 摆回二维块", blk);

    // 校验是否与目标一致。
    const int expect[16] = {0, 3, 0, 1, -1, -1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    bool ok = true;
    for (int i = 0; i < 16; ++i)
        if (r0.coeff[i] != expect[i]) ok = false;
    std::printf("\n还原正确性：%s\n", ok ? "与目标逐个一致 ✓" : "不一致 ✗");

    // ---- 上下文自适应：同一段比特，不同 nC 选不同表 ----
    std::printf("\n==== 上下文自适应：同一段比特，nC 不同 -> 选不同 coeff_token 表 ====\n");
    std::printf("（coeff_token 是变长码，选错表就把开头几位解成完全不同的 "
                "TotalCoeff/TrailingOnes）\n");
    const int ncs[4] = {0, 2, 4, 8};
    const char* bucket[4] = {"0<=nC<2 (Table 9-5 第0档)",
                             "2<=nC<4 (第1档)",
                             "4<=nC<8 (第2档)",
                             "nC>=8 (定长6位)"};
    std::vector<cfs::CoeffToken> tokens;
    for (int k = 0; k < 4; ++k) {
        cfs::RbspBitReader b(packed.data(), packed.size());
        cfs::CoeffToken t = cfs::DecodeCoeffToken(b, ncs[k]);
        tokens.push_back(t);
        std::printf("  nC=%d  %-26s -> coeff_token 解出 "
                    "TotalCoeff=%2d, TrailingOnes=%d\n",
                    ncs[k], bucket[k], t.total_coeff, t.trailing_ones);
    }

    // ---- nC 是怎么从邻居算出来的 ----
    std::printf("\n==== nC 由邻居块的非零系数个数推出（spec 9.2.1 式 9-4）====\n");
    struct Case {
        int nA;
        bool avA;
        int nB;
        bool avB;
        const char* note;
    };
    Case cases[] = {
        {0, true, 0, true, "左、上邻块都平坦(0 个非零)"},
        {5, true, 3, true, "左5、上3 -> (5+3+1)>>1"},
        {9, true, 8, true, "左9、上8 -> 高活动区 -> nC>=8 选定长码"},
        {6, true, 0, false, "只有左邻可用(上邻越界)"},
        {0, false, 0, false, "都不可用(块在角落)"},
    };
    for (auto& c : cases) {
        int nc = cfs::DeriveNc(c.nA, c.avA, c.nB, c.avB);
        std::printf("  nA=%d(%s) nB=%d(%s) -> nC=%d   %s\n", c.nA,
                    c.avA ? "可用" : "不可用", c.nB, c.avB ? "可用" : "不可用",
                    nc, c.note);
    }

    // ---- dump 供 gen_charts.py ----
    if (!out_dir.empty()) {
        std::string path = out_dir + "/cavlc_dump.txt";
        FILE* f = std::fopen(path.c_str(), "w");
        if (f) {
            std::fprintf(f, "# H.264 CAVLC demo dump（真实标准样例）\n");
            std::fprintf(f, "bits %s\n", kBits.c_str());
            std::fprintf(f, "correct %d\n", ok ? 1 : 0);
            DumpResult(f, "main", r0);
            for (int k = 0; k < 4; ++k) {
                std::fprintf(f, "ctx_nc %d %d %d\n", ncs[k],
                             tokens[k].total_coeff, tokens[k].trailing_ones);
            }
            std::fclose(f);
            std::printf("\ndumped cavlc data -> %s\n", path.c_str());
        }
    }
    return 0;
}
