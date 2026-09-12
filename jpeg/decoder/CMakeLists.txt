cmake_minimum_required(VERSION 3.10)
project(cfs_jpeg_decoder CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

# 复用 common 与 encoder 的定义（量化表 / Zig-Zag 表 / 标准霍夫曼表）。
set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
set(ENCODER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../encoder)
include_directories(${COMMON_DIR} ${ENCODER_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

set(BITREADER_SRC ${COMMON_DIR}/bitreader.cpp)
# huffman_encode.cpp（复用其 BuildHuffTable 与标准表）依赖 BitWriter。
set(BITWRITER_SRC ${COMMON_DIR}/bitwriter.cpp)

# encoder 里被解码器复用的源（提供 kZigZagOrder / 标准表 / QuantTable 常量）。
set(ENCODER_REUSE_SRC
    ${ENCODER_DIR}/zigzag_rle.cpp
    ${ENCODER_DIR}/huffman_encode.cpp
    ${ENCODER_DIR}/quantize.cpp
    ${ENCODER_DIR}/dct8x8.cpp)

# A6: JFIF 解析 + 霍夫曼解码表重建
set(DECODER_A6_SRC
    jfif_parser.cpp
    huffman_decode.cpp
    ${BITREADER_SRC}
    ${BITWRITER_SRC}
    ${ENCODER_REUSE_SRC})

add_executable(parser_demo
    parser_demo.cpp
    ${DECODER_A6_SRC})

add_executable(test_jpeg_parse
    test_jpeg_parse.cpp
    ${DECODER_A6_SRC})

enable_testing()
add_test(NAME jpeg_parse COMMAND test_jpeg_parse)
