#
# Copyright (C) 2022 Intel Corporation.
# SPDX-License-Identifier: Apache 2.0
#

#

if(ENABLE_LTO)
    ie_enable_lto()
endif()

set(HDDL2_BACKEND_TARGET "vpux_hddl2_backend" CACHE INTERNAL "")
set(HDDL2_BACKEND_MODULE_TARGET "${HDDL2_BACKEND_TARGET}" CACHE INTERNAL "")
set(HDDL2_BACKEND_STATIC_TARGET "${HDDL2_BACKEND_TARGET}_static" CACHE INTERNAL "")

file(GLOB_RECURSE SOURCES *.cpp *.hpp *.h)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

#
# Module library
#

add_library(${HDDL2_BACKEND_MODULE_TARGET} MODULE ${SOURCES})

enable_warnings_as_errors(${HDDL2_BACKEND_MODULE_TARGET})

target_compile_definitions(${HDDL2_BACKEND_MODULE_TARGET}
    PRIVATE
        IMPLEMENT_INFERENCE_ENGINE_PLUGIN)

target_include_directories(${HDDL2_BACKEND_MODULE_TARGET}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/hddl_unite"
        "${IE_MAIN_VPUX_PLUGIN_SOURCE_DIR}/include"
        # TODO Should be not included
        "${IE_MAIN_VPUX_PLUGIN_SOURCE_DIR}/src/vpux_plugin/include"
        )

link_system_libraries(${HDDL2_BACKEND_MODULE_TARGET}
    PRIVATE
        IE::inference_engine
        IE::inference_engine_plugin_api
        )

target_link_libraries(${HDDL2_BACKEND_MODULE_TARGET}
    PRIVATE
        HddlUnite
        vpux_al
        kmb_utils
        vpux_utils)

# FIXME: this is a WA for E#13996
# for some reason, compile_tool hangs after hddlUnite.so fini
# if the openvino built with ENABLE_LTO=ON
if(LINUX AND LINUX_OS_NAME MATCHES "Ubuntu")
    set_target_properties(${HDDL2_BACKEND_MODULE_TARGET} PROPERTIES LINK_OPTIONS "-Wl,-z,nodelete")
endif()

if(ENABLE_CLANG_FORMAT)
    add_clang_format_target(clang_format_${HDDL2_BACKEND_MODULE_TARGET} FOR_TARGETS ${HDDL2_BACKEND_MODULE_TARGET} ALL)
    add_dependencies(${HDDL2_BACKEND_MODULE_TARGET} clang_format_${HDDL2_BACKEND_MODULE_TARGET})
endif()

#
# Static library
#

if(ENABLE_TESTS)
    add_library(${HDDL2_BACKEND_STATIC_TARGET} STATIC
            ${SOURCES})

    target_include_directories(${HDDL2_BACKEND_STATIC_TARGET}
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/hddl_unite"
            "${IE_MAIN_VPUX_PLUGIN_SOURCE_DIR}/include/hddl2"
            "${IE_MAIN_VPUX_PLUGIN_SOURCE_DIR}/include/vpu"
            # TODO Should be not included
            "${IE_MAIN_VPUX_PLUGIN_SOURCE_DIR}/src/vpux_plugin/include"
            )

    link_system_libraries(${HDDL2_BACKEND_STATIC_TARGET}
        PRIVATE
            IE::inference_engine
            IE::inference_engine_plugin_api
            )

    target_link_libraries(${HDDL2_BACKEND_STATIC_TARGET}
        PUBLIC
            #   TODO FIX PUBLIC "fatal error: vpu/parsed_config_base.hpp: No such file or directory" otherwise
            vpux_utils
        PRIVATE
            HddlUnite
            vpux_al
            kmb_utils
            )
endif()

#
# targets install
#

install(TARGETS ${HDDL2_BACKEND_MODULE_TARGET}
        LIBRARY DESTINATION ${IE_CPACK_RUNTIME_PATH} COMPONENT ${VPUX_PLUGIN_COMPONENT})
