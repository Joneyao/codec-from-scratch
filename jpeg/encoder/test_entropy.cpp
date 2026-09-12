// test_entropy.cpp — Zig-Zag / 游程 / 霍夫曼 熵编码最小单元测试。
#include <array>
#include <cstdio>
#include <vector>

#include "huffman_encode.h"
#include "zigzag_rle.h"

namespace {
int g_failed = 0;
void Check(bool c, const char* m) {
    std::printf(c ? "[ok]   %s\n" : "[FAIL] %s\n", m);
    if (!c) ++g_failed;
}
}  // namespace

int main() {
    // 1. Zig-Zag 顺序正确：第 0 个是 [0][0]（DC），几个关键位置对得上 T.81 Fig A.6。
    {
        Check(cfs::kZigZagOrder[0] == 0, "zigzag[0] is DC (index 0)");
        Check(cfs::kZigZagOrder[1] == 1, "zigzag[1] is [0][1]");
        Check(cfs::kZigZagOrder[2] == 8, "zigzag[2] is [1][0]");
        Check(cfs::kZigZagOrder[63] == 63, "zigzag[63] is [7][7] (highest freq)");
        // 顺序应是 0..63 的一个排列（每个索引恰好出现一次）。
        std::array<int, 64> seen{};
        bool perm = true;
        for (int k = 0; k < 64; ++k) {
            int idx = cfs::kZigZagOrder[k];
            if (idx < 0 || idx >= 64 || seen[idx]) perm = false;
            seen[idx] = 1;
        }
        Check(perm, "zigzag order is a permutation of 0..63");
    }

    // 2. 全 0 块：AC 编码后就是一个 EOB 符号，别的什么都没有。
    {
        std::array<int, 64> zz{};  // 全 0
        std::vector<cfs::RleSymbol> ac = cfs::RunLengthEncodeAc(zz);
        Check(ac.size() == 1, "all-zero AC produces exactly one symbol");
        Check(ac[0].run == 0 && ac[0].value == 0,
              "that single symbol is EOB {0,0}");
    }

    // 3. 尾部无 0（最后一个 AC 非零）时不应产生 EOB。
    {
        std::array<int, 64> zz{};
        zz[63] = 7;  // 最后一个位置非零
        std::vector<cfs::RleSymbol> ac = cfs::RunLengthEncodeAc(zz);
        bool has_eob = false;
        for (const auto& s : ac)
            if (s.run == 0 && s.value == 0) has_eob = true;
        Check(!has_eob, "no EOB when the last AC coefficient is nonzero");
    }

    // 4. 长游程：连续 20 个 0 后一个非零，应先吐 ZRL(16 连 0) 再吐 {4,value}。
    {
        std::array<int, 64> zz{};
        zz[21] = 3;  // 前面 index 1..20 共 20 个 0
        std::vector<cfs::RleSymbol> ac = cfs::RunLengthEncodeAc(zz);
        // 20 连 0 -> ZRL(16) + {4,3}，其后 index 22..63 全 0 -> EOB，共 3 个符号。
        Check(ac.size() == 3, "run of 20 zeros -> ZRL + {4,3} + EOB");
        Check(ac[0].run == 15 && ac[0].value == 0, "first symbol is ZRL {15,0}");
        Check(ac[1].run == 4 && ac[1].value == 3, "second symbol is {4,3}");
        Check(ac[2].run == 0 && ac[2].value == 0, "third symbol is EOB {0,0}");
    }

    // 5. 编码可逆：RLE 序列 + DC 能重建原 zigzag 序列。
    {
        std::array<int, 64> zz{};
        zz[0] = -42;  // DC
        zz[1] = 5; zz[2] = 0; zz[3] = -3;
        zz[10] = 2; zz[40] = -1;  // 中间散落非零
        std::vector<cfs::RleSymbol> ac = cfs::RunLengthEncodeAc(zz);
        std::array<int, 64> back = cfs::RebuildZigZag(zz[0], ac);
        bool same = true;
        for (int k = 0; k < 64; ++k)
            if (zz[k] != back[k]) same = false;
        Check(same, "RebuildZigZag reconstructs the original sequence");
    }

    // 6. Category（SSSS）边界值符合 T.81 Table F.1。
    {
        Check(cfs::Category(0) == 0, "category(0)=0");
        Check(cfs::Category(1) == 1 && cfs::Category(-1) == 1, "category(+-1)=1");
        Check(cfs::Category(2) == 2 && cfs::Category(3) == 2, "category(2,3)=2");
        Check(cfs::Category(-7) == 3 && cfs::Category(4) == 3, "category(4..7)=3");
        Check(cfs::Category(255) == 8, "category(255)=8");
    }

    // 7. 幅值编码：正数取本身，负数取补码（T.81 F.1.2.1）。
    //    +5(size3)=101b=5；-5(size3)=010b=2（即 5 的按位取反低 3 位）。
    {
        Check(cfs::MagnitudeBits(5, 3) == 5u, "magnitude(+5,3)=101b");
        Check(cfs::MagnitudeBits(-5, 3) == 2u, "magnitude(-5,3)=010b");
        Check(cfs::MagnitudeBits(-1, 1) == 0u, "magnitude(-1,1)=0b");
        Check(cfs::MagnitudeBits(1, 1) == 1u, "magnitude(+1,1)=1b");
    }

    // 8. 标准霍夫曼表构建正确：DC size=0 的码字是 '00'（2 bit，T.81 Table K.3）。
    {
        const cfs::HuffTable& dc = cfs::StdLumaDcTable();
        Check(dc.len[0] == 2 && dc.code[0] == 0, "luma DC cat0 = '00' (len2)");
        Check(dc.len[1] == 3 && dc.code[1] == 0b010,
              "luma DC cat1 = '010' (len3)");
        const cfs::HuffTable& ac = cfs::StdLumaAcTable();
        // EOB(0x00) 码字是 '1010'(len4)，ZRL(0xF0) 是 '11111111001'(len11)。
        Check(ac.len[0x00] == 4 && ac.code[0x00] == 0b1010,
              "luma AC EOB = '1010' (len4)");
        Check(ac.len[0xF0] == 11, "luma AC ZRL has code length 11");
    }

    // 9. 端到端 bit 数合理：编码一个真实感块，实际 bit 数应远小于朴素 512 bit。
    {
        std::array<int, 64> zz{};
        zz[0] = 12; zz[1] = -3; zz[2] = 1; zz[5] = 2;  // 其余全 0
        int dc_diff = zz[0];
        std::vector<cfs::RleSymbol> ac = cfs::RunLengthEncodeAc(zz);
        cfs::BitWriter bw;
        size_t bits = cfs::EncodeBlock(dc_diff, ac, cfs::StdLumaDcTable(),
                                       cfs::StdLumaAcTable(), bw);
        Check(bits > 0 && bits < 512,
              "sparse block encodes to far fewer than 512 bits");
    }

    if (g_failed == 0) { std::printf("\nALL TESTS PASSED\n"); return 0; }
    std::printf("\n%d TEST(S) FAILED\n", g_failed);
    return 1;
}
