//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

//

#include "kmb_regression_target.hpp"

#include <file_reader.h>
#include <gtest/gtest.h>
#include <precision_utils.h>

#include <blob_factory.hpp>
#include <condition_variable>
#include <mutex>
#include <vpux/vpux_plugin_config.hpp>
#include <vpu_layers_tests.hpp>

#include "kmb_layers_tests.hpp"
#include "kmb_xml_tests.hpp"
#include "tests_timeout.hpp"
#include "yolo_helpers.hpp"

#include "vpux/utils/IE/blob.hpp"

using namespace ::testing;
using namespace InferenceEngine;
using namespace InferenceEngine::details;
using namespace TestsTimeout;
using namespace KmbRegressionTarget;

#if !defined(__arm__) && !defined(__aarch64__)
using kmbLayersTestsConvolution = kmbLayersTests_nightly;

// NB: Kmb doesn't support compilation and inference on the same device
TEST_F(kmbLayersTestsConvolution, DISABLED_compilationLoadNetworkAndInfer) {
    std::string model = convolution_u8_only;

    const size_t convolutionWeightsByteSize = 36864;
    const size_t convolutionWeightsSize = convolutionWeightsByteSize / sizeof(uint8_t);

    const size_t biasByteSize = 256;
    const size_t biasSize = biasByteSize / sizeof(int32_t);

    auto convolutionWeightsBuffer =
        make_shared_blob<uint8_t>({Precision::U8, {convolutionWeightsByteSize + biasByteSize}, Layout::C});
    convolutionWeightsBuffer->allocate();
    auto weightsBufferData = convolutionWeightsBuffer->buffer().as<uint8_t*>();
    for (size_t i = 0; i < convolutionWeightsSize; ++i) {
        weightsBufferData[i] = 1;
    }

    uint32_t* biasData =
        reinterpret_cast<uint32_t*>(convolutionWeightsBuffer->buffer().as<uint8_t*>() + convolutionWeightsSize);
    for (size_t i = 0; i < biasSize; ++i) {
        biasData[i] = 1lu;
    }

    CNNNetwork network;
    ASSERT_NO_THROW(network = core->ReadNetwork(model, convolutionWeightsBuffer));

    auto _inputsInfo = network.getInputsInfo();
    _inputsInfo["input"]->setPrecision(Precision::U8);

    auto _outputsInfo = network.getOutputsInfo();
    _outputsInfo["conv2"]->setPrecision(Precision::U8);

    std::map<std::string, std::string> config;
    setCommonConfig(config);

    Core ie;
    InferenceEngine::ExecutableNetwork exeNetwork;
    ASSERT_NO_THROW(exeNetwork = core->LoadNetwork(network, deviceName, config));

    InferenceEngine::InferRequest inferRequest;
    ASSERT_NO_THROW(inferRequest = exeNetwork.CreateInferRequest());

    std::string input_name = exeNetwork.GetInputsInfo().begin()->first;
    std::string output_name = exeNetwork.GetOutputsInfo().begin()->first;

    Blob::Ptr inputBlob;
    ASSERT_NO_THROW(inputBlob = inferRequest.GetBlob(input_name));

    ASSERT_NO_THROW(inferRequest.Infer());

    Blob::Ptr outputBlob;
    ASSERT_NO_THROW(outputBlob = inferRequest.GetBlob(output_name));
}
#else

