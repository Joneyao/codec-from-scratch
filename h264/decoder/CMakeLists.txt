cmake_minimum_required(VERSION 3.10)
project(cfs_h264_decoder CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
# C4 的 demo 复用 common/ 的 PPM 读写（image_io）。
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../common)

# C1：Annex B 切分 + Emulation Prevention 还原。
# 目前只依赖 nal_splitter 自身，不牵动 common/（bitreader 等留给 C2）。
add_executable(nal_splitter_demo
    nal_splitter_demo.cpp
    nal_splitter.cpp)

add_executable(test_nal_splitter
    test_nal_splitter.cpp
    nal_splitter.cpp)

# C2：指数哥伦布 + SPS/PPS 解析（读出真实分辨率）。
add_executable(sps_pps_demo
    sps_pps_demo.cpp
    sps_pps_parser.cpp
    rbsp_bit_reader.cpp
    nal_splitter.cpp)

add_executable(test_sps_pps
    test_sps_pps.cpp
    sps_pps_parser.cpp
    rbsp_bit_reader.cpp
    nal_splitter.cpp)

# C3：slice header 解析 + 宏块网格划分（把一帧切成 16x16 的宏块）。
add_executable(macroblock_demo
    macroblock_demo.cpp
    macroblock_layout.cpp
    slice_header.cpp
    sps_pps_parser.cpp
    rbsp_bit_reader.cpp
    nal_splitter.cpp)

add_executable(test_macroblock
    test_macroblock.cpp
    macroblock_layout.cpp
    slice_header.cpp
    sps_pps_parser.cpp
    rbsp_bit_reader.cpp
    nal_splitter.cpp)

# C4：帧内预测（Intra_4x4 的 9 种模式 + Intra_16x16 的 4 种模式）。
# demo 读真实 PPM 图，跑全部模式并算 SAD；纯预测逻辑不依赖码流解析模块。
add_executable(intra_predict_demo
    intra_predict_demo.cpp
    intra_predict.cpp
    ../../common/image_io.cpp)

add_executable(test_intra_predict
    test_intra_predict.cpp
    intra_predict.cpp)

enable_testing()
# 解析真实 test.h264（相对 build 目录：h264/decoder/build/ -> ../../samples/）。
add_test(NAME nal_splitter
         COMMAND test_nal_splitter ${CMAKE_CURRENT_SOURCE_DIR}/../../samples/test.h264)
add_test(NAME sps_pps
         COMMAND test_sps_pps ${CMAKE_CURRENT_SOURCE_DIR}/../../samples/test.h264)
add_test(NAME macroblock
         COMMAND test_macroblock ${CMAKE_CURRENT_SOURCE_DIR}/../../samples/test.h264)
# C4：帧内预测公式校验（构造输入，可手算核对，不需要外部文件）。
add_test(NAME intra_predict COMMAND test_intra_predict)
