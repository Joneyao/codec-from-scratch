# jpeg/decoder

基线 JPEG 解码器，对应《手搓 JPEG 解码器》系列文章（A6-A8）。复用 `jpeg/encoder` 里的量化表、Zig-Zag 表等定义，实现完整的"读懂真实 .jpg"能力。

## 模块与文章对应关系

| 源文件 | 对应文章 | 核心内容 |
|---|---|---|
| `jfif_parser.cpp` | A6 | 扫描 marker、解析 DQT/DHT/SOF/SOS、EXIF/APP 段 |
| `huffman_decode.cpp` | A6 | 构建霍夫曼查表、变长解码 |
| `idct8x8.cpp` | A7 | 8x8 反离散余弦变换 |
| `dequantize.cpp` | A7 | 反量化、反 Zig-Zag |
| `jpeg_decoder_main.cpp` | A8 | YCbCr→RGB、色度上采样、RST 标记容错、整合、PSNR 校对 |

## 构建

```bash
mkdir build && cd build
cmake .. && make
./jpeg_decoder ../../samples/camera_photo.jpg output.ppm
```

## 验证方式

解码真实相机/Photoshop 生成的 .jpg，输出 PPM，与 libjpeg-turbo 的 `djpeg` 解码同一文件的结果做逐像素 PSNR 对比。重点验证重启标记（RST marker）、4:2:0/4:2:2 不同色度抽样的正确处理。
