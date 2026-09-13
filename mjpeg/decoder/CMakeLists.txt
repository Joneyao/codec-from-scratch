cmake_minimum_required(VERSION 3.10)
project(cfs_mjpeg_decoder CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

# MJPEG 解码 = 拆 AVI 容器 + 复用整条 JPEG 解码器。
set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(COMMON_DIR ${ROOT_DIR}/common)
set(JPEG_ENC_DIR ${ROOT_DIR}/jpeg/encoder)
set(JPEG_DEC_DIR ${ROOT_DIR}/jpeg/decoder)

include_directories(
    ${COMMON_DIR}
    ${JPEG_ENC_DIR}
    ${JPEG_DEC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR})

# common：PPM 读写 + 位读写。
set(COMMON_SRC
    ${COMMON_DIR}/image_io.cpp
    ${COMMON_DIR}/bitreader.cpp
    ${COMMON_DIR}/bitwriter.cpp)

# jpeg/encoder 里被解码器复用的源（Zig-Zag 表 / 标准霍夫曼表 / 量化表 / DCT）。
set(JPEG_ENC_REUSE_SRC
    ${JPEG_ENC_DIR}/zigzag_rle.cpp
    ${JPEG_ENC_DIR}/huffman_encode.cpp
    ${JPEG_ENC_DIR}/quantize.cpp
    ${JPEG_ENC_DIR}/dct8x8.cpp)

# jpeg/decoder 的完整流水线（A6 解析 + A7 熵解码 + A8 整图重建）。
set(JPEG_DEC_SRC
    ${JPEG_DEC_DIR}/jfif_parser.cpp
    ${JPEG_DEC_DIR}/huffman_decode.cpp
    ${JPEG_DEC_DIR}/entropy_decode.cpp
    ${JPEG_DEC_DIR}/block_reconstruct.cpp
    ${JPEG_DEC_DIR}/jpeg_decoder.cpp)

# B2: MJPEG 解码器主程序（解封装 + 逐帧解码）。
add_executable(mjpeg_decoder
    mjpeg_decoder_main.cpp
    avi_demuxer.cpp
    ${JPEG_DEC_SRC}
    ${JPEG_ENC_REUSE_SRC}
    ${COMMON_SRC})

# AVI 解封装单元测试（只依赖 avi_demuxer，不牵动 JPEG 流水线）。
add_executable(test_avi_demuxer
    test_avi_demuxer.cpp
    avi_demuxer.cpp)

enable_testing()
# 测试解析 encoder 产出的真实 out.avi（相对 build 目录的路径）。
add_test(NAME avi_demuxer
         COMMAND test_avi_demuxer ${CMAKE_CURRENT_SOURCE_DIR}/../encoder/out.avi)
