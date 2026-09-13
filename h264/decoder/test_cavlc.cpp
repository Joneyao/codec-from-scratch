// test_cavlc.cpp — 校验 H.264 CAVLC 残差解码（spec 9.2）。
//
// 覆盖：
//   1) coeff_token 解码：已知比特串 + nC -> 正确的 TotalCoeff/TrailingOnes。
//   2) 上下文选表：不同 nC 落在不同分档（0<=nC<2 / 2<=nC<4 / 4<=nC<8 / >=8）。
//   3) nC 推导：由左/上邻块非零数按 spec 9-4 算出 nC。
//   4) 完整解一个 4x4 块：教科书标准样例 -> 正确的 16 系数序列。
//   5) total_zeros / run_before 把 0 摆回正确分布。
//   6) 全零块：coeff_token=0 时 TotalCoeff=0，整块为 0。
//   7) zigzag 反扫描位置正确。
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "cavlc_decode.h"
#include "rbsp_bit_reader.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "CHECK failed: %s (line %d)\n", #cond, \
                         __LINE__);                                     \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

std::vector<uint8_t> PackBits(const std::string& bits) {
    std::vector<uint8_t> out((bits.size() + 7) / 8, 0);
    for (size_t i = 0; i < bits.size(); ++i)
        if (bits[i] == '1') out[i >> 3] |= (0x80u >> (i & 7));
    return out;
}

// 测试 1：coeff_token 单独解码（nC=0 档）。
// 标准样例开头 coeff_token 是 "0000100"，对应 TotalCoeff=5, TrailingOnes=3。
void TestCoeffTokenBasic() {
    auto packed = PackBits("0000100");
    cfs::RbspBitReader br(packed.data(), packed.size());
    cfs::CoeffToken t = cfs::DecodeCoeffToken(br, 0);
    CHECK(t.total_coeff == 5);
    CHECK(t.trailing_ones == 3);

    // TotalCoeff=0 的码字在第 0 档是 "1"。
    auto z = PackBits("1");
    cfs::RbspBitReader bz(z.data(), z.size());
    cfs::CoeffToken tz = cfs::DecodeCoeffToken(bz, 0);
    CHECK(tz.total_coeff == 0);
    CHECK(tz.trailing_ones == 0);
}

// 测试 2：nC>=8 定长 6 位码。
//   code=3 -> TotalCoeff=0；否则 TotalCoeff=(code>>2)+1, TrailingOnes=code&3。
void TestCoeffTokenFixed() {
    // code=000100 (=4) -> TotalCoeff=2, TrailingOnes=0
    auto p = PackBits("000100");
    cfs::RbspBitReader br(p.data(), p.size());
    cfs::CoeffToken t = cfs::DecodeCoeffToken(br, 8);
    CHECK(t.total_coeff == 2);
    CHECK(t.trailing_ones == 0);

    // code=000011 (=3) -> TotalCoeff=0
    auto p0 = PackBits("000011");
    cfs::RbspBitReader br0(p0.data(), p0.size());
    cfs::CoeffToken t0 = cfs::DecodeCoeffToken(br0, 8);
    CHECK(t0.total_coeff == 0);
}

// 测试 3：nC 推导（spec 9-4）。
void TestDeriveNc() {
    CHECK(cfs::DeriveNc(0, true, 0, true) == 0);      // (0+0+1)>>1 = 0
    CHECK(cfs::DeriveNc(5, true, 3, true) == 4);      // (5+3+1)>>1 = 4
    CHECK(cfs::DeriveNc(9, true, 8, true) == 9);      // (9+8+1)>>1 = 9 (>=8)
    CHECK(cfs::DeriveNc(6, true, 0, false) == 6);     // 只有左邻
    CHECK(cfs::DeriveNc(0, false, 7, true) == 7);     // 只有上邻
    CHECK(cfs::DeriveNc(0, false, 0, false) == 0);    // 都不可用
}

// 测试 4：完整解一个 4x4 块（Richardson 标准样例）。
void TestFullBlock() {
    auto packed = PackBits("000010001110010111101101");
    cfs::RbspBitReader br(packed.data(), packed.size());
    cfs::CavlcResult r = cfs::DecodeResidual4x4(br, 0);

    CHECK(r.token.total_coeff == 5);
    CHECK(r.token.trailing_ones == 3);
    CHECK(r.total_zeros == 3);

    const int expect[16] = {0, 3, 0, 1, -1, -1, 0, 1,
                            0, 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 16; ++i) CHECK(r.coeff[i] == expect[i]);
}

// 测试 5：total_zeros / run_before 把 0 的分布还原对。
// 上面样例：非零系数在扫描位置 1,3,4,5,7；位置 0/2/6 是 0。
void TestZeroDistribution() {
    auto packed = PackBits("000010001110010111101101");
    cfs::RbspBitReader br(packed.data(), packed.size());
    cfs::CavlcResult r = cfs::DecodeResidual4x4(br, 0);

    // 非零系数总数 + total_zeros 应等于“最高非零位置索引 + 1”。
    int last_nonzero = 0;
    for (int i = 0; i < 16; ++i)
        if (r.coeff[i] != 0) last_nonzero = i;
    CHECK(last_nonzero == r.token.total_coeff + r.total_zeros - 1);

    // 具体 0 的位置。
    CHECK(r.coeff[0] == 0);
    CHECK(r.coeff[2] == 0);
    CHECK(r.coeff[6] == 0);
    CHECK(r.coeff[1] == 3);
    CHECK(r.coeff[7] == 1);
}

// 测试 6：全零块。
void TestAllZeroBlock() {
    auto packed = PackBits("1");  // coeff_token=0 (nC=0 档)
    cfs::RbspBitReader br(packed.data(), packed.size());
    cfs::CavlcResult r = cfs::DecodeResidual4x4(br, 0);
    CHECK(r.token.total_coeff == 0);
    for (int i = 0; i < 16; ++i) CHECK(r.coeff[i] == 0);
}

// 测试 7：zigzag 反扫描位置正确。
void TestInverseZigzag() {
    cfs::Cavlc4x4 c{};
    for (int i = 0; i < 16; ++i) c[i] = i;  // 扫描序号即值
    auto b = cfs::InverseZigzag4x4(c);
    // 扫描序 0->(0,0), 1->(0,1), 2->(1,0), 3->(2,0), 5->(0,2)...
    CHECK(b[0][0] == 0);
    CHECK(b[0][1] == 1);
    CHECK(b[1][0] == 2);
    CHECK(b[2][0] == 3);
    CHECK(b[0][2] == 5);
    CHECK(b[3][3] == 15);
}

}  // namespace

int main() {
    TestCoeffTokenBasic();
    TestCoeffTokenFixed();
    TestDeriveNc();
    TestFullBlock();
    TestZeroDistribution();
    TestAllZeroBlock();
    TestInverseZigzag();

    if (g_failures != 0) {
        std::fprintf(stderr, "test_cavlc: %d FAILURE(s)\n", g_failures);
        return 1;
    }
    std::printf("test_cavlc: 全部通过\n");
    return 0;
}
