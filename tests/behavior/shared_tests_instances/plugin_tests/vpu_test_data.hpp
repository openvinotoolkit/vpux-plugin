// Copyright (C) 2018-2019 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include <vpu/kmb_plugin_config.hpp>
#include <vpu/vpu_compiler_config.hpp>

#include "behavior_test_plugin.h"

// correct params
#define BEH_KMB    \
    BehTestParams( \
        "KMB", convReluNormPoolFcModelFP16.model_xml_str, convReluNormPoolFcModelFP16.weights_blob, Precision::FP32)
#define BEH_HETERO                                                                                               \
    BehTestParams("HETERO", convReluNormPoolFcModelFP32.model_xml_str, convReluNormPoolFcModelFP32.weights_blob, \
        Precision::FP32)

// all parameters are unsupported - reversed
#define BEH_US_ALL_KMB \
    BehTestParams(     \
        "KMB", convReluNormPoolFcModelQ78.model_xml_str, convReluNormPoolFcModelQ78.weights_blob, Precision::Q78)

const BehTestParams supportedValues[] = {
    BEH_KMB,
};

const BehTestParams requestsSupportedValues[] = {
    BEH_KMB,
};

const BehTestParams allInputSupportedValues[] = {
    BEH_KMB,
    BEH_KMB.withIn(Precision::U8),
    BEH_KMB.withIn(Precision::FP16),
};

const BehTestParams allOutputSupportedValues[] = {
    BEH_KMB,
    BEH_KMB.withOut(Precision::FP16),
};

const BehTestParams typeUnSupportedValues[] = {
    BEH_KMB.withIn(Precision::Q78),
    BEH_KMB.withIn(Precision::U16),
    BEH_KMB.withIn(Precision::I8),
    BEH_KMB.withIn(Precision::I16),
    BEH_KMB.withIn(Precision::I32),
};

const BehTestParams allUnSupportedValues[] = {
    BEH_US_ALL_KMB,
};

const std::vector<BehTestParams> withCorrectConfValues = {
    BEH_KMB.withConfig({{KEY_VPU_COPY_OPTIMIZATION, NO}}),
    BEH_KMB.withConfig({{KEY_LOG_LEVEL, LOG_DEBUG}}),
    BEH_KMB.withConfig({{KEY_VPU_IGNORE_UNKNOWN_LAYERS, YES}}),
    BEH_KMB.withConfig({{KEY_VPU_HW_STAGES_OPTIMIZATION, YES}}),
    BEH_KMB.withConfig({{KEY_VPU_NONE_LAYERS, "Tile"}}),
    BEH_KMB.withConfig({{KEY_VPU_NUMBER_OF_SHAVES, "5"}, {KEY_VPU_NUMBER_OF_CMX_SLICES, "5"}}),
    BEH_KMB.withConfig({{KEY_VPU_HW_INJECT_STAGES, "YES"}}),
    BEH_KMB.withConfig({{KEY_VPU_HW_POOL_CONV_MERGE, "YES"}}),
    BEH_KMB.withConfig({{VPU_KMB_CONFIG_KEY(PREPROCESSING_SHAVES), "6"}}),
    BEH_KMB.withConfig({{VPU_KMB_CONFIG_KEY(PREPROCESSING_LPI), "8"}}),
    BEH_KMB.withConfig({{VPU_COMPILER_CONFIG_KEY(LOG_LEVEL), LOG_DEBUG}}),
    BEH_KMB.withConfig({{VPU_COMPILER_CONFIG_KEY(INPUTS_SCALES_ALIGNMENT), NO}}),
    BEH_KMB.withConfig({{VPU_COMPILER_CONFIG_KEY(INPUT_SCALE_SHIFT_REMOVING), YES}}),
    BEH_KMB.withConfig({{"VPU_KMB_SIPP_OUT_COLOR_FORMAT", "RGB"}}),
    BEH_KMB.withConfig({{"VPU_KMB_FORCE_NCHW_TO_NHWC", YES}}),
};

const std::vector<BehTestParams> withCorrectConfValuesPluginOnly = {};

const std::vector<BehTestParams> withCorrectConfValuesNetworkOnly = {};

const BehTestParams withIncorrectConfValues[] = {
    BEH_KMB.withConfig({{KEY_VPU_COPY_OPTIMIZATION, "ON"}}),
    BEH_KMB.withConfig({{KEY_LOG_LEVEL, "VERBOSE"}}),
    BEH_KMB.withConfig({{KEY_VPU_IGNORE_UNKNOWN_LAYERS, "ON"}}),
    BEH_KMB.withConfig({{VPU_KMB_CONFIG_KEY(PREPROCESSING_SHAVES), "SIX"}}),
    BEH_KMB.withConfig({{VPU_KMB_CONFIG_KEY(PREPROCESSING_LPI), "EIGHT"}}),
    BEH_KMB.withConfig({{VPU_COMPILER_CONFIG_KEY(LOG_LEVEL), "debug"}}),
    BEH_KMB.withConfig({{VPU_COMPILER_CONFIG_KEY(INPUTS_SCALES_ALIGNMENT), "NOP"}}),
    BEH_KMB.withConfig({{VPU_COMPILER_CONFIG_KEY(INPUT_SCALE_SHIFT_REMOVING), "YEP"}}),
    BEH_KMB.withConfig({{"VPU_KMB_SIPP_OUT_COLOR_FORMAT", "NV12"}}),
    BEH_KMB.withConfig({{"VPU_KMB_FORCE_NCHW_TO_NHWC", "YEP"}}),
};