const static std::vector<modelBlobsInfo> pathToPreCompiledGraph = {
    {
        ._graphPath = "/KMB_models/BLOBS/mobilenet-v2/schema-3.24.3/mobilenet-v2.blob",
        ._inputPath = "/KMB_models/BLOBS/mobilenet-v2/input.bin",
        ._outputPath = "/KMB_models/BLOBS/mobilenet-v2/output.bin",
    },
    {
        ._graphPath = "/KMB_models/BLOBS/resnet-50/schema-3.24.3/resnet-50.blob",
        ._inputPath = "/KMB_models/BLOBS/resnet-50/input.bin",
        ._outputPath = "/KMB_models/BLOBS/resnet-50/output.bin",
    },
    {
        ._graphPath = "/KMB_models/BLOBS/tiny-yolo-v2/schema-3.24.3/tiny-yolo-v2.blob",
        ._inputPath = "/KMB_models/BLOBS/tiny-yolo-v2/input.bin",
        ._outputPath = "/KMB_models/BLOBS/tiny-yolo-v2/output.bin",
    }};

void printTopResults(const Blob::Ptr& referenceOutputBlob, const Blob::Ptr& outputBlob, const std::string& graphSuffix) {
    Blob::Ptr refFP32 = vpux::toFP32(as<MemoryBlob>(referenceOutputBlob));
    Blob::Ptr outputFP32 = vpux::toFP32(as<MemoryBlob>(outputBlob));
    if (graphSuffix.rfind(YOLO_GRAPH_NAME) == graphSuffix.size() - YOLO_GRAPH_NAME.size()) {
        const auto imgWidth = outputFP32->getTensorDesc().getDims()[3];
        const auto imgHeight = outputFP32->getTensorDesc().getDims()[2];

        bool isTiny = true;
        float confThresh = 0.4;
        auto referenceResult = utils::parseYoloOutput(refFP32, imgWidth, imgHeight, confThresh, isTiny);
        auto actualResult = utils::parseYoloOutput(outputFP32, imgWidth, imgHeight, confThresh, isTiny);

        std::ostringstream outString;
        utils::printDetectionBBoxOutputs(actualResult, outString, {});
        std::cout << "out: " << std::endl << outString.str() << std::endl;

        std::ostringstream refString;
        utils::printDetectionBBoxOutputs(referenceResult, refString, {});
        std::cout << "ref: " << std::endl << refString.str() << std::endl;
    } else {
        auto refTopClasses = Comparators::yieldTopClasses<float>(refFP32, NUMBER_OF_TOP_CLASSES);
        auto outTopClasses = Comparators::yieldTopClasses<float>(outputFP32, NUMBER_OF_TOP_CLASSES);
        std::cout << "out:";
        for (auto classId : outTopClasses) {
            std::cout << " " << classId;
        }
        std::cout << std::endl;
        std::cout << "ref:";
        for (auto classId : refTopClasses) {
            std::cout << " " << classId;
        }
        std::cout << std::endl;
    }
}

TEST_P(VpuInferWithPath, DISABLED_canDoInferenceOnImportedBlob) {
    modelBlobsInfo blobsInfo = GetParam();
    std::string modelFilePath = ModelsPath() + blobsInfo._graphPath;

    Core ie;
    InferenceEngine::ExecutableNetwork importedNetwork;
    ASSERT_NO_THROW(importedNetwork = core->ImportNetwork(modelFilePath, deviceName));

    InferenceEngine::InferRequest inferRequest;
    ASSERT_NO_THROW(inferRequest = importedNetwork.CreateInferRequest());
    ASSERT_NO_THROW(inferRequest.Infer());
}

