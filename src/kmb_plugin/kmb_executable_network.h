//
// Copyright 2019 Intel Corporation.
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

#include <ie_common.h>

#include <cpp_interfaces/impl/ie_executable_network_thread_safe_default.hpp>
#include <fstream>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <threading/ie_executor_manager.hpp>
#include <vector>
#include <vpux.hpp>

#include "ie_remote_context.hpp"
#include "kmb_async_infer_request.h"
#include "kmb_infer_request.h"
#include "kmb_remote_context.h"

namespace vpu {
namespace KmbPlugin {
namespace ie = InferenceEngine;

class ExecutableNetwork final : public ie::ExecutableNetworkThreadSafeDefault {
public:
    using Ptr = std::shared_ptr<ExecutableNetwork>;

    explicit ExecutableNetwork(
        ie::ICNNNetwork& network, const vpux::VPUXConfig& config, const std::shared_ptr<vpux::Device>& device);
    explicit ExecutableNetwork(
        std::istream& strm, const vpux::VPUXConfig& config, const std::shared_ptr<vpux::Device>& device);

    virtual ~ExecutableNetwork() = default;

    void SetConfig(const std::map<std::string, ie::Parameter>& config) override;
    ie::Parameter GetMetric(const std::string& name) const override;
    ie::Parameter GetConfig(const std::string& name) const override;

    ie::InferRequestInternal::Ptr CreateInferRequestImpl(
        ie::InputsDataMap networkInputs, ie::OutputsDataMap networkOutputs) override {
        if (_device == nullptr) {
            THROW_IE_EXCEPTION << "Can not create an infer request because there are no devices";
        }

        return std::make_shared<KmbInferRequest>(
            networkInputs, networkOutputs, _stagesMetaData, _config, _executor, _device->getAllocator(), _netName);
    }

    ie::IInferRequest::Ptr CreateInferRequest() override {
        if (_device == nullptr) {
            THROW_IE_EXCEPTION << "Can not create an infer request because there are no devices";
        }

        auto syncRequestImpl = std::make_shared<KmbInferRequest>(
            _networkInputs, _networkOutputs, _stagesMetaData, _config, _executor, _device->getAllocator(), _netName);

        syncRequestImpl->setPointerToExecutableNetworkInternal(shared_from_this());
        auto taskExecutorGetResult = getNextTaskExecutor();
        auto asyncThreadSafeImpl = std::make_shared<KmbAsyncInferRequest>(
            syncRequestImpl, _taskExecutor, taskExecutorGetResult, _callbackExecutor, _logger);
        ie::IInferRequest::Ptr asyncRequest;
        asyncRequest.reset(new ie::InferRequestBase<ie::AsyncInferRequestThreadSafeDefault>(asyncThreadSafeImpl),
            [](ie::IInferRequest* p) {
                p->Release();
            });
        asyncThreadSafeImpl->SetPointerToPublicInterface(asyncRequest);
        return asyncRequest;
    }

    void ExportImpl(std::ostream& model) override {
        const auto& blob = _networkDescription->getCompiledNetwork();
        model.write(blob.data(), blob.size());
    }

    void Export(const std::string& modelFileName) override {
        const auto& blob = _networkDescription->getCompiledNetwork();
        if (!blob.empty()) {
            std::ofstream modelFile(modelFileName, std::ios::out | std::ios::binary);

            if (modelFile.is_open()) {
                ExportImpl(modelFile);
            } else {
                THROW_IE_EXCEPTION << "The " << modelFileName << " file can not be opened for export";
            }
        }
    }

    void Export(std::ostream& networkModel) override { ExportImpl(networkModel); }

private:
    explicit ExecutableNetwork(const vpux::VPUXConfig& config, const std::shared_ptr<vpux::Device>& device);

    void ConfigureExecutor(const std::string& networkName);
    void LoadBlob();

    ie::ITaskExecutor::Ptr getNextTaskExecutor();

    Logger::Ptr _logger;

    std::vector<StageMetaInfo> _stagesMetaData;
    vpux::VPUXConfig _config;
    std::map<std::string, ie::Parameter> _parsedConfig;
    std::vector<std::string> _supportedMetrics;

    std::queue<std::string> _taskExecutorGetResultIds;

    std::string _netName;

    vpux::Compiler::Ptr _compiler = nullptr;
    vpux::NetworkDescription::Ptr _networkDescription = nullptr;

    std::shared_ptr<vpux::Device> _device = nullptr;
    // FIXME: executor contains a pointer for allocator which comes from dll.
    // executor should be destroyed before _device. TBD: ticket
    std::shared_ptr<vpux::Executor> _executor = nullptr;
    static std::atomic<int> loadBlobCounter;
};

}  // namespace KmbPlugin
}  // namespace vpu
