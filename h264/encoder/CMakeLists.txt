cmake_minimum_required(VERSION 3.10)
project(cfs_h264_encoder CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
# 复用 decoder 里已按 spec 实现的帧内预测（intra_predict.{h,cpp}）。
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../decoder)
# demo 读真实 PPM 复用 common/ 的 image_io。
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../common)

# D1：帧内预测模式决策（RDO 简化版：试遍所有模式，SAD/SATD 选最小）。
# 预测逻辑复用 decoder/intra_predict.cpp，本模块只做「代价计算 + 模式选择」。
add_executable(intra_mode_demo
    intra_mode_demo.cpp
    intra_mode_decision.cpp
    ../decoder/intra_predict.cpp
    ../../common/image_io.cpp)

add_executable(test_intra_mode_decision
    test_intra_mode_decision.cpp
    intra_mode_decision.cpp
    ../decoder/intra_predict.cpp)

# demo/test 运行时把 chart_data 写到当前源目录下，先建好目录。
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/chart_data)

# D2：正向 4x4 整数变换 + 量化。编码侧对偶于 decoder 的反变换/反量化。
# 往返验证需要 decoder 的 transform_quant.cpp（反量化 + 反变换）。
add_executable(forward_transform_quant_demo
    forward_transform_quant_demo.cpp
    forward_transform_quant.cpp)

add_executable(test_forward_transform_quant
    test_forward_transform_quant.cpp
    forward_transform_quant.cpp
    ../decoder/transform_quant.cpp)

# D3：CAVLC 正向熵编码。编码侧对偶于 decoder 的 CAVLC 解码。
# 往返验证需要 decoder 的 cavlc_decode.cpp + rbsp_bit_reader.cpp，
# 输出比特用 common 的 bitwriter.cpp。
# demo 里做编码→解码往返展示，故也需要 decoder 的 CAVLC 解码 + 位读取。
add_executable(cavlc_encode_demo
    cavlc_encode_demo.cpp
    cavlc_encode.cpp
    ../decoder/cavlc_decode.cpp
    ../decoder/rbsp_bit_reader.cpp
    ../../common/bitwriter.cpp)

add_executable(test_cavlc_encode
    test_cavlc_encode.cpp
    cavlc_encode.cpp
    ../decoder/cavlc_decode.cpp
    ../decoder/rbsp_bit_reader.cpp
    ../../common/bitwriter.cpp)

# D4：运动估计（全搜索 vs 菱形搜索）。整像素搜索，demo 读真实 PPM。
add_executable(motion_search_demo
    motion_search_demo.cpp
    motion_search.cpp
    ../../common/image_io.cpp)

add_executable(test_motion_search
    test_motion_search.cpp
    motion_search.cpp
    ../../common/image_io.cpp)

# D5：码率控制（固定 QP vs CBR 缓冲区模型）。相对独立，无外部依赖。
add_executable(rate_control_demo
    rate_control_demo.cpp
    rate_control.cpp)

add_executable(test_rate_control
    test_rate_control.cpp
    rate_control.cpp)

enable_testing()
# D1：模式决策校验（构造输入可手算核对：横/竖条纹选对模式、SAD/SATD 数值）。
add_test(NAME intra_mode_decision COMMAND test_intra_mode_decision)
# D2：正向变换 + 量化校验（往返一致性 / DC-only / QP 翻倍 / 符号保留）。
add_test(NAME forward_transform_quant COMMAND test_forward_transform_quant)
# D3：CAVLC 编码校验（编码→解码往返一致，覆盖尾部±1/大幅值/全零/DC-only）。
add_test(NAME cavlc_encode COMMAND test_cavlc_encode)
# D4：运动估计校验（全搜索找回已知 MV、菱形搜索点数远小于全搜索）。
add_test(NAME motion_search COMMAND test_motion_search)
# D5：码率控制校验（固定 QP 恒定、CBR 反向调 QP、长期码率收敛）。
add_test(NAME rate_control COMMAND test_rate_control)

# ── D6：NAL 封装 + 编码器整合（收官）─────────────────────────────────────
# nal_packetizer：SPS/PPS 生成 + Annex B 起始码 + emulation prevention。
# encoder_main：整合 D1(帧内模式)/D2(正变换量化)/D3(CAVLC) + decoder 的帧内
# 预测与反变换（编码端解码重建），把一帧图像编成 .h264（仅 I_4x4 帧内，诚实
# 收窄：色度不编码残差 cbp_chroma=0，无 P 帧，见 encoder_main.cpp 说明）。

# 编码器可执行文件。
add_executable(h264_encoder
    encoder_main.cpp
    nal_packetizer.cpp
    intra_mode_decision.cpp
    forward_transform_quant.cpp
    cavlc_encode.cpp
    rate_control.cpp
    ../decoder/intra_predict.cpp
    ../decoder/transform_quant.cpp
    ../decoder/cavlc_decode.cpp
    ../decoder/rbsp_bit_reader.cpp
    ../../common/bitwriter.cpp
    ../../common/image_io.cpp)

# D6 封装层测试：SPS/PPS 往返（用 decoder 的 sps_pps_parser）+ Annex B 起始码
# 与 emulation prevention 往返（用 decoder 的 nal_splitter）。
add_executable(test_nal_packetizer
    test_nal_packetizer.cpp
    nal_packetizer.cpp
    ../decoder/sps_pps_parser.cpp
    ../decoder/nal_splitter.cpp
    ../decoder/rbsp_bit_reader.cpp)

# D6：封装层校验（SPS/PPS 字段往返一致、Annex B 转义还原）。
add_test(NAME nal_packetizer COMMAND test_nal_packetizer)
