#
# Copyright (C) 2022 Intel Corporation
# SPDX-License-Identifier: Apache 2.0
#
cmake_minimum_required(VERSION 3.5.0)
project(validateTensor VERSION 0.0.1)

find_package(gflags QUIET)

include_directories(${IE_MAIN_VPUX_PLUGIN_SOURCE_DIR}/thirdparty/flatbuffers/include)
add_executable(validate 
    "validate.cpp"
)

target_link_libraries(validate gflags vpux_mcm_compiler)
add_dependencies(validate vpux_mcm_compiler)
