//
// Copyright 2021 Intel Corporation.
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

#include "emulator_network_description.hpp"

#include "vpux/al/config/common.hpp"

#include <utility>

using namespace vpux;

namespace vpu {
namespace MCMAdapter {

EmulatorNetworkDescription::EmulatorNetworkDescription(const std::vector<char>& compiledNetwork, const Config& config,
                                                       const std::string& name)
        : _name{name},
          _logger{std::unique_ptr<vpu::Logger>(new vpu::Logger(
                  "EmulatorNetworkDescription", toOldLogLevel(config.get<LOG_LEVEL>()), consoleOutput()))},
          _dataMapPlaceholder{},
          _compiledNetwork{compiledNetwork},
          _quantParams{} {
    IE_ASSERT(!_compiledNetwork.empty());
}

const DataMap& EmulatorNetworkDescription::getInputsInfo() const {
    _logger->info("EmulatorNetworkDescription::getInputsInfo()\n");
    return _dataMapPlaceholder;
}

const DataMap& EmulatorNetworkDescription::getOutputsInfo() const {
    _logger->info("EmulatorNetworkDescription::getOutputsInfo()\n");
    return _dataMapPlaceholder;
}

const DataMap& EmulatorNetworkDescription::getDeviceInputsInfo() const {
    _logger->info("EmulatorNetworkDescription::getDeviceInputsInfo()\n");
    return _dataMapPlaceholder;
}

const DataMap& EmulatorNetworkDescription::getDeviceOutputsInfo() const {
    _logger->info("EmulatorNetworkDescription::getDeviceOutputsInfo()\n");
    return _dataMapPlaceholder;
}

const DataMap& EmulatorNetworkDescription::getDeviceProfilingOutputsInfo() const {
    _logger->info("EmulatorNetworkDescription::getDeviceProfilingsInfo()\n");
    return _dataMapPlaceholder;
}

const QuantizationParamMap& EmulatorNetworkDescription::getQuantParamsInfo() const {
    _logger->info("EmulatorNetworkDescription::getQuantParamsInfo()\n");
    return _quantParams;
}

const std::vector<char>& EmulatorNetworkDescription::getCompiledNetwork() const {
    return _compiledNetwork;
}

const void* EmulatorNetworkDescription::getNetworkModel() const {
    return _compiledNetwork.data();
}

std::size_t EmulatorNetworkDescription::getNetworkModelSize() const {
    return _compiledNetwork.size();
}

const std::string& EmulatorNetworkDescription::getName() const {
    return _name;
}

}  // namespace MCMAdapter
}  // namespace vpu
