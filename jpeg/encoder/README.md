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
cmake -S . -B build && cmake --build build
ctest --test-dir build            # 5 组单元测试

# 编码：PPM 进，.jpg 出，第三个参数是 quality(1-100，默认 90)
./build/encoder_main ../../samples/test_pattern.ppm out.jpg 90
```

## 验证方式

输出的 .jpg 用系统图片查看器、`ffmpeg`/`ffprobe`、以及 libjpeg（Pillow 底层即
libjpeg-turbo）分别解码，确认能被通用解码器正确打开；再和 libjpeg 编码同一张图的
结果做文件大小对比。若装了 libjpeg-turbo，也可以用 `djpeg out.jpg > /dev/null` 验证。

`verify_jpg.py` 把上述验证一键跑完：编多个 quality 的 .jpg，逐个用 libjpeg 解码，
测量文件大小并对原图算 PSNR，结果写入 `chart_data/`（供文章配图脚本引用）。

```bash
python3 verify_jpg.py ./build/encoder_main ../../samples/test_pattern.ppm chart_data/
```

> `out.jpg` 与 `chart_data/` 下的图片属于可再生成的产物，已在 `.gitignore` 中忽略；
> 仓库只保留 `chart_data/*.txt` 这类小体积的真实测量数据。