TEST_P(VpuInferWithPath, DISABLED_compareInferenceOutputWithReference) {
    modelBlobsInfo blobsInfo = GetParam();
    std::string graphSuffix = blobsInfo._graphPath;
    std::string inputSuffix = blobsInfo._inputPath;
    std::string outputSuffix = blobsInfo._outputPath;
    std::string modelFilePath = ModelsPath() + graphSuffix;
    if (graphSuffix.rfind(YOLO_GRAPH_NAME) == graphSuffix.size() - YOLO_GRAPH_NAME.size()) {
        GTEST_SKIP() << "Disabled due to H#18012088819";
    }

    Core ie;
    InferenceEngine::ExecutableNetwork importedNetwork;
    ASSERT_NO_THROW(importedNetwork = core->ImportNetwork(modelFilePath, deviceName));

    InferenceEngine::InferRequest inferRequest;
    ASSERT_NO_THROW(inferRequest = importedNetwork.CreateInferRequest());

    ConstInputsDataMap inputInfo;
    inputInfo = importedNetwork.GetInputsInfo();

    std::string inputFilePath = ModelsPath() + inputSuffix;
    for (auto& item : inputInfo) {
        auto inferInputBlob = inferRequest.GetBlob(item.first.c_str());
        InferenceEngine::Blob::Ptr inputBlob;
        ASSERT_NO_THROW(
            inputBlob = vpu::KmbPlugin::utils::fromBinaryFile(inputFilePath, inferInputBlob->getTensorDesc()));
        ASSERT_NO_THROW(inferRequest.SetBlob(item.first.c_str(), inputBlob));
    }

    ASSERT_NO_THROW(inferRequest.Infer());

    ConstOutputsDataMap outputInfo;
    outputInfo = importedNetwork.GetOutputsInfo();

    for (auto& item : outputInfo) {
        Blob::Ptr outputBlob = inferRequest.GetBlob(item.first.c_str());

        TensorDesc outputBlobTensorDesc = outputBlob->getTensorDesc();
        Blob::Ptr referenceOutputBlob;
        std::string referenceOutputFilePath = ModelsPath() + outputSuffix;
        ASSERT_NO_THROW(
            referenceOutputBlob = vpu::KmbPlugin::utils::fromBinaryFile(referenceOutputFilePath, outputBlobTensorDesc));
        Blob::Ptr refFP32 = vpux::toFP32(as<MemoryBlob>(referenceOutputBlob));
        Blob::Ptr outputFP32 = vpux::toFP32(as<MemoryBlob>(outputBlob));
        if (graphSuffix.rfind(YOLO_GRAPH_NAME) == graphSuffix.size() - YOLO_GRAPH_NAME.size()) {
            Compare(refFP32, outputFP32, 0.0f);
        } else {
            ASSERT_NO_THROW(compareTopClasses(outputFP32, refFP32, NUMBER_OF_TOP_CLASSES));
        }
    }
}

class VpuInferAndCompareTestsWithParam :
    public vpuLayersTests,
    public testing::WithParamInterface<std::tuple<bool, modelBlobsInfo>> {};

