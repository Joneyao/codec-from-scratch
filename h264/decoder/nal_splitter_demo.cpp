// nal_splitter_demo.cpp — 解析一个真实的 .h264 裸流，dump 每个 NAL Unit 的
// type / ref_idc / 偏移 / 大小 / RBSP 长度，并统计全流的 emulation prevention
// byte 出现次数。输出可直接喂给配图脚本。
//
// 用法：
//   ./nal_splitter_demo ../../samples/test.h264 [chart_data/nal_dump.txt]
#include <cstdio>
#include <string>

#include "nal_splitter.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s <input.h264> [dump.txt]\n", argv[0]);
        return 1;
    }
    const std::string in_path = argv[1];

    std::vector<uint8_t> bytes = cfs::ReadFileBytes(in_path);
    if (bytes.empty()) {
        std::printf("error: cannot read %s\n", in_path.c_str());
        return 1;
    }
    std::printf("input: %s  (%zu bytes)\n\n", in_path.c_str(), bytes.size());

    cfs::NalSplitResult r = cfs::SplitAnnexB(bytes);
    if (!r.ok) {
        std::printf("split failed: %s\n", r.error.c_str());
        return 1;
    }

    std::printf("%-4s %-14s %-4s %-4s %-9s %-8s %-9s %-6s\n",
                "idx", "type", "sc", "fzb", "offset", "nalSize",
                "rbspSize", "ep3b");
    std::printf("---------------------------------------------------------------------\n");
    for (size_t i = 0; i < r.units.size(); ++i) {
        const cfs::NalUnit& u = r.units[i];
        std::printf("%-4zu %-14s %zuB   %d    %-9zu %-8zu %-9zu %-6d\n",
                    i, u.TypeName().c_str(), u.start_code_len,
                    u.forbidden_zero_bit, u.nal_offset, u.nal_size,
                    u.rbsp.size(), u.emulation_bytes_removed);
    }
    std::printf("\n");
    std::printf("total NAL units: %zu\n", r.units.size());
    std::printf("total emulation_prevention_three_byte removed: %d\n",
                r.total_emulation_bytes_removed);

    // 类型计数汇总。
    int cnt[32] = {0};
    for (const auto& u : r.units) cnt[u.nal_unit_type]++;
    std::printf("type histogram: SPS=%d PPS=%d SEI=%d IDR=%d nonIDR=%d\n",
                cnt[cfs::kNalSps], cnt[cfs::kNalPps], cnt[cfs::kNalSei],
                cnt[cfs::kNalSliceIdr], cnt[cfs::kNalSliceNonIdr]);

    // 可选：把逐 NAL 数据写成 txt，供 gen_charts.py 读取（真实数据配图）。
    if (argc >= 3) {
        const std::string dump_path = argv[2];
        FILE* out = std::fopen(dump_path.c_str(), "w");
        if (out) {
            std::fprintf(out, "# idx type_name type_id ref_idc sc_len "
                              "nal_offset nal_size rbsp_size ep3b\n");
            for (size_t i = 0; i < r.units.size(); ++i) {
                const cfs::NalUnit& u = r.units[i];
                std::fprintf(out, "%zu %s %d %d %zu %zu %zu %zu %d\n",
                             i, u.TypeName().c_str(), u.nal_unit_type,
                             u.nal_ref_idc, u.start_code_len, u.nal_offset,
                             u.nal_size, u.rbsp.size(),
                             u.emulation_bytes_removed);
            }
            std::fprintf(out, "# total_ep3b %d\n",
                         r.total_emulation_bytes_removed);
            std::fclose(out);
            std::printf("\ndumped per-NAL table -> %s\n", dump_path.c_str());
        }
    }
    return 0;
}
