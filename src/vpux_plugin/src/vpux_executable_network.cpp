//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//

// System
#include <fstream>

#include <ie_icore.hpp>
#include <ie_metric_helpers.hpp>
#include <ie_plugin_config.hpp>
#include <legacy/convert_function_to_cnn_network.hpp>
#include <legacy/transformations/convert_opset1_to_legacy/convert_opset1_to_legacy.hpp>
#include <ngraph/pass/constant_folding.hpp>
#include <ngraph/pass/manager.hpp>
#include <openvino/runtime/properties.hpp>
#include <threading/ie_executor_manager.hpp>

// Plugin
#include "vpux/utils/IE/config.hpp"
#include "vpux/utils/IE/itt.hpp"
#include "vpux_async_infer_request.h"
#include "vpux_exceptions.h"
#include "vpux_executable_network.h"

// Abstraction layer
#include "vpux.hpp"
#include "vpux/al/config/common.hpp"
#include "vpux/al/config/runtime.hpp"
#include "vpux/utils/core/checked_cast.hpp"
#include "vpux_compiler.hpp"

namespace vpux {
namespace IE = InferenceEngine;

//------------------------------------------------------------------------------
//      Helpers
//------------------------------------------------------------------------------
static Executor::Ptr getExecutorForInference(const Executor::Ptr& executor, Logger logger) {
    if (executor == nullptr) {
        IE_THROW() << NO_EXECUTOR_FOR_INFERENCE;
    }

    try {
        return executor->clone();
    } catch (const std::exception& exc) {
        logger.warning("getExecutorForInference: executor threw an exception: {0}", exc.what());
        return executor;
    } catch (...) {
        logger.warning("getExecutorForInference: executor threw an unknown exception");
        return executor;
    }
}

//------------------------------------------------------------------------------
//      Shared init ctor
//------------------------------------------------------------------------------
ExecutableNetwork::ExecutableNetwork(const Config& config, const Device::Ptr& device)
        : _config(config),
          _logger("ExecutableNetwork", config.get<LOG_LEVEL>()),
          _device(device),
          _compiler(Compiler::create(config)),
          _supportedMetrics({METRIC_KEY(OPTIMAL_NUMBER_OF_INFER_REQUESTS)}) {
}

//------------------------------------------------------------------------------
//      Load network
//------------------------------------------------------------------------------
ExecutableNetwork::ExecutableNetwork(const IE::CNNNetwork& orignet, const Device::Ptr& device, const Config& config)
        : ExecutableNetwork(config, device) {
    OV_ITT_SCOPED_TASK(itt::domains::VPUXPlugin, "ExecutableNetwork::ExecutableNetwork[Load]");
    // FIXME: This is a copy-paste from kmb_executable_network.cpp
    // should be fixed after switching to VPUX completely
    OV_ITT_TASK_CHAIN(EXECUTABLE_NETWORK_LOAD, itt::domains::VPUXPlugin, "ExecutableNetwork::ExecutableNetwork[Load]",
                      "CloneNetwork");
    IE::CNNNetwork network = IE::details::cloneNetwork(orignet);
    OV_ITT_TASK_NEXT(EXECUTABLE_NETWORK_LOAD, "Compile");
    if (const auto func = network.getFunction()) {
        IE::InputsDataMap inputsInfo = network.getInputsInfo();
        IE::OutputsDataMap outputsInfo = network.getOutputsInfo();
        try {
            _networkPtr = _compiler->compile(func, network.getName(), inputsInfo, outputsInfo, _config);
        } catch (const std::exception& ex) {
            IE_THROW() << ex.what();
        } catch (...) {
            _logger.error("Unexpected exception");
            IE_THROW() << "VPUX ExecutableNetwork got unexpected exception from compiler";
        }
    } else {
        _logger.warning("Failed to read NGraph network");
        IE_THROW() << "Failed to read NGraph network";
    }
    OV_ITT_TASK_NEXT(EXECUTABLE_NETWORK_LOAD, "createExecutor");
    // TODO: Fix this WA for E#22783, E#25449
    bool IE_VPUX_CREATE_EXECUTOR = true;
    if (const auto var = std::getenv("IE_VPUX_CREATE_EXECUTOR")) {
        IE_VPUX_CREATE_EXECUTOR = vpux::envVarStrToBool("IE_VPUX_CREATE_EXECUTOR", var);
    }

    if (IE_VPUX_CREATE_EXECUTOR) {
        _executorPtr = createExecutor(_networkPtr, _config, device);
        ConfigureStreamsExecutor(network.getName());
    }
    OV_ITT_TASK_SKIP(EXECUTABLE_NETWORK_LOAD);
}

//------------------------------------------------------------------------------
//      Import network
//------------------------------------------------------------------------------
ExecutableNetwork::ExecutableNetwork(std::istream& networkModel, const Device::Ptr& device, const Config& config)
        : ExecutableNetwork(config, device) {
    OV_ITT_SCOPED_TASK(itt::domains::VPUXPlugin, "ExecutableNetwork::ExecutableNetwork[Import]");
    try {
        OV_ITT_TASK_CHAIN(EXECUTABLE_NETWORK_IMPORT, itt::domains::VPUXPlugin,
                          "ExecutableNetwork::ExecutableNetwork[Import]", "Parse");
        const std::string networkName = "net" + std::to_string(loadBlobCounter);
        _networkPtr = _compiler->parse(networkModel, _config, networkName);
        OV_ITT_TASK_NEXT(EXECUTABLE_NETWORK_IMPORT, "createExecutor");
        _executorPtr = createExecutor(_networkPtr, _config, device);
        OV_ITT_TASK_NEXT(EXECUTABLE_NETWORK_IMPORT, "Init");
        _networkInputs = helpers::dataMapIntoInputsDataMap(_networkPtr->getInputsInfo());
        _networkOutputs = helpers::dataMapIntoOutputsDataMap(_networkPtr->getOutputsInfo());
        setInputs(helpers::ovRawNodesIntoOVNodes(_networkPtr->getOVParameters(), false));
        setOutputs(helpers::ovRawNodesIntoOVNodes(_networkPtr->getOVResults(), true));
        ConfigureStreamsExecutor(networkName);
        OV_ITT_TASK_SKIP(EXECUTABLE_NETWORK_IMPORT);
    } catch (const std::exception& ex) {
        IE_THROW() << ex.what();
    } catch (...) {
        _logger.error("Unexpected exception");
        IE_THROW() << "VPUX ExecutableNetwork got unexpected exception from compiler";
    }
}

void ExecutableNetwork::ConfigureStreamsExecutor(const std::string& networkName) {
    size_t maxTaskExecutorGetResultCount = 1;
    if (_config.get<EXCLUSIVE_ASYNC_REQUESTS>()) {
        IE::ExecutorManager* executorManager = IE::ExecutorManager::getInstance();
        _taskExecutor = executorManager->getExecutor("VPUX");
        maxTaskExecutorGetResultCount = 1;
    } else {
        _taskExecutor = std::make_shared<IE::CPUStreamsExecutor>(IE::IStreamsExecutor::Config{
                "VPUXPlugin executor", checked_cast<int>(_config.get<EXECUTOR_STREAMS>())});
        maxTaskExecutorGetResultCount = _config.get<EXECUTOR_STREAMS>();
    }

    for (size_t i = 0; i < maxTaskExecutorGetResultCount; i++) {
        std::stringstream idStream;
        idStream << networkName << "_VPUXResultExecutor" << i;
        _taskExecutorGetResultIds.emplace(idStream.str());
    }
}

IE::ITaskExecutor::Ptr ExecutableNetwork::getNextTaskExecutor() {
    std::string id = _taskExecutorGetResultIds.front();

    _taskExecutorGetResultIds.pop();
    _taskExecutorGetResultIds.push(id);

    IE::ExecutorManager* executorManager = IE::ExecutorManager::getInstance();
    IE::ITaskExecutor::Ptr taskExecutor = executorManager->getExecutor(id);

    return taskExecutor;
}

//------------------------------------------------------------------------------
//      Create infer requests
//------------------------------------------------------------------------------
IE::IInferRequestInternal::Ptr ExecutableNetwork::CreateInferRequestImpl(const IE::InputsDataMap networkInputs,
                                                                         const IE::OutputsDataMap networkOutputs) {
    OV_ITT_SCOPED_TASK(itt::domains::VPUXPlugin, "ExecutableNetwork::CreateInferRequestImpl");
    if (!_executorPtr) {
        _executorPtr = createExecutor(_networkPtr, _config, _device);
        ConfigureStreamsExecutor(_networkName);
    }
    const auto inferExecutor = getExecutorForInference(_executorPtr, _logger);
    const auto allocator = _device->getAllocator();
    return _device->createInferRequest(networkInputs, networkOutputs, inferExecutor, _config, _networkName, _parameters,
                                       _results, allocator);
}

InferenceEngine::IInferRequestInternal::Ptr ExecutableNetwork::CreateInferRequest() {
    OV_ITT_SCOPED_TASK(itt::domains::VPUXPlugin, "ExecutableNetwork::CreateInferRequest");
    if (!_executorPtr) {
        _executorPtr = createExecutor(_networkPtr, _config, _device);
        ConfigureStreamsExecutor(_networkName);
    }
    const auto inferExecutor = getExecutorForInference(_executorPtr, _logger);
    const auto allocator = _device->getAllocator();
    auto syncRequestImpl = _device->createInferRequest(_networkInputs, _networkOutputs, inferExecutor, _config,
                                                       _networkName, _parameters, _results, allocator);
    syncRequestImpl->setPointerToExecutableNetworkInternal(shared_from_this());
    return std::make_shared<AsyncInferRequest>(syncRequestImpl, _taskExecutor, getNextTaskExecutor(),
                                               _callbackExecutor);
}

//------------------------------------------------------------------------------
//      Export
//------------------------------------------------------------------------------

namespace {
std::uint32_t hash(const std::vector<char>& data) {
    std::uint32_t result = 1171117u;
    for (const char& c : data)
        result = ((result << 7) + result) + static_cast<uint32_t>(c);
    return result;
}

}  // namespace

void ExecutableNetwork::Export(std::ostream& model) {
    auto graphBlob = _networkPtr->getCompiledNetwork();
    model.write(graphBlob.data(), graphBlob.size());
    std::stringstream str;
    str << "Blob hash: " << std::hex << hash(graphBlob);
    _logger.info("{0}", str.str());
}

void ExecutableNetwork::Export(const std::string& modelFileName) {
    std::ofstream modelFile(modelFileName, std::ios::binary);

    if (modelFile.is_open()) {
        Export(modelFile);
    } else {
        IE_THROW() << "The " << modelFileName << " file can not be opened for export.";
    }
}

//------------------------------------------------------------------------------
//      Config and Metrics
//------------------------------------------------------------------------------

IE::Parameter ExecutableNetwork::GetMetric(const std::string& name) const {
    if (_plugin->GetCore()->isNewAPI()) {
        const auto RO_property = [](const std::string& propertyName) {
            return ov::PropertyName(propertyName, ov::PropertyMutability::RO);
        };

        if (name == ov::supported_properties) {
            // [Track number: E#35689]
            static const std::vector<ov::PropertyName> supportedProperties{
                    RO_property(ov::supported_properties.name()),
                    RO_property(ov::model_name.name()),
                    RO_property(ov::optimal_number_of_infer_requests.name()),
                    RO_property(ov::hint::model_priority.name()),
                    RO_property(ov::device::id.name()),
                    RO_property(ov::intel_vpux::csram_size.name()),
                    RO_property(ov::intel_vpux::executor_streams.name()),
                    RO_property(ov::intel_vpux::graph_color_format.name()),
                    RO_property(ov::intel_vpux::inference_shaves.name()),
                    RO_property(ov::intel_vpux::inference_timeout.name()),
                    RO_property(ov::intel_vpux::preprocessing_lpi.name()),
                    RO_property(ov::intel_vpux::preprocessing_pipes.name()),
                    RO_property(ov::intel_vpux::preprocessing_shaves.name()),
                    RO_property(ov::intel_vpux::print_profiling.name()),
                    RO_property(ov::intel_vpux::profiling_output_file.name()),
                    RO_property(ov::intel_vpux::use_m2i.name()),
                    RO_property(ov::intel_vpux::use_shave_only_m2i.name()),
                    RO_property(ov::intel_vpux::use_sipp.name()),
                    RO_property(ov::intel_vpux::vpux_platform.name())};
            return supportedProperties;
        } else if (name == ov::model_name) {
            VPUX_THROW_WHEN(_networkPtr == nullptr, "GetMetric: network is not initialized");
            return _networkPtr->getName();
        } else if (name == ov::optimal_number_of_infer_requests) {
            VPUX_THROW_WHEN(_networkPtr == nullptr, "GetMetric: network is not initialized");
            return static_cast<uint32_t>(2 * _networkPtr->getNumStreams());
        } else if (name == ov::device::id) {
            return _config.get<DEVICE_ID>();
        } else if (name == ov::intel_vpux::csram_size) {
            return _config.get<CSRAM_SIZE>();
        } else if (name == ov::intel_vpux::executor_streams) {
            return _config.get<EXECUTOR_STREAMS>();
        } else if (name == ov::intel_vpux::graph_color_format) {
            return _config.get<GRAPH_COLOR_FORMAT>();
        } else if (name == ov::intel_vpux::inference_shaves) {
            return _config.get<INFERENCE_SHAVES>();
        } else if (name == ov::intel_vpux::inference_timeout) {
            return _config.get<INFERENCE_TIMEOUT_MS>();
        } else if (name == ov::intel_vpux::preprocessing_lpi) {
            return _config.get<PREPROCESSING_LPI>();
        } else if (name == ov::intel_vpux::preprocessing_pipes) {
            return _config.get<PREPROCESSING_PIPES>();
        } else if (name == ov::intel_vpux::preprocessing_shaves) {
            return _config.get<PREPROCESSING_SHAVES>();
        } else if (name == ov::intel_vpux::print_profiling) {
            return _config.get<PRINT_PROFILING>();
        } else if (name == ov::intel_vpux::profiling_output_file) {
            return _config.get<PROFILING_OUTPUT_FILE>();
        } else if (name == ov::intel_vpux::use_m2i) {
            return _config.get<USE_M2I>();
        } else if (name == ov::intel_vpux::use_shave_only_m2i) {
            return _config.get<USE_SHAVE_ONLY_M2I>();
        } else if (name == ov::intel_vpux::use_sipp) {
            return _config.get<USE_SIPP>();
        } else if (name == ov::intel_vpux::vpux_platform) {
            return _config.get<PLATFORM>();
        } else if (name == ov::hint::model_priority) {
            return _config.get<MODEL_PRIORITY>();
        }
    }

    if (name == METRIC_KEY(NETWORK_NAME)) {
        VPUX_THROW_WHEN(_networkPtr == nullptr, "GetMetric: network is not initialized");
        IE_SET_METRIC_RETURN(NETWORK_NAME, _networkPtr->getName());
    } else if (name == METRIC_KEY(OPTIMAL_NUMBER_OF_INFER_REQUESTS)) {
        VPUX_THROW_WHEN(_networkPtr == nullptr, "GetMetric: network is not initialized");
        IE_SET_METRIC_RETURN(OPTIMAL_NUMBER_OF_INFER_REQUESTS,
                             static_cast<unsigned int>(2 * _networkPtr->getNumStreams()));
    }

    VPUX_THROW("Unsupported metric {0}", name);
}

//------------------------------------------------------------------------------
std::atomic<int> ExecutableNetwork::loadBlobCounter{1};

Executor::Ptr ExecutableNetwork::createExecutor(const NetworkDescription::Ptr& network, const Config& config,
                                                const Device::Ptr& device) {
    loadBlobCounter++;  // increment blob static counter to make unique network ID
    if (network == nullptr) {
        IE_THROW() << "Network is null!";
    }

    // Default executor is nullptr, allow only perform export
    Executor::Ptr executor = nullptr;
    if (device != nullptr) {
        executor = device->createExecutor(network, config);
    }
    _networkName = _networkPtr->getName();
    return executor;
}

}  // namespace vpux
