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

enable_testing()
add_test(NAME color_transform COMMAND test_color_transform)
