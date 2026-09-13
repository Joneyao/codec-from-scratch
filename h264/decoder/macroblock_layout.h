// macroblock_layout.h — 把一帧组织成 16x16 的宏块网格，并解析宏块类型。
//
// 一帧的宽高（以像素计）在 C2 里已从 SPS 反推出来。这里更进一步：一帧被划成
// PicWidthInMbs × FrameHeightInMbs 个宏块(Macroblock,MB)，每个宏块 16x16 像素。
// 宏块是预测和变换的基本单位。
//
// 两件事：
//   1) 宏块地址(mbAddr) ↔ 网格坐标(x,y) 的互相映射。mbAddr 按光栅扫描(raster
//      scan)编号：从左到右、从上到下，第 0 个在左上角。这是 slice 里宏块的排列
//      顺序（ITU-T H.264 7.4.4、公式 InverseRasterScan）。
//   2) 宏块类型(mb_type) 的解析与分类。IDR 帧里全是 I(帧内)宏块；P 帧里会出现
//      P(帧间预测)宏块和 skip(跳过)宏块。本篇只解出类型并分类，不解残差。
//
// 类型编号对照：I slice 见表 7-8，P/SP slice 见表 7-10。
#ifndef CODEC_FROM_SCRATCH_H264_MACROBLOCK_LAYOUT_H
#define CODEC_FROM_SCRATCH_H264_MACROBLOCK_LAYOUT_H

#include <cstdint>

#include "slice_header.h"

namespace cfs {

// 宏块的粗分类（本篇只关心到这个粒度）。
enum class MbClass {
    kIntra4x4,    // I_4x4：帧内，16x16 里再切 16 个 4x4 子块各自预测
    kIntra16x16,  // I_16x16：帧内，整块一个方向预测
    kIPcm,        // I_PCM：不预测不变换，直接存原始像素
    kPInter,      // P_L0_*：帧间预测（用参考帧的块）
    kPSkip,       // P_Skip：跳过，直接抄参考帧同位置
    kUnknown,
};

const char* MbClassName(MbClass c);

// 网格几何：宽高（以宏块计）与总宏块数。
struct MbGrid {
    uint32_t width_in_mbs = 0;   // PicWidthInMbs
    uint32_t height_in_mbs = 0;  // FrameHeightInMbs
    uint32_t Total() const { return width_in_mbs * height_in_mbs; }

    // 从 SPS 建立网格几何。
    static MbGrid FromSps(const Sps& sps);

    // 光栅扫描：mbAddr → (x,y)（列, 行），均以宏块为单位。
    void AddrToXY(uint32_t mb_addr, uint32_t* x, uint32_t* y) const {
        *x = mb_addr % width_in_mbs;
        *y = mb_addr / width_in_mbs;
    }
    // (x,y) → mbAddr。
    uint32_t XYToAddr(uint32_t x, uint32_t y) const {
        return y * width_in_mbs + x;
    }
    // 像素坐标（宏块左上角在整帧里的位置）。
    void AddrToPixel(uint32_t mb_addr, uint32_t* px, uint32_t* py) const {
        uint32_t x, y;
        AddrToXY(mb_addr, &x, &y);
        *px = x * 16;
        *py = y * 16;
    }
};

// 把 I slice 的 mb_type 原始值映射到粗分类（表 7-8：0=I_4x4，1..24=I_16x16，
// 25=I_PCM）。
MbClass ClassifyISliceMbType(uint32_t mb_type);

// 把 P/SP slice 的 mb_type 映射到粗分类（表 7-10：0..4 是各种 P 帧间划分；
// 值 >=5 时会落到 I 宏块编号区间，此处按 P 上下文只区分帧间/其它）。
MbClass ClassifyPSliceMbType(uint32_t mb_type);

}  // namespace cfs

#endif  // CODEC_FROM_SCRATCH_H264_MACROBLOCK_LAYOUT_H
