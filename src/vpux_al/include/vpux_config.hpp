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

#pragma once

#include <vpux/vpux_plugin_config.hpp>

#include "vpux_config_base.hpp"
#include "vpux_private_config.hpp"

namespace vpux {

class VPUXConfig : public VPUXConfigBase {
public:
    VPUXConfig();

    // Public options
    bool performanceCounting() const {
        return _performanceCounting;
    }
    std::string deviceId() const {
        return _deviceId;
    }
    int throughputStreams() const {
        return _throughputStreams;
    }
    int numberOfNnCoreShaves() const {
        return _numberOfNnCoreShaves;
    }
    InferenceEngine::VPUXConfigParams::VPUXPlatform platform() const {
        return _platform;
    }

    // Private options
    InferenceEngine::ColorFormat graphColorFormat() const {
        return _graphColorFormat;
    }
    int32_t CSRAMSize() const {
        return _csramSize;
    }
    bool useM2I() const {
        return _useM2I;
    }
    bool useSHAVE_only_M2I() const {
        return _useSHAVE_only_M2I;
    }
    bool useSIPP() const {
        return _useSIPP;
    }
    int numberOfSIPPShaves() const {
        return _numberOfSIPPShaves;
    }
    int SIPPLpi() const {
        return _SIPPLpi;
    }
    int numberOfPPPipes() const {
        return _numberOfPPPipes;
    }
    int executorStreams() const {
        return _executorStreams;
    }
    uint32_t inferenceTimeoutMs() const noexcept {
        return _inferenceTimeoutMs;
    }
    InferenceEngine::VPUXConfigParams::CompilerType compilerType() const noexcept {
        return _compilerType;
    }
    const std::string& compilationMode() const {
        return _compilationMode;
    }

    void parseFrom(const VPUXConfig& other);

protected:
    void parse(const std::map<std::string, std::string>& config) override;

    // Public options
    bool _performanceCounting = false;
    std::string _deviceId = "VPU-0";
    int _throughputStreams = 2;
    int _numberOfNnCoreShaves = 0;
    InferenceEngine::VPUXConfigParams::VPUXPlatform _platform = InferenceEngine::VPUXConfigParams::VPUXPlatform::AUTO;
    int32_t _csramSize = -1;

    // Private options
    InferenceEngine::ColorFormat _graphColorFormat = InferenceEngine::ColorFormat::BGR;
    // FIXME: Likely has to be true by default as well.
    // NB.: Currently applies to the detection use-case only
    bool _useM2I = false;
    bool _useSHAVE_only_M2I = false;
    bool _useSIPP = true;
    int _numberOfSIPPShaves = 4;
    int _SIPPLpi = 8;
    int _numberOfPPPipes = 1;
    int _executorStreams = 1;
    // backend pull timeout - 5 seconds by default
    uint32_t _inferenceTimeoutMs = 5 * 1000;
    InferenceEngine::VPUXConfigParams::CompilerType _compilerType =
            InferenceEngine::VPUXConfigParams::CompilerType::MCM;

    std::string _compilationMode = "ReferenceSW";

private:
    void parseEnvironment();
};

std::string getLibFilePath(const std::string& baseName);

}  // namespace vpux
