# Spec Reference — 标准原文参考

本目录存放本项目实现所依据的官方标准文本，供写作和实现时对照条款号使用。

## 版权声明（重要）

以下标准文本的版权归 **国际电信联盟（ITU）** 所有。ITU-T Recommendations 在定稿后对公众免费开放下载，但版权仍归 ITU。本目录收录这些 PDF 仅用于学习和实现参考，不构成再许可。如需引用或分发，请以 ITU 官方版本为准。

官方免费下载地址：
- H.264: https://www.itu.int/rec/T-REC-H.264
- T.81 (JPEG): https://www.itu.int/rec/T-REC-T.81

## 文件清单

| 文件 | 标准 | 版本 | 页数 | 用途 |
|---|---|---|---|---|
| `ITU-T-H264-200305.pdf` | ITU-T H.264 / ISO/IEC 14496-10 AVC | 2003-05（首版，条款编号与后续版本基本一致） | 282 | H.264 编码器/解码器实现的语法与算法依据 |
| `ITU-T-T81-JPEG.pdf` | ITU-T T.81 / ISO/IEC 10918-1 JPEG | 1992-09 | 186 | JPEG/MJPEG 编码器实现依据 |

## 常用条款速查（实现时对照）

### H.264（ITU-T H.264）

| 条款 | 内容 | 对应本项目模块 |
|---|---|---|
| 7.3.1 | NAL unit 语法 | `h264/decoder/nal_splitter.cpp` |
| 7.4.1 | Emulation prevention（防竞争字节） | `h264/decoder/nal_splitter.cpp` |
| 9.1 | 指数哥伦布编码（Exp-Golomb） | `common/bitreader.cpp` |
| 7.3.2.1 | SPS 语法 | `h264/decoder/sps_pps_parser.cpp` |
| 7.3.2.2 | PPS 语法 | `h264/decoder/sps_pps_parser.cpp` |
| 7.3.3 | Slice header 语法 | `h264/decoder/slice_header.cpp` |
| 8.3 | 帧内预测过程 | `h264/decoder/intra_predict.cpp` |
| 8.5 | 变换与量化（4x4 整数变换） | `h264/decoder/transform_quant.cpp` |
| 9.2 | CAVLC 解析 | `h264/decoder/cavlc_decode.cpp` |
| 8.4 | 帧间预测 / 运动补偿 | `h264/decoder/motion_compensate.cpp` |
| 8.7 | 去块滤波（Deblocking Filter） | `h264/decoder/deblock_filter.cpp` |

### JPEG（ITU-T T.81）

| 条款 | 内容 | 对应本项目模块 |
|---|---|---|
| Annex A | DCT 定义 | `jpeg/encoder/dct8x8.cpp` |
| Annex F | 基线顺序 DCT 编码过程 | `jpeg/encoder/encoder_main.cpp` |
| Annex K | 推荐量化表与霍夫曼表 | `jpeg/encoder/quantize.cpp` / `huffman_encode.cpp` |
| B.2 | 标记段（Marker）与文件结构 | `jpeg/encoder/jfif_writer.cpp` |

> 条款号以文件内实际章节为准，写作时请打开对应 PDF 核对具体页码。
