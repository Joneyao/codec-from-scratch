// macroblock_layout.cpp — 宏块网格几何与 mb_type 分类实现。
#include "macroblock_layout.h"

namespace cfs {

const char* MbClassName(MbClass c) {
    switch (c) {
        case MbClass::kIntra4x4:   return "I_4x4";
        case MbClass::kIntra16x16: return "I_16x16";
        case MbClass::kIPcm:       return "I_PCM";
        case MbClass::kPInter:     return "P_inter";
        case MbClass::kPSkip:      return "P_Skip";
        default: return "?";
    }
}

MbGrid MbGrid::FromSps(const Sps& sps) {
    MbGrid g;
    g.width_in_mbs = sps.PicWidthInMbs();
    g.height_in_mbs = sps.FrameHeightInMbs();
    return g;
}

MbClass ClassifyISliceMbType(uint32_t mb_type) {
    // 表 7-8：I slice 的 mb_type 取值 0..25。
    if (mb_type == 0) return MbClass::kIntra4x4;    // I_4x4
    if (mb_type >= 1 && mb_type <= 24) return MbClass::kIntra16x16;  // I_16x16 变体
    if (mb_type == 25) return MbClass::kIPcm;       // I_PCM
    return MbClass::kUnknown;
}

MbClass ClassifyPSliceMbType(uint32_t mb_type) {
    // 表 7-10：P/SP slice 的 mb_type 0..4 是帧间划分
    //   0=P_L0_16x16 1=P_L0_L0_16x8 2=P_L0_L0_8x16 3=P_8x8 4=P_8x8ref0
    // mb_type >= 5 时按 7.4.5 落入 I 宏块编号（值减 5 走 I slice 表），
    // 本篇 P 上下文只区分"帧间 vs 帧内"，故 >=5 归为帧内。
    if (mb_type <= 4) return MbClass::kPInter;
    return ClassifyISliceMbType(mb_type - 5);
}

}  // namespace cfs
