#
# Copyright (C) 2022 Intel Corporation
# SPDX-License-Identifier: Apache-2.0
#

file(GLOB SRC *.cpp)
file(GLOB HDR *.hpp)

add_library(gapi_test_computations SHARED ${SRC} ${HDR})

target_include_directories(gapi_test_computations
    SYSTEM PRIVATE
        ${GAPI_SIPP_INCLUDES})

target_include_directories(gapi_test_computations
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(gapi_test_computations
    PUBLIC
        openvino_intel_vpux_plugin_static
    PRIVATE
        ${GAPI_SIPP_LIBRARY}
        ${GAPI_M2I_LIBRARY}
)

target_compile_definitions(gapi_test_computations PRIVATE IMPLEMENT_GAPI_COMPUTATION_API)

if(ENABLE_CLANG_FORMAT)
    add_clang_format_target(clang_format_gapi_test_computations FOR_TARGETS gapi_test_computations ALL)
endif()
