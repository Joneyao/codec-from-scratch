# common

跨模块共享的基础设施代码。

- `bitreader.h/.cpp` — 手写位读取器（大端比特序），支持指数哥伦布解码
- `bitwriter.h/.cpp` — 手写位写入器，编码器侧使用
- `yuv_utils.h/.cpp` — YUV420/RGB 互转、PSNR 计算
- `nal_common.h` — H.264/H.265 共享的 NAL 类型定义（H.264 系列专用，JPEG 系列不依赖此文件）

这个目录不单独编译成库，被各子模块的 CMakeLists.txt 以源码方式引入，方便每篇文章的读者直接看到完整依赖，不需要额外链接步骤。
