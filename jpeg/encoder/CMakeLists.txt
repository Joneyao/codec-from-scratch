cmake_minimum_required(VERSION 3.10)
project(cfs_jpeg_encoder CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

# common 以源码方式引入，方便读者看到完整依赖。
set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
include_directories(${COMMON_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

set(COMMON_SRC ${COMMON_DIR}/image_io.cpp)

# A1: 色彩转换
add_executable(color_transform_demo
    color_transform_demo.cpp
    color_transform.cpp
    ${COMMON_SRC})

add_executable(test_color_transform
    test_color_transform.cpp
    color_transform.cpp
    ${COMMON_SRC})

# A2: 8x8 DCT
add_executable(dct8x8_demo
    dct8x8_demo.cpp
    dct8x8.cpp
    color_transform.cpp
    ${COMMON_SRC})

add_executable(test_dct8x8
    test_dct8x8.cpp
    dct8x8.cpp)

enable_testing()
add_test(NAME color_transform COMMAND test_color_transform)
add_test(NAME dct8x8 COMMAND test_dct8x8)
