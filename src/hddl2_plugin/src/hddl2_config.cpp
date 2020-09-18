//
// Copyright 2020 Intel Corporation.
//
// This software and the related documents are Intel copyrighted materials,
// and your use of them is governed by the express license under which they
// were provided to you (End User License Agreement for the Intel(R) Software
// Development Products (Version May 2017)). Unless the License provides
// otherwise, you may not use, modify, copy, publish, distribute, disclose or
// transmit this software or the related documents without Intel's prior
// written permission.
//
// This software and the related documents are provided as is, with no
// express or implied warranties, other than those that are expressly
// stated in the License.
//

#include "hddl2_config.h"

#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vpu/kmb_plugin_config.hpp>

using namespace vpu;
namespace IE = InferenceEngine;

const std::unordered_set<std::string>& HDDL2Config::getCompileOptions() const {
    static const std::unordered_set<std::string> options = vpux::VPUXConfig::getCompileOptions();
    return options;
}

const std::unordered_set<std::string>& HDDL2Config::getRunTimeOptions() const {
    static const std::unordered_set<std::string> options =
        merge(vpux::VPUXConfig::getRunTimeOptions(), {
                                                         CONFIG_KEY(PERF_COUNT),
                                                         CONFIG_KEY(DEVICE_ID),
                                                         VPU_HDDL2_CONFIG_KEY(GRAPH_COLOR_FORMAT),
                                                         VPU_HDDL2_CONFIG_KEY(CSRAM_SIZE)
                                                     });

    return options;
}

void HDDL2Config::parse(const std::map<std::string, std::string>& config) {
    vpux::VPUXConfig::parse(config);

    static const std::unordered_map<std::string, LogLevel> logLevels = {{CONFIG_VALUE(LOG_NONE), LogLevel::None},
        {CONFIG_VALUE(LOG_ERROR), LogLevel::Error}, {CONFIG_VALUE(LOG_WARNING), LogLevel::Warning},
        {CONFIG_VALUE(LOG_INFO), LogLevel::Info}, {CONFIG_VALUE(LOG_DEBUG), LogLevel::Debug},
        {CONFIG_VALUE(LOG_TRACE), LogLevel::Trace}};

    static const std::unordered_map<std::string, IE::ColorFormat> colorFormat = {
        {VPU_HDDL2_CONFIG_VALUE(BGR), IE::ColorFormat::BGR}, {VPU_HDDL2_CONFIG_VALUE(RGB), IE::ColorFormat::RGB}};

    setOption(_device_id, config, CONFIG_KEY(DEVICE_ID));
    setOption(_performance_counting, switches, config, CONFIG_KEY(PERF_COUNT));
    setOption(_graph_color_format, colorFormat, config, VPU_HDDL2_CONFIG_KEY(GRAPH_COLOR_FORMAT));
    setOption(_csram_size, config, VPU_HDDL2_CONFIG_KEY(CSRAM_SIZE));
}
