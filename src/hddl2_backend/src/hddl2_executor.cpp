//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

//

#include "hddl2_executor.h"

#include "vpux/al/config/common.hpp"
#include "vpux/al/config/runtime.hpp"

#include "vpux/utils/IE/blob.hpp"

#include <ie_compound_blob.h>
#include <ie_memcpy.h>

#include <hddl2_backend.h>
#include <blob_factory.hpp>
#include <ie_preprocess.hpp>
#include <ie_remote_context.hpp>

#include "hddl2_exceptions.h"
#include "hddl2_helper.h"
#include "hddl_unite/hddl2_unite_graph.h"
#include "vpux_params_private_options.hpp"
#include "vpux_remote_context.h"

namespace vpux {
namespace hddl2 {

namespace IE = InferenceEngine;
//------------------------------------------------------------------------------
//      Helpers
//------------------------------------------------------------------------------
// TODO [Track number: S#21391]
// FIXME: does not work for batch != 1
static bool is2DTensor(const IE::SizeVector& dims) {
    size_t ones = std::count(dims.begin(), dims.end(), 1);
    return (dims.size() - ones) == 1;
}

static void copyDataToBlob(const IE::Blob::Ptr& dest, const void* source, const size_t& size) {
    if (source == nullptr) {
        IE_THROW() << "Source data is nullptr!";
    }
    if (dest->byteSize() != size) {
        IE_THROW() << "Output size mismatch between HddlUnite: " << size
                   << " and expected output: " << dest->byteSize();
    }
    IE::MemoryBlob::Ptr mblob = IE::as<IE::MemoryBlob>(dest);
    if (!mblob) {
        IE_THROW() << "Failed output blob type!";
    }
    auto lockedMemory = mblob->wmap();
    void* data = lockedMemory.as<void*>();
    auto result = ie_memcpy(data, dest->byteSize(), source, size);
    if (result != 0) {
        IE_THROW() << "Failed to copy memory.";
    }
}

static IE::Blob::Ptr prepareInputForInference(const IE::Blob::Ptr& actualInput, const IE::Layout& expectedLayout) {
    if (actualInput == nullptr) {
        IE_THROW() << "Actual input blob null pointer!";
    }
    if (actualInput->getTensorDesc().getLayout() == expectedLayout ||
        /** Currently we ignore information of what type of remote blob we are using **/
        actualInput->is<IE::RemoteBlob>() ||
        /** Repacking for NV12 Blob is not required, compound blob should be handled other way **/
        // TODO Add repacking for compound blob case
        actualInput->is<IE::NV12Blob>() || actualInput->is<IE::CompoundBlob>()) {
        return actualInput;
    }

    IE::Blob::Ptr inputForInference;

    if (is2DTensor(actualInput->getTensorDesc().getDims())) {
        auto tensorDims = actualInput->getTensorDesc().getDims();
        for (size_t dimInd = actualInput->getTensorDesc().getDims().size(); dimInd < 4; dimInd++) {
            tensorDims.push_back(1);
        }
        IE::TensorDesc TensorDesc = {actualInput->getTensorDesc().getPrecision(), tensorDims, expectedLayout};
        inputForInference = make_blob_with_precision(TensorDesc);
        inputForInference->allocate();

        auto src_mem_blob_ptr = IE::as<IE::MemoryBlob>(actualInput);
        if (src_mem_blob_ptr == nullptr)
            IE_THROW() << "Memory blob ptr is null";
        auto src_lock_mem = src_mem_blob_ptr->rmap();
        auto src_mem_ptr = src_lock_mem.as<float*>();
        if (src_mem_ptr == nullptr)
            IE_THROW() << "Blob memory is null";

        auto dst_mem_blob_ptr = IE::as<IE::MemoryBlob>(inputForInference);
        if (dst_mem_blob_ptr == nullptr)
            IE_THROW() << "Memory blob ptr is null";
        auto dst_lock_mem = dst_mem_blob_ptr->wmap();
        auto dst_mem_ptr = dst_lock_mem.as<float*>();
        if (dst_mem_ptr == nullptr)
            IE_THROW() << "Blob memory is null";

        ie_memcpy(dst_mem_ptr, inputForInference->byteSize(), src_mem_ptr, actualInput->byteSize());
    } else {
        if (actualInput->getTensorDesc().getDims().size() == 3) {
            // 3D CHW input
            auto tensorDims = actualInput->getTensorDesc().getDims();
            tensorDims.insert(tensorDims.begin(), 1);
            IE::TensorDesc tensorDesc = {actualInput->getTensorDesc().getPrecision(), tensorDims, IE::Layout::NCHW};
            IE::Blob::Ptr tmpBlobPtr = make_blob_with_precision(tensorDesc);
            tmpBlobPtr->allocate();

            auto src_mem_blob_ptr = IE::as<IE::MemoryBlob>(actualInput);
            if (src_mem_blob_ptr == nullptr)
                IE_THROW() << "Memory blob ptr is null";
            auto src_lock_mem = src_mem_blob_ptr->rmap();
            auto src_mem_ptr = src_lock_mem.as<float*>();
            if (src_mem_ptr == nullptr)
                IE_THROW() << "Blob memory is null";

            auto dst_mem_blob_ptr = IE::as<IE::MemoryBlob>(tmpBlobPtr);
            if (dst_mem_blob_ptr == nullptr)
                IE_THROW() << "Memory blob ptr is null";
            auto dst_lock_mem = dst_mem_blob_ptr->wmap();
            auto dst_mem_ptr = dst_lock_mem.as<float*>();
            if (dst_mem_ptr == nullptr)
                IE_THROW() << "Blob memory is null";

            ie_memcpy(dst_mem_ptr, tmpBlobPtr->byteSize(), src_mem_ptr, actualInput->byteSize());

            inputForInference = toLayout(IE::as<IE::MemoryBlob>(tmpBlobPtr), expectedLayout);
        } else {
            // 4D to 4D input conversion
            inputForInference = toLayout(IE::as<IE::MemoryBlob>(actualInput), expectedLayout);
        }
    }

    return inputForInference;
}
//------------------------------------------------------------------------------
std::atomic<size_t> HDDL2Executor::_executorIdCounter{0};
std::map<size_t, std::weak_ptr<HddlUniteGraph>> HDDL2Executor::_uniteGraphMap;

HDDL2Executor::Ptr HDDL2Executor::prepareExecutor(const vpux::NetworkDescription::Ptr& networkDesc,
                                                  const Config& config,
                                                  const std::shared_ptr<vpux::Allocator>& allocator,
                                                  const HddlUnite::WorkloadContext::Ptr& workloadContext) {
    const Logger logger("Executor", config.get<LOG_LEVEL>());

    HDDL2Executor::Ptr executor = nullptr;

    if (!HDDL2Backend::isServiceAvailable()) {
        return executor;
    }

    try {
        executor = std::make_shared<HDDL2Executor>(networkDesc, config, allocator, workloadContext);
    } catch (const IE::NetworkNotLoaded&) {
        logger.error("{0}", FAILED_LOAD_NETWORK);
    } catch (const IE::Exception& exception) {
        logger.error("{0} ERROR: {1}", EXECUTOR_NOT_CREATED, exception.what());
    } catch (const std::exception& exception) {
        logger.error("{0} ERROR: {1}", EXECUTOR_NOT_CREATED, exception.what());
    }
    return executor;
}

HDDL2Executor::HDDL2Executor(const vpux::NetworkDescription::CPtr& network, const Config& config,
                             const std::shared_ptr<vpux::Allocator>& allocator,
                             const HddlUnite::WorkloadContext::Ptr& workloadContext)
        // TODO Make executor logger name unique
        : _config(config),
          _logger("Executor", config.get<LOG_LEVEL>()),
          _network(network),
          _allocatorPtr(allocator),
          _workloadContext(workloadContext),
          _baseExecutorId(_executorIdCounter++) {
    setUniteLogLevel(_logger);
    _inferDataPtr = std::make_shared<InferDataAdapter>(_network, _workloadContext, _config.get<GRAPH_COLOR_FORMAT>());
}

HDDL2Executor::HDDL2Executor(const HDDL2Executor& ex)
        : _config(ex._config),
          _logger("Executor", _config.get<LOG_LEVEL>()),
          _network(ex._network),
          _uniteGraphPtr(ex._uniteGraphPtr),
          _allocatorPtr(ex._allocatorPtr),
          _workloadContext(ex._workloadContext),
          _baseExecutorId(ex._baseExecutorId) {
    _inferDataPtr = std::make_shared<InferDataAdapter>(_network, _workloadContext, _config.get<GRAPH_COLOR_FORMAT>());
}

void HDDL2Executor::setup(const InferenceEngine::ParamMap& params) {
    UNUSED(params);
    IE_THROW(NotImplemented);
}

void HDDL2Executor::push(const InferenceEngine::BlobMap& inputs) {
    push(inputs, {});
}

void HDDL2Executor::push(const InferenceEngine::BlobMap& inputs, const PreprocMap& preProcMap) {
    // TODO [Design flaw] InferData need to know if preprocessing required on creation [Track number: S#31308]
    bool needUnitePreProcessing = false;
    IE::BlobMap updatedInputs;

    try {
        loadGraphToDevice();
    } catch (const IE::NetworkNotLoaded&) {
        _logger.error("{0}", FAILED_LOAD_NETWORK);
        throw;
    } catch (const IE::Exception& exception) {
        _logger.error("{0} ERROR: {1}", FAILED_LOAD_NETWORK, exception.what());
        throw;
    } catch (const std::exception& exception) {
        _logger.error("{0} ERROR: {1}", FAILED_LOAD_NETWORK, exception.what());
        IE_THROW() << "Couldn't load the graph into the device." << exception.what();
    }

    const auto& networkInputs = _network->getInputsInfo();
    const auto& deviceInputs = _network->getDeviceInputsInfo();

    if (inputs.size() != networkInputs.size()) {
        _logger.warning("Amount of blobs and network inputs mismatch!\n"
                        "Blobs: {0}, network inputs: {1}",
                        inputs.size(), networkInputs.size());
    } else if (networkInputs.size() != deviceInputs.size()) {
        _logger.warning("Amount of network inputs and expected device inputs mismatch!\n"
                        "Network inputs: {0}, Device inputs: {1}",
                        networkInputs.size(), deviceInputs.size());
    }

    for (const auto& networkInput : networkInputs) {
        const std::string inputName = networkInput.first;
        auto foundInputBlob = inputs.find(inputName);
        if (foundInputBlob == inputs.end()) {
            IE_THROW() << "Error: input [" << inputName << "] is not provided.";
        }

        const IE::Blob::Ptr inputBlobPtr = foundInputBlob->second;
        if (preProcMap.find(inputName) != preProcMap.end()) {
            needUnitePreProcessing = true;
        }

        if (inputBlobPtr->is<IE::RemoteBlob>()) {
            const auto& param = std::static_pointer_cast<IE::RemoteBlob>(inputBlobPtr)->getParams();
            needUnitePreProcessing |= (param.find(IE::VPUX_PARAM_KEY(ROI_PTR)) != param.end());
        }

        const auto deviceInputLayout = deviceInputs.at(inputName)->getLayout();
        updatedInputs[foundInputBlob->first] = prepareInputForInference(foundInputBlob->second, deviceInputLayout);
    }

    _inferDataPtr->setPreprocessFlag(needUnitePreProcessing);

    // TODO Should we use deviceInputs instead of networkInputs here?
    for (const auto& networkInput : networkInputs) {
        const std::string inputName = networkInput.first;
        const IE::DataPtr inputDesc = networkInput.second;

        auto foundInputBlob = updatedInputs.find(inputName);
        if (foundInputBlob == updatedInputs.end()) {
            IE_THROW() << "Error: input [" << inputName << "] is not provided.";
        }
        const IE::Blob::Ptr inputBlobPtr = foundInputBlob->second;
        IE::ColorFormat inputColorFormat = IE::ColorFormat::BGR;
        // TODO ResizeAlgorithm information from preProcessInfo is not used [Track number: S#37393]
        if (preProcMap.find(inputName) != preProcMap.end()) {
            InferenceEngine::PreProcessInfo preProcessInfo = preProcMap.find(inputName)->second;
            inputColorFormat = preProcessInfo.getColorFormat();
        }
        _inferDataPtr->prepareUniteInput(inputBlobPtr, inputName, inputColorFormat);
    }

    _uniteGraphPtr->InferAsync(_inferDataPtr);
}

void HDDL2Executor::pull(InferenceEngine::BlobMap& outputs) {
    _inferDataPtr->waitInferDone();
    const auto& networkOutputs = _network->getOutputsInfo();
    const auto& deviceOutputs = _network->getDeviceOutputsInfo();
    for (const auto& networkOutput : networkOutputs) {
        const std::string outputName = networkOutput.first;
        auto foundOutputBlob = outputs.find(outputName);
        if (foundOutputBlob == outputs.end()) {
            IE_THROW() << "Error: output [" << outputName << "] is not provided.";
        }
        const auto& deviceOutput = deviceOutputs.find(outputName);
        if (deviceOutput == deviceOutputs.end()) {
            IE_THROW() << "Error: output [" << outputName << "] information for device not found.";
        }
        IE::Blob::Ptr outputBlobPtr = foundOutputBlob->second;

        const std::string outputUniteData = _inferDataPtr->getOutputData(outputName);

        const auto deviceTensorDesc = deviceOutput->second->getTensorDesc();
        const auto outputBlobTensorDesc = outputBlobPtr->getTensorDesc();

        const auto deviceOutputPrecision = deviceTensorDesc.getPrecision();
        const auto blobOutputPrecision = outputBlobTensorDesc.getPrecision();

        if (deviceOutputPrecision == IE::Precision::FP32 || blobOutputPrecision == IE::Precision::FP32) {
            if (deviceOutputPrecision == IE::Precision::U8 || blobOutputPrecision == IE::Precision::U8) {
                IE_THROW() << "Error: output precision conversion from " << deviceOutputPrecision << " to "
                           << blobOutputPrecision << " is not supported.";
            }
        } else {
            if (deviceOutputPrecision == IE::Precision::U8 && blobOutputPrecision == IE::Precision::FP16) {
                IE_THROW() << "Error: output precision conversion from " << deviceOutputPrecision << " to "
                           << blobOutputPrecision << " is not supported.";
            }
            if (outputUniteData.size() != outputBlobPtr->byteSize()) {
                IE_THROW() << "Output size mismatch between HddlUnite and network expected output";
            }
        }

        IE::Blob::Ptr deviceOutputBlob = make_blob_with_precision(deviceTensorDesc);
        deviceOutputBlob->allocate();
        copyDataToBlob(deviceOutputBlob, outputUniteData.data(), outputUniteData.size());
        outputBlobPtr = toPrecision(IE::as<IE::MemoryBlob>(deviceOutputBlob), blobOutputPrecision);

        if (deviceTensorDesc.getDims().size() == outputBlobTensorDesc.getDims().size()) {
            outputBlobPtr = toLayout(IE::as<IE::MemoryBlob>(outputBlobPtr), outputBlobTensorDesc.getLayout());
        } else {
            // FIXME If device and output layout dims are different, we do plain copy (see below)
            // Will be different behavior with channel minor and channel major layouts
            if (deviceTensorDesc.getLayout() == IE::Layout::NHWC &&
                outputBlobTensorDesc.getLayout() == IE::Layout::CHW) {
                outputBlobPtr = toLayout(IE::as<IE::MemoryBlob>(outputBlobPtr), IE::Layout::NCHW);
            }
        }

        auto memOutputBlob = IE::as<IE::MemoryBlob>(foundOutputBlob->second);
        IE_ASSERT(memOutputBlob != nullptr);
        auto memDeviceBlob = IE::as<IE::MemoryBlob>(outputBlobPtr);
        IE_ASSERT(memDeviceBlob != nullptr);
        auto memOutputLock = memOutputBlob->wmap();
        auto memDeviceLock = memDeviceBlob->rmap();
        std::memcpy(memOutputLock.as<uint8_t*>(), memDeviceLock.as<uint8_t*>(), outputBlobPtr->byteSize());
    }
}  // namespace hddl2

static bool preProcSupported(const IE::ResizeAlgorithm resizeAlgo, const IE::ColorFormat colorFormat) {
    return ((resizeAlgo == IE::RESIZE_BILINEAR) && (colorFormat == IE::ColorFormat::NV12)) ||
           (colorFormat == IE::ColorFormat::NV12);
}

bool HDDL2Executor::isPreProcessingSupported(const PreprocMap& preProcMap) const {
    if (preProcMap.empty()) {
        return true;
    }
    auto isPreProcSupported = true;
    for (const auto& input : preProcMap) {
        const auto& preProcInfo = input.second;
        const auto preProcessingSupported =
                preProcSupported(preProcInfo.getResizeAlgorithm(), preProcInfo.getColorFormat());
        _logger.debug("Preprocessing for color format '{0}' resize algorithm '{1}' is {2}.",
                      preProcInfo.getColorFormat(), preProcInfo.getResizeAlgorithm(),
                      preProcessingSupported ? "supported" : "not supported");
        isPreProcSupported &= preProcessingSupported;
    }
    return isPreProcSupported;
}

std::map<std::string, IE::InferenceEngineProfileInfo> HDDL2Executor::getLayerStatistics() {
    return _inferDataPtr->getHDDLUnitePerfCounters();
}

InferenceEngine::Parameter HDDL2Executor::getParameter(const std::string& paramName) const {
    UNUSED(paramName);
    IE_THROW(NotImplemented);
}

void HDDL2Executor::loadGraphToDevice() {
    std::unordered_map<std::string, std::string> hddlUniteConfig = {};
    const auto csramSize = _config.get<CSRAM_SIZE>();
    // HddlUnite requires CSRAM size in Kb
    const decltype(csramSize) toKilobytes = 1024;
    const auto csramSizeUnite = (csramSize >= 0) ? csramSize / toKilobytes : csramSize;
    hddlUniteConfig.insert(std::make_pair("CSRAM_SIZE", std::to_string(csramSizeUnite)));

    // Graph hasn't been initialized yet
    if (_uniteGraphPtr == nullptr) {
        std::lock_guard<std::mutex> lock(_uniteGraphMapMutex);
        const auto findUniteGraph = _uniteGraphMap.find(_baseExecutorId);
        // No graph in the map - need to load it to the device and add to the map
        if (findUniteGraph == _uniteGraphMap.end()) {
            if (_workloadContext == nullptr) {
                _uniteGraphPtr = std::make_shared<HddlUniteGraph>(
                        _network, getSwDeviceIdFromName(_config.get<DEVICE_ID>()), _config);
            } else {
                _uniteGraphPtr = std::make_shared<HddlUniteGraph>(_network, _workloadContext, _config);
            }
            _uniteGraphMap[_baseExecutorId] = _uniteGraphPtr;
        } else {
            // Graph was found - use it
            _uniteGraphPtr = findUniteGraph->second.lock();
            if (_uniteGraphPtr == nullptr) {
                IE_THROW() << "HddlUnite graph was found but deleted.";
            }
        }
    }
}

Executor::Ptr HDDL2Executor::clone() const {
    return std::make_shared<HDDL2Executor>(*this);
}

}  // namespace hddl2
}  // namespace vpux