TEST_P(VpuInferAndCompareTestsWithParam, DISABLED_multipleInferRequests) {
    std::tuple<bool, modelBlobsInfo> paramTuple = GetParam();
    bool isSync = std::get<0>(paramTuple);
    modelBlobsInfo blobsInfo = std::get<1>(paramTuple);
    std::string graphSuffix = blobsInfo._graphPath;
    std::string inputSuffix = blobsInfo._inputPath;
    std::string outputSuffix = blobsInfo._outputPath;
    std::string modelFilePath = ModelsPath() + graphSuffix;
    if (graphSuffix.rfind(YOLO_GRAPH_NAME) == graphSuffix.size() - YOLO_GRAPH_NAME.size()) {
        GTEST_SKIP() << "Disabled due to H#18012088819";
    }

    Core ie;
    InferenceEngine::ExecutableNetwork importedNetwork;
    ASSERT_NO_THROW(importedNetwork = core->ImportNetwork(modelFilePath, deviceName));

    std::list<InferenceEngine::InferRequest> requestList;
    const int REQUEST_LIMIT = 10;
    for (int requestCount = 0; requestCount < REQUEST_LIMIT; requestCount++) {
        InferenceEngine::InferRequest inferRequest;
        ASSERT_NO_THROW(inferRequest = importedNetwork.CreateInferRequest());
        requestList.push_back(inferRequest);
    }

    std::string inputFilePath = ModelsPath() + inputSuffix;

    ConstInputsDataMap inputInfo;
    inputInfo = importedNetwork.GetInputsInfo();

    for (auto& item : inputInfo) {
        for (auto currentRequest : requestList) {
            Blob::Ptr inputBlob;
            Blob::Ptr inferInputBlob;
            inferInputBlob = currentRequest.GetBlob(item.first.c_str());
            ASSERT_NO_THROW(
                inputBlob = vpu::KmbPlugin::utils::fromBinaryFile(inputFilePath, inferInputBlob->getTensorDesc()));
            ASSERT_NO_THROW(currentRequest.SetBlob(item.first.c_str(), inputBlob));
        }
    }

    const int MAX_WAIT = 60000;
    auto requestRoutine = [MAX_WAIT](InferenceEngine::InferRequest request) -> void {
        ResponseDesc response;
        ASSERT_NO_THROW(request.StartAsync());
        ASSERT_EQ(StatusCode::OK, request.Wait(MAX_WAIT)) << response.msg;
    };

    if (isSync) {
        // synchronous execution
        for (InferenceEngine::InferRequest& currentRequest : requestList) {
            ASSERT_NO_THROW(currentRequest.Infer());
        }
    } else {
        // asynchronous execution
        std::list<std::thread> requestThreadList;
        for (InferenceEngine::InferRequest& currentRequest : requestList) {
            requestThreadList.push_back(std::thread(requestRoutine, currentRequest));
        }

        for (std::thread& requestThread : requestThreadList) {
            requestThread.join();
        }
    }

    ConstOutputsDataMap outputInfo;
    outputInfo = importedNetwork.GetOutputsInfo();

    std::string referenceOutputFilePath = ModelsPath() + outputSuffix;
    for (auto& item : outputInfo) {
        for (InferenceEngine::InferRequest& inferRequest : requestList) {
            Blob::Ptr outputBlob;
            outputBlob = inferRequest.GetBlob(item.first.c_str());

            TensorDesc outputBlobTensorDesc = outputBlob->getTensorDesc();
            Blob::Ptr referenceOutputBlob;

            ASSERT_NO_THROW(referenceOutputBlob =
                                vpu::KmbPlugin::utils::fromBinaryFile(referenceOutputFilePath, outputBlobTensorDesc));
            Blob::Ptr refFP32 = vpux::toFP32(as<MemoryBlob>(referenceOutputBlob));
            Blob::Ptr outputFP32 = vpux::toFP32(as<MemoryBlob>(outputBlob));
            if (graphSuffix.rfind(YOLO_GRAPH_NAME) == graphSuffix.size() - YOLO_GRAPH_NAME.size()) {
                Compare(refFP32, outputFP32, 0.0f);
            } else {
                ASSERT_NO_THROW(compareTopClasses(outputFP32, refFP32, NUMBER_OF_TOP_CLASSES));
            }
        }
    }
}

