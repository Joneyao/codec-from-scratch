cmake_minimum_required(VERSION 3.10)
project(cfs_mjpeg_encoder CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

# 复用 common 与 jpeg/encoder 的源码（MJPEG = 逐帧 JPEG + 容器）。
set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(COMMON_DIR ${ROOT_DIR}/common)
set(JPEG_ENC_DIR ${ROOT_DIR}/jpeg/encoder)

include_directories(
    ${COMMON_DIR}
    ${JPEG_ENC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR})

# jpeg/encoder 里被 MJPEG 复用的整条编码流水线。
set(JPEG_ENC_SRC
    ${JPEG_ENC_DIR}/jpeg_encode_api.cpp
    ${JPEG_ENC_DIR}/jfif_writer.cpp
    ${JPEG_ENC_DIR}/zigzag_rle.cpp
    ${JPEG_ENC_DIR}/huffman_encode.cpp
    ${JPEG_ENC_DIR}/quantize.cpp
    ${JPEG_ENC_DIR}/dct8x8.cpp
    ${JPEG_ENC_DIR}/color_transform.cpp)

set(COMMON_SRC
    ${COMMON_DIR}/image_io.cpp
    ${COMMON_DIR}/bitwriter.cpp)

# B1: MJPEG 编码器主程序。
add_executable(mjpeg_encoder
    mjpeg_encoder_main.cpp
    frame_sequencer.cpp
    avi_muxer.cpp
    ${JPEG_ENC_SRC}
    ${COMMON_SRC})

# AVI 容器结构单元测试（不依赖 jpeg 流水线，只测 muxer）。
add_executable(test_avi_muxer
    test_avi_muxer.cpp
    avi_muxer.cpp)

enable_testing()
add_test(NAME avi_muxer COMMAND test_avi_muxer)
