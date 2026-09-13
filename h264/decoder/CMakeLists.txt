cmake_minimum_required(VERSION 3.10)
project(cfs_h264_decoder CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# C1：Annex B 切分 + Emulation Prevention 还原。
# 目前只依赖 nal_splitter 自身，不牵动 common/（bitreader 等留给 C2）。
add_executable(nal_splitter_demo
    nal_splitter_demo.cpp
    nal_splitter.cpp)

add_executable(test_nal_splitter
    test_nal_splitter.cpp
    nal_splitter.cpp)

enable_testing()
# 解析真实 test.h264（相对 build 目录：h264/decoder/build/ -> ../../samples/）。
add_test(NAME nal_splitter
         COMMAND test_nal_splitter ${CMAKE_CURRENT_SOURCE_DIR}/../../samples/test.h264)
