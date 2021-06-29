//
// Copyright 2019-2020 Intel Corporation.
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

#pragma once

#include <iomanip>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(__arm__) || defined(__aarch64__)
#include <NnCorePlg.h>
#include <NnXlinkPlg.h>
#include <mvMacros.h>
#include <xlink_uapi.h>
#include "vpual_core_nn_synchronizer.hpp"
#endif

// FIXME: get back config
// #include <kmb_config.h>
#include <vpu/utils/logger.hpp>
#include <vpux.hpp>
#include <vpux_config.hpp>

#include "vpual_config.hpp"
#include "vpual_core_nn_watchdog.hpp"
#include "vpusmm_allocator.hpp"

namespace ie = InferenceEngine;

namespace vpux {

class VpualCoreNNExecutor final : public vpux::Executor {
public:
    using Ptr = std::shared_ptr<VpualCoreNNExecutor>;

#if (defined(__arm__) || defined(__aarch64__)) && defined(VPUX_DEVELOPER_BUILD)
    class PipePrintHandler;
#endif

    virtual ~VpualCoreNNExecutor();
    VpualCoreNNExecutor(const vpux::NetworkDescription::Ptr& networkDescription,
        const VpusmmAllocator::Ptr& allocator,
        const uint32_t deviceId,
        const InferenceEngine::VPUXConfigParams::VPUXPlatform& platform,
        const VpualConfig& config);

#if defined(__arm__) || defined(__aarch64__)
    VpualCoreNNExecutor(const vpux::NetworkDescription::Ptr& networkDescription,
        const VpusmmAllocator::Ptr& allocator,
        const std::shared_ptr<NnXlinkPlg>& other_nnXlinkPlg,
        const std::shared_ptr<NnCorePlg>& other_nnCorePlg,
        const std::shared_ptr<VpualCoreNNSynchronizer<VpualSyncXLinkImpl>>& other_nnSync,
        const std::shared_ptr<Pipeline>& other_pipe,
        const std::shared_ptr<WatchDog>& watchDog,
        const VpualConfig& config);
#endif

    void push(const InferenceEngine::BlobMap& inputs) override;
    void push(const InferenceEngine::BlobMap& inputs, const PreprocMap& preProcMap) override;
    void pull(InferenceEngine::BlobMap& outputs) override;
    // TODO: not implemented
    void setup(const InferenceEngine::ParamMap& params) override;
    bool isPreProcessingSupported(const PreprocMap& preProcMap) const override;
    std::map<std::string, InferenceEngine::InferenceEngineProfileInfo> getLayerStatistics() override;
    InferenceEngine::Parameter getParameter(const std::string& paramName) const override;
    vpux::Executor::Ptr clone() const override;

private:
    vpux::NetworkDescription::Ptr _networkDescription;
    VpusmmAllocator::Ptr _allocator;
    VpusmmAllocator::Ptr _csramAllocator;
    const VpualConfig& _config;
    vpu::Logger::Ptr _logger;

#if defined(__arm__) || defined(__aarch64__)
    std::shared_ptr<WatchDog> _wd;
    std::shared_ptr<NnXlinkPlg> _nnXlinkPlg = nullptr;
    std::shared_ptr<NnCorePlg> _nnCorePlg = nullptr;
    VpualSyncXLinkImpl _vpualSyncImpl;
    std::shared_ptr<VpualCoreNNSynchronizer<VpualSyncXLinkImpl>> _nnSync = nullptr;
    // pipeline has to be deleted before NNCore plug-in
    // otherwise it leads to 'Bus error'
    std::shared_ptr<Pipeline> _pipe = nullptr;
    std::unique_ptr<void, std::function<void(void*)>> blob_file = nullptr;
    std::unique_ptr<BlobHandle_t> _blobHandle = nullptr;
    unsigned int _execInferId = 0;

    void initWatchDog();
#endif

    VpualCoreNNExecutor();
    void allocateGraph(const std::vector<char>& compiledNetwork);

    ie::Blob::Ptr prepareInputForInference(
        const ie::Blob::Ptr& actualInput, const InferenceEngine::TensorDesc& deviceDesc);
    uint32_t extractPhysAddrForInference(const ie::BlobMap& inputs);

    ie::BlobMap extractOutputsFromPhysAddr(uint32_t physAddr);
    void repackDeviceOutputsToNetworkOutputs(
        const InferenceEngine ::BlobMap& deviceOutputs, InferenceEngine::BlobMap& networkOutputs);

    std::vector<void*> _scratchBuffers;
    std::unique_ptr<uint8_t, std::function<void(uint8_t*)>> _preFetchBuffer;
    std::unique_ptr<uint8_t, std::function<void(uint8_t*)>> _inputBuffer;
    std::unique_ptr<uint8_t, std::function<void(uint8_t*)>> _outputBuffer;
    InferenceEngine::VPUXConfigParams::VPUXPlatform _platform;

    std::vector<uint32_t> _outputPhysAddrs;

#if (defined(__arm__) || defined(__aarch64__)) && defined(VPUX_DEVELOPER_BUILD)
    std::shared_ptr<PipePrintHandler> _pipePrint;
#endif
};

}  // namespace vpux