TEST_P(VpuInferWithPath, DISABLED_asyncInferCallback) {
    modelBlobsInfo blobsInfo = GetParam();
    std::string graphSuffix = blobsInfo._graphPath;
    std::string inputSuffix = blobsInfo._inputPath;
    std::string outputSuffix = blobsInfo._outputPath;
    std::string modelFilePath = ModelsPath() + graphSuffix;
    if (graphSuffix.rfind(YOLO_GRAPH_NAME) == graphSuffix.size() - YOLO_GRAPH_NAME.size()) {
        GTEST_SKIP() << "Disabled due to H#18012088819";
    }

    InferenceEngine::ExecutableNetwork importedNetwork;
    ASSERT_NO_THROW(importedNetwork = core->ImportNetwork(modelFilePath, deviceName));

    std::list<InferenceEngine::InferRequest> requestList;
    const int REQUEST_LIMIT = 10;
    for (int requestCount = 0; requestCount < REQUEST_LIMIT; requestCount++) {
        InferenceEngine::InferRequest inferRequest;
        ASSERT_NO_THROW(inferRequest = importedNetwork.CreateInferRequest());
        requestList.push_back(inferRequest);
    }

    std::string inputFilePath = ModelsPath() + inputSuffix;

    ConstInputsDataMap inputInfo = importedNetwork.GetInputsInfo();

    for (auto& item : inputInfo) {
        for (auto currentRequest : requestList) {
            Blob::Ptr inputBlob;
            Blob::Ptr inferInputBlob;
            ASSERT_NO_THROW(inferInputBlob = currentRequest.GetBlob(item.first.c_str()));
            ASSERT_NO_THROW(
                inputBlob = vpu::KmbPlugin::utils::fromBinaryFile(inputFilePath, inferInputBlob->getTensorDesc()));
            ASSERT_NO_THROW(currentRequest.SetBlob(item.first.c_str(), inputBlob));
        }
    }

    std::mutex requestCounterGuard;
    volatile int completedRequests = 0;
    auto onComplete = [&completedRequests, &requestCounterGuard](void) -> void {
        std::lock_guard<std::mutex> incrementLock(requestCounterGuard);
        completedRequests++;
    };

    // asynchronous execution
    for (InferenceEngine::InferRequest& currentRequest : requestList) {
        currentRequest.SetCompletionCallback(onComplete);
        ASSERT_NO_THROW(currentRequest.StartAsync());
    }

    const int MAX_WAIT = 60000;
    auto waitRoutine = [&completedRequests, MAX_WAIT, REQUEST_LIMIT](void) -> void {
        std::chrono::system_clock::time_point endTime =
            std::chrono::system_clock::now() + std::chrono::milliseconds(MAX_WAIT);

        while (completedRequests < REQUEST_LIMIT) {
            ASSERT_LE(std::chrono::system_clock::now(), endTime);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    };

    std::thread waitThread(waitRoutine);
    waitThread.join();

    ConstOutputsDataMap outputInfo = importedNetwork.GetOutputsInfo();

    std::string referenceOutputFilePath = ModelsPath() + outputSuffix;
    for (auto& item : outputInfo) {
        for (InferenceEngine::InferRequest& inferRequest : requestList) {
            Blob::Ptr outputBlob;
            ASSERT_NO_THROW(outputBlob = inferRequest.GetBlob(item.first.c_str()));

            TensorDesc outputBlobTensorDesc = outputBlob->getTensorDesc();
            Blob::Ptr referenceOutputBlob;
            ASSERT_NO_THROW(referenceOutputBlob =
                                vpu::KmbPlugin::utils::fromBinaryFile(referenceOutputFilePath, outputBlobTensorDesc));
            Blob::Ptr refFP32 = vpux::toFP32(as<MemoryBlob>(referenceOutputBlob));
            Blob::Ptr outputFP32 = vpux::toFP32(as<MemoryBlob>(outputBlob));
            if (graphSuffix.rfind(YOLO_GRAPH_NAME) == graphSuffix.size() - YOLO_GRAPH_NAME.size()) {
                Compare(refFP32, outputFP32, 0.0f);
            } else {
                ASSERT_NO_THROW(compareTopClasses(outputFP32, refFP32, NUMBER_OF_TOP_CLASSES));
            }
        }
    }
}

TEST_P(VpuInferWithPath, DISABLED_asyncInferCallbackRecursive) {
    modelBlobsInfo blobsInfo = GetParam();
    std::string graphSuffix = blobsInfo._graphPath;
    std::string inputSuffix = blobsInfo._inputPath;
    std::string outputSuffix = blobsInfo._outputPath;
    std::string modelFilePath = ModelsPath() + graphSuffix;
    if (graphSuffix.rfind(YOLO_GRAPH_NAME) == graphSuffix.size() - YOLO_GRAPH_NAME.size()) {
        GTEST_SKIP() << "Disabled due to H#18012088819";
    }

    InferenceEngine::ExecutableNetwork importedNetwork;
    ASSERT_NO_THROW(importedNetwork = core->ImportNetwork(modelFilePath, deviceName));

    InferenceEngine::InferRequest inferRequest;
    ASSERT_NO_THROW(inferRequest = importedNetwork.CreateInferRequest());

    std::string inputFilePath = ModelsPath() + inputSuffix;

    ConstInputsDataMap inputInfo = importedNetwork.GetInputsInfo();

    for (auto& item : inputInfo) {
        Blob::Ptr inputBlob;
        Blob::Ptr inferInputBlob;
        ASSERT_NO_THROW(inferInputBlob = inferRequest.GetBlob(item.first.c_str()));
        ASSERT_NO_THROW(
            inputBlob = vpu::KmbPlugin::utils::fromBinaryFile(inputFilePath, inferInputBlob->getTensorDesc()));
        ASSERT_NO_THROW(inferRequest.SetBlob(item.first.c_str(), inputBlob));
    }

    const std::size_t MAX_ITERATIONS = 10;
    volatile std::size_t iterationCount = 0;
    std::condition_variable waitCompletion;

    auto onComplete = [&waitCompletion, &iterationCount, &inferRequest](void) -> void {
        iterationCount++;
        if (iterationCount < MAX_ITERATIONS) {
            ASSERT_NO_THROW(inferRequest.StartAsync());
        } else {
            waitCompletion.notify_one();
        }
    };

    inferRequest.SetCompletionCallback(onComplete);

    ASSERT_NO_THROW(inferRequest.StartAsync());

    std::mutex execGuard;
    std::unique_lock<std::mutex> execLocker(execGuard);
    waitCompletion.wait(execLocker, [&] {
        return iterationCount == MAX_ITERATIONS;
    });

    ConstOutputsDataMap outputInfo = importedNetwork.GetOutputsInfo();

    std::string referenceOutputFilePath = ModelsPath() + outputSuffix;
    for (auto& item : outputInfo) {
        Blob::Ptr outputBlob;
        ASSERT_NO_THROW(outputBlob = inferRequest.GetBlob(item.first.c_str()));

        TensorDesc outputBlobTensorDesc = outputBlob->getTensorDesc();
        Blob::Ptr referenceOutputBlob;

        ASSERT_NO_THROW(
            referenceOutputBlob = vpu::KmbPlugin::utils::fromBinaryFile(referenceOutputFilePath, outputBlobTensorDesc));

        Blob::Ptr refFP32 = vpux::toFP32(as<MemoryBlob>(referenceOutputBlob));
        Blob::Ptr outputFP32 = vpux::toFP32(as<MemoryBlob>(outputBlob));
        if (graphSuffix.rfind(YOLO_GRAPH_NAME) == graphSuffix.size() - YOLO_GRAPH_NAME.size()) {
            Compare(refFP32, outputFP32, 0.0f);
        } else {
            ASSERT_NO_THROW(compareTopClasses(outputFP32, refFP32, NUMBER_OF_TOP_CLASSES));
        }
    }
}

const static std::vector<bool> isSyncVec = {false, true};

INSTANTIATE_TEST_SUITE_P(precommit, VpuInferAndCompareTestsWithParam,
    ::testing::Combine(::testing::ValuesIn(isSyncVec), ::testing::ValuesIn(pathToPreCompiledGraph)));

TEST_P(VpuInferWithPath, DISABLED_compareOutputsTwoNetworks) {
    modelBlobsInfo blobsInfo = GetParam();
    std::string graphSuffix = blobsInfo._graphPath;
    std::string inputSuffix = blobsInfo._inputPath;
    std::string outputSuffix = blobsInfo._outputPath;
    std::string modelFilePath = ModelsPath() + graphSuffix;
    std::string inputNameFilePath = ModelsPath() + inputSuffix;
    std::string outputNameFilePath = ModelsPath() + outputSuffix;

    Core ie;
    InferenceEngine::ExecutableNetwork importedNetwork1;
    ASSERT_NO_THROW(importedNetwork1 = core->ImportNetwork(modelFilePath, deviceName));
    InferenceEngine::ExecutableNetwork importedNetwork2;
    ASSERT_NO_THROW(importedNetwork2 = core->ImportNetwork(modelFilePath, deviceName));

    InferenceEngine::InferRequest inferRequest1;
    ASSERT_NO_THROW(inferRequest1 = importedNetwork1.CreateInferRequest());

    std::string input_name1 = importedNetwork1.GetInputsInfo().begin()->first;
    std::string output_name1 = importedNetwork1.GetOutputsInfo().begin()->first;

    InferenceEngine::TensorDesc outputTensorDesc = inferRequest1.GetBlob(output_name1)->getTensorDesc();

    Blob::Ptr fileOutputBlob;
    ASSERT_NO_THROW(fileOutputBlob = vpu::KmbPlugin::utils::fromBinaryFile(outputNameFilePath, outputTensorDesc));

    Blob::Ptr inputBlob1;
    ASSERT_NO_THROW(inputBlob1 = inferRequest1.GetBlob(input_name1));

    Blob::Ptr blobFromFile;
    ASSERT_NO_THROW(
        blobFromFile = vpu::KmbPlugin::utils::fromBinaryFile(inputNameFilePath, inputBlob1->getTensorDesc()));

    {
        const auto inMem = blobFromFile->cbuffer();
        const auto outMem = inputBlob1->buffer();

        const auto inPtr = inMem.as<const uint8_t*>();
        const auto outPtr = outMem.as<uint8_t*>();

        std::copy_n(inPtr, blobFromFile->byteSize(), outPtr);
    }

    ASSERT_NO_THROW(inferRequest1.Infer());
    Blob::Ptr outputBlob1;
    ASSERT_NO_THROW(outputBlob1 = inferRequest1.GetBlob(output_name1));

    // --------------------

    InferenceEngine::InferRequest InferRequest2;
    ASSERT_NO_THROW(InferRequest2 = importedNetwork2.CreateInferRequest());

    std::string input_name2 = importedNetwork2.GetInputsInfo().begin()->first;
    std::string output_name2 = importedNetwork2.GetOutputsInfo().begin()->first;

    ASSERT_NO_THROW(InferRequest2.SetBlob(input_name2, inputBlob1));
    ASSERT_NO_THROW(InferRequest2.Infer());
    Blob::Ptr outputBlob2;
    ASSERT_NO_THROW(outputBlob2 = InferRequest2.GetBlob(output_name2));
    ASSERT_EQ(outputBlob1->byteSize(), outputBlob2->byteSize());

    if (graphSuffix.rfind(YOLO_GRAPH_NAME) == graphSuffix.size() - YOLO_GRAPH_NAME.size()) {
        Blob::Ptr blobFP32 = vpux::toFP32(as<MemoryBlob>(outputBlob2));
        Blob::Ptr expectedBlobFP32 = vpux::toFP32(as<MemoryBlob>(outputBlob1));
        printTopResults(expectedBlobFP32, blobFP32, graphSuffix);

        blobFP32 = vpux::toFP32(as<MemoryBlob>(outputBlob2));
        expectedBlobFP32 = vpux::toFP32(as<MemoryBlob>(fileOutputBlob));
        printTopResults(expectedBlobFP32, blobFP32, graphSuffix);
    } else {
        Blob::Ptr outputBlob2FP32 = vpux::toFP32(as<MemoryBlob>(outputBlob2));
        ASSERT_NO_THROW(compareTopClasses(outputBlob2FP32, vpux::toFP32(as<MemoryBlob>(outputBlob1)), NUMBER_OF_TOP_CLASSES));
        ASSERT_NO_THROW(compareTopClasses(outputBlob2FP32, vpux::toFP32(as<MemoryBlob>(fileOutputBlob)), NUMBER_OF_TOP_CLASSES));
    }
}

struct TopNetTest {
    modelBlobsInfo info;
    bool isComparable;
};

const static std::vector<TopNetTest> pathToTop3PreCompiledGraph = {
    {{
         ._graphPath = "/KMB_models/BLOBS/mobilenet-v2/schema-3.24.3/mobilenet-v2.blob",
         ._inputPath = "/KMB_models/BLOBS/mobilenet-v2/input.bin",
         ._outputPath = "/KMB_models/BLOBS/mobilenet-v2/output.bin",
     },
        true},
    {{
         ._graphPath = "/KMB_models/BLOBS/resnet-50/schema-3.24.3/resnet-50.blob",
         ._inputPath = "/KMB_models/BLOBS/resnet-50/input.bin",
         ._outputPath = "/KMB_models/BLOBS/resnet-50/output.bin",
     },
        true},
    {{
         ._graphPath = "/KMB_models/BLOBS/tiny-yolo-v2/schema-3.24.3/tiny-yolo-v2.blob",
         ._inputPath = "/KMB_models/BLOBS/tiny-yolo-v2/input.bin",
         ._outputPath = "/KMB_models/BLOBS/tiny-yolo-v2/output.bin",
     },
        false}};

class VpuInferWithPathForTop3Net : public vpuLayersTests, public testing::WithParamInterface<TopNetTest> {};

TEST_P(VpuInferWithPathForTop3Net, DISABLED_canDoInferenceOnTop3ImportedBlobs) {
    modelBlobsInfo blobsInfo = GetParam().info;
    std::string modelFilePath = ModelsPath() + blobsInfo._graphPath;
    std::string inputDataPath = ModelsPath() + blobsInfo._inputPath;
    std::string refOutputPath = ModelsPath() + blobsInfo._outputPath;

    InferenceEngine::ExecutableNetwork importedNetwork;
    ASSERT_NO_THROW(importedNetwork = core->ImportNetwork(modelFilePath, deviceName));

    InferenceEngine::InferRequest inferRequest;
    ASSERT_NO_THROW(inferRequest = importedNetwork.CreateInferRequest());
    auto inputBlobName = importedNetwork.GetInputsInfo().begin()->first;
    auto inferInputBlob = inferRequest.GetBlob(inputBlobName);
    auto inputBlob = vpu::KmbPlugin::utils::fromBinaryFile(inputDataPath, inferInputBlob->getTensorDesc());
    ASSERT_NO_THROW(inferRequest.SetBlob(inputBlobName, inputBlob));

    ASSERT_NO_THROW(inferRequest.Infer());

    if (!GetParam().isComparable) return;

    auto outputBlobName = importedNetwork.GetOutputsInfo().begin()->first;
    auto outputBlob = inferRequest.GetBlob(outputBlobName);

    InferenceEngine::Blob::Ptr refBlob;
    refBlob = vpu::KmbPlugin::utils::fromBinaryFile(refOutputPath, outputBlob->getTensorDesc());

    ASSERT_TRUE(outputBlob->byteSize() == refBlob->byteSize());
    ASSERT_TRUE(outputBlob->getTensorDesc().getPrecision() == Precision::FP16 ||
                outputBlob->getTensorDesc().getPrecision() == Precision::FP32);
    ASSERT_NO_THROW(compareTopClasses(
                        vpux::toFP32(as<MemoryBlob>(outputBlob)),
                        vpux::toFP32(as<MemoryBlob>(refBlob)),
                        NUMBER_OF_TOP_CLASSES));
}

INSTANTIATE_TEST_SUITE_P(precommit, VpuInferWithPath, ::testing::ValuesIn(pathToPreCompiledGraph));

INSTANTIATE_TEST_SUITE_P(precommit, VpuInferWithPathForTop3Net, ::testing::ValuesIn(pathToTop3PreCompiledGraph));
#endif
