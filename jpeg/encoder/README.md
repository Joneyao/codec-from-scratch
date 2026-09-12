# jpeg/encoder

基线（Baseline Sequential DCT）JPEG 编码器，对应《从0到1手搓JPEG编码器》系列文章。

## 模块与文章对应关系

| 源文件 | 对应文章 | 核心内容 |
|---|---|---|
| `color_transform.cpp` | #01 | RGB → YCbCr 转换、4:2:0 色度抽样 |
| `dct8x8.cpp` | #02 | 8x8 浮点/整数 DCT 正变换 |
| `quantize.cpp` | #03 | 量化表（亮度/色度分离）、Q 因子与质量的关系 |
| `zigzag_rle.cpp` | #04 | Zig-Zag 扫描、DC 差分编码、AC 的 Run-Length 编码 |
| `huffman_encode.cpp` | #04 | 标准霍夫曼表构建与编码（DC/AC 分离） |
| `jfif_writer.cpp` | #05 | JFIF 文件结构（SOI/APP0/DQT/DHT/SOF/SOS/EOI）封装 |
| `encoder_main.cpp` | #05 | 整合以上模块，编码真实 BMP/PPM 图片为 .jpg |

## 构建

```bash
mkdir build && cd build
cmake .. && make
./jpeg_encoder ../../samples/lena.ppm output.jpg --quality 80
```

## 验证方式

输出的 .jpg 用系统自带图片查看器和 libjpeg-turbo 的 `djpeg` 分别解码，确认能被通用解码器正确打开；再和 libjpeg-turbo 编码同一张图的结果做文件大小和 PSNR 对比。
