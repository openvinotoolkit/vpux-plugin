//
// Copyright 2020 Intel Corporation.
//
// LEGAL NOTICE: Your use of this software and any required dependent software
// (the "Software Package") is subject to the terms and conditions of
// the Intel(R) OpenVINO(TM) Distribution License for the Software Package,
// which may also include notices, disclaimers, or license terms for
// third party or open source software included in or with the Software Package,
// and your use indicates your acceptance of all such terms. Please refer
// to the "third-party-programs.txt" or other similarly-named text file
// included with the Software Package for additional details.
//


/**
 * @brief A header that defines advanced related properties for VPU plugins.
 * These properties should be used in SetConfig() and LoadNetwork() methods of plugins
 *
 * @file vpu_plugin_config.hpp
 */

#pragma once

#include <vpux/vpux_compiler_config.hpp>

//
// VPUX plugin options
//

#define VPUX_CONFIG_KEY(name) InferenceEngine::VPUXConfigParams::_CONFIG_KEY(VPUX_##name)
#define VPUX_CONFIG_VALUE(name) InferenceEngine::VPUXConfigParams::name

#define DECLARE_VPUX_CONFIG_KEY(name) DECLARE_CONFIG_KEY(VPUX_##name)
#define DECLARE_VPUX_CONFIG_VALUE(name) DECLARE_CONFIG_VALUE(name)

// TODO Only to support old config options. Must be removed in future

#define VPU_KMB_CONFIG_KEY(name) InferenceEngine::VPUXConfigParams::_CONFIG_KEY(VPU_KMB_##name)
#define VPU_KMB_CONFIG_VALUE(name) InferenceEngine::VPUXConfigParams::VPU_KMB_##name

#define KMB_CONFIG_KEY(name) InferenceEngine::VPUXConfigParams::_CONFIG_KEY(KMB_##name)
#define KMB_CONFIG_VALUE(name) InferenceEngine::VPUXConfigParams::KMB_##name

#define DECLARE_VPU_KMB_CONFIG_KEY(name) DECLARE_CONFIG_KEY(VPU_KMB_##name)
#define DECLARE_VPU_KMB_CONFIG_VALUE(name) DECLARE_CONFIG_VALUE(VPU_KMB_##name)

#define DECLARE_KMB_CONFIG_KEY(name) DECLARE_CONFIG_KEY(KMB_##name)
#define DECLARE_KMB_CONFIG_VALUE(name) DECLARE_CONFIG_VALUE(KMB_##name)

namespace InferenceEngine {
namespace VPUXConfigParams {

//
// VPUX plugin options
//

/**
 * @brief [Only for VPUX Plugin]
 * Type: integer, default is 2.
 */
DECLARE_VPUX_CONFIG_KEY(THROUGHPUT_STREAMS);

/**
 * @deprecated Use VPUX_THROUGHPUT_STREAMS instead
 * @brief [Only for VPUX Plugin]
 * Type: integer, default is 2.
 */
DECLARE_KMB_CONFIG_KEY(THROUGHPUT_STREAMS);

/**
 * @brief [Only for VPUX Plugin]
 * Type: integer, default is 0. SetNumUpaShaves is not called in that case.
 * Number of shaves to be used by NNCore plug-in during inference
 */
DECLARE_VPUX_CONFIG_KEY(INFERENCE_SHAVES);

/**
 * Type: Arbitrary string. Default is "-1".
 * This option allows to specify CSRAM size in bytes
 * When the size is -1, low-level SW is responsible for determining the required amount of CSRAM
 * When the size is 0, CSRAM isn't used
 */
DECLARE_VPUX_CONFIG_KEY(CSRAM_SIZE);

/**
 * @brief [Only for VPUX Plugin]
 * Type: bool, default is true.
 * When true, Executor is created after ExecutableNetwork compilation
 * When false, ExecutableNetwork is compiled without creating an Executor and loading to the device
 */
DECLARE_VPUX_CONFIG_KEY(CREATE_EXECUTOR);

}  // namespace VPUXConfigParams
}  // namespace InferenceEngine
