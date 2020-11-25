#
# Copyright 2020 Intel Corporation.
#
# This software and the related documents are Intel copyrighted materials,
# and your use of them is governed by the express license under which they
# were provided to you (End User License Agreement for the Intel(R) Software
# Development Products (Version May 2017)). Unless the License provides
# otherwise, you may not use, modify, copy, publish, distribute, disclose or
# transmit this software or the related documents without Intel's prior
# written permission.
#
# This software and the related documents are provided as is, with no
# express or implied warranties, other than those that are expressly
# stated in the License.
#

include(TableGen)

get_directory_property(MLIR_TABLEGEN_EXE DIRECTORY ${MLIR_SOURCE_DIR} DEFINITION MLIR_TABLEGEN_EXE)

function(vpux_add_tblgen_command)
    set(options)
    set(oneValueArgs TOOL MODE SOURCE OUTPUT)
    set(multiValueArgs EXTRA_ARGS INCLUDES)
    cmake_parse_arguments(TBLGEN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT TBLGEN_TOOL)
        message(FATAL_ERROR "Missing TOOL argument in vpux_add_tblgen_command")
    endif()
    if(NOT TBLGEN_MODE)
        message(FATAL_ERROR "Missing MODE argument in vpux_add_tblgen_command")
    endif()
    if(NOT TBLGEN_SOURCE)
        message(FATAL_ERROR "Missing SOURCE argument in vpux_add_tblgen_command")
    endif()
    if(NOT TBLGEN_OUTPUT)
        message(FATAL_ERROR "Missing OUTPUT argument in vpux_add_tblgen_command")
    endif()

    if(IS_ABSOLUTE ${TBLGEN_SOURCE})
        message(FATAL_ERROR "SOURCE argument in vpux_add_tblgen_command must be a relative path")
    endif()
    if(IS_ABSOLUTE ${TBLGEN_OUTPUT})
        message(FATAL_ERROR "OUTPUT argument in vpux_add_tblgen_command must be a relative path")
    endif()

    set(src_abs_path "${CMAKE_CURRENT_SOURCE_DIR}/${TBLGEN_SOURCE}")
    set(dst_abs_path "${CMAKE_CURRENT_BINARY_DIR}/${TBLGEN_OUTPUT}")

    get_filename_component(dst_dir ${dst_abs_path} DIRECTORY)
    file(MAKE_DIRECTORY ${dst_dir})

    set(cmd_args ${TBLGEN_MODE} ${TBLGEN_EXTRA_ARGS})
    list(APPEND cmd_args -I "${CMAKE_CURRENT_SOURCE_DIR}/tblgen")
    foreach(include_dir IN LISTS LLVM_INCLUDE_DIRS MLIR_INCLUDE_DIRS TBLGEN_INCLUDES)
        list(APPEND cmd_args -I ${include_dir})
    endforeach()

    set(LLVM_TARGET_DEFINITIONS ${src_abs_path})
    set(TABLEGEN_OUTPUT)
    tablegen(${TBLGEN_TOOL} ${TBLGEN_OUTPUT} ${cmd_args})

    set(TBGGEN_OUTPUT_FILES ${TBGGEN_OUTPUT_FILES} ${TABLEGEN_OUTPUT} PARENT_SCOPE)
endfunction()

function(vpux_add_tblgen_target TARGET_NAME)
    add_custom_target(${TARGET_NAME} ALL
        DEPENDS ${TBGGEN_OUTPUT_FILES}
        SOURCES ${ARGN}
        COMMENT "[TableGen] ${TARGET_NAME}"
    )
endfunction()
