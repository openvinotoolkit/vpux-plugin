//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

//

#include <InferBlob.h>
#include <InferData.h>
#include <Inference.h>

#include "RemoteMemory.h"
#include "core_api.h"
#include "gtest/gtest.h"
#include "hddl2_helpers/helper_device_name.h"
#include "hddl2_helpers/helper_hddl_unite_graph.h"
#include "hddl2_helpers/helper_remote_memory.h"
#include "hddl2_helpers/helper_workload_context.h"
#include "executable_network_factory.h"
#include "models/models_constant.h"

using namespace HddlUnite;
namespace IE = InferenceEngine;

const std::string DUMP_PATH = []() -> std::string {
  if (const auto var = std::getenv("IE_KMB_TESTS_DUMP_PATH")) {
      return var;
  }

  return "/tmp/";
}();

//------------------------------------------------------------------------------
class HDDL2_HddlUnite_Tests : public ::testing::Test {
public:
    WorkloadContext_Helper workloadContextHelper;

    RemoteMemory_Helper remoteMemoryHelper;
};

//------------------------------------------------------------------------------
// TODO FAIL - HddlUnite problem
TEST_F(HDDL2_HddlUnite_Tests, DISABLED_WrapIncorrectWorkloadID_ThrowException) {
    const size_t incorrectWorkloadID = INT32_MAX;
    const size_t size = 1;

    ASSERT_ANY_THROW(remoteMemoryHelper.allocateRemoteMemory(incorrectWorkloadID, size));
}

// TODO FAIL - HdllUnite problem
TEST_F(HDDL2_HddlUnite_Tests, DISABLED_WrapNegativeWorkloadID_ThrowException) {
    const size_t incorrectWorkloadID = -1;
    const size_t size = 1;

    ASSERT_ANY_THROW(remoteMemoryHelper.allocateRemoteMemory(incorrectWorkloadID, size));
}

//------------------------------------------------------------------------------
TEST_F(HDDL2_HddlUnite_Tests, CanGetAvailableDevices) {
    std::vector<HddlUnite::Device> devices;

    HddlStatusCode code = getAvailableDevices(devices);
    std::cout << " [INFO] - Devices found: " << devices.size() << std::endl;

    ASSERT_EQ(code, HddlStatusCode::HDDL_OK);
}

//------------------------------------------------------------------------------
TEST_F(HDDL2_HddlUnite_Tests, CanCreateAndChangeRemoteMemory) {
    auto workloadContext = workloadContextHelper.getWorkloadContext();
    const std::string message = "Hello there\n";

    const size_t size = 100;

    remoteMemoryHelper.allocateRemoteMemory(workloadContext->getWorkloadContextID(), size);
    auto remoteMemoryPtr = remoteMemoryHelper.getRemoteMemoryPtr();

    { remoteMemoryPtr->syncToDevice(message.data(), message.size()); }

    char resultData[size] = {};
    { remoteMemoryPtr->syncFromDevice(resultData, size); }

    const std::string resultMessage(resultData);

    ASSERT_EQ(resultMessage, message);
}

TEST_F(HDDL2_HddlUnite_Tests, WrappedMemoryWillHaveSameData) {
    auto workloadContext = workloadContextHelper.getWorkloadContext();
    const std::string message = "Hello there\n";

    const size_t size = 100;

    remoteMemoryHelper.allocateRemoteMemory(workloadContext->getWorkloadContextID(), size);
    auto remoteMemoryPtr = remoteMemoryHelper.getRemoteMemoryPtr();
    { remoteMemoryPtr->syncToDevice(message.data(), message.size()); }

    // Wrapped memory
    RemoteMemory wrappedRemoteMemory(*workloadContext, remoteMemoryPtr->getMemoryDesc(), remoteMemoryPtr->getDmaBufFd());
    char resultData[size] = {};
    { wrappedRemoteMemory.syncFromDevice(resultData, size); }

    const std::string resultMessage(resultData);

    ASSERT_EQ(resultMessage, message);
}

TEST_F(HDDL2_HddlUnite_Tests, CanSetAndGetRemoteContextUsingId) {
    const auto numDevices = DeviceName::getDevicesNames().size();
    if (numDevices > 1) {
        GTEST_SKIP() << "Skipping single-device test - multiple devices have been detected";
    }

    auto workloadContext = workloadContextHelper.getWorkloadContext();
    WorkloadID workloadId = workloadContext->getWorkloadContextID();

    // Get workload context and check that name the same
    {
        auto workload_context = HddlUnite::queryWorkloadContext(workloadId);
        EXPECT_NE(nullptr, workload_context);

        const std::string deviceName = utils::getDeviceNameBySwDeviceId(workload_context->getDevice()->getSwDeviceId());
        EXPECT_EQ(deviceName, DeviceName::getName());
    }

    // Destroy after finishing working with context
    HddlUnite::unregisterWorkloadContext(workloadId);
}

TEST_F(HDDL2_HddlUnite_Tests, QueryIncorrectWorkloadIdReturnNull) {
    auto workload_context = HddlUnite::queryWorkloadContext(INT32_MAX);
    ASSERT_EQ(workload_context, nullptr);
}

TEST_F(HDDL2_HddlUnite_Tests, CanCreateTwoDifferentContextOneAfterAnother) {
    // Destory default remote context from SetUp
    workloadContextHelper.destroyHddlUniteContext(workloadContextHelper.getWorkloadId());

    WorkloadID firstWorkloadId = WorkloadContext_Helper::createAndRegisterWorkloadContext();
    unregisterWorkloadContext(firstWorkloadId);

    WorkloadID secondWorkloadId = WorkloadContext_Helper::createAndRegisterWorkloadContext();
    unregisterWorkloadContext(secondWorkloadId);
    ASSERT_NE(firstWorkloadId, secondWorkloadId);
}

// TODO FAIL - HUNG - HddlUnite problem
TEST_F(HDDL2_HddlUnite_Tests, DISABLED_CreatingTwoWorkloadContextForSameProcessWillReturnError) {
    // First context
    WorkloadID firstWorkloadId = -1;
    auto firstContext = HddlUnite::createWorkloadContext();

    auto ret = firstContext->setContext(firstWorkloadId);
    EXPECT_EQ(ret, HDDL_OK);
    ret = registerWorkloadContext(firstContext);
    EXPECT_EQ(ret, HDDL_OK);

    // Second context
    WorkloadID secondWorkloadId = -1;
    auto secondContext = HddlUnite::createWorkloadContext();

    ret = secondContext->setContext(secondWorkloadId);
    EXPECT_EQ(ret, HDDL_GENERAL_ERROR);
    ret = registerWorkloadContext(secondContext);
    EXPECT_EQ(ret, HDDL_GENERAL_ERROR);

    EXPECT_NE(firstWorkloadId, secondWorkloadId);

    HddlUnite::unregisterWorkloadContext(firstWorkloadId);
    HddlUnite::unregisterWorkloadContext(secondWorkloadId);
}

//------------------------------------------------------------------------------
class HddlUnite_BlobDescr : public HDDL2_HddlUnite_Tests {
public:
    void SetUp() override;
    void callInferenceOnBlobs();

    const size_t allocationSize = 1024 * 1024 * 4;
    const std::string inputName = "input";
    const std::string outputName = "output";
    const Inference::Precision precision = Inference::Precision::U8;

    // TODO some hardcoded values, get rid of it
    const size_t resNetInputSize = 150528;
    // TODO HACK - Emulator (?) cannot work with not 512 output
    const size_t resNetOutputSize = 512;

    std::string simpleInputData;

    HddlUnite::Inference::InferData::Ptr inferDataPtr = nullptr;
    HddlUnite::RemoteMemory::Ptr remoteMemory = nullptr;

protected:
    std::vector<HddlUnite::Inference::AuxBlob::Type> _auxBlob;

    HddlUnite_Graph_Helper::Ptr _uniteGraphHelper = nullptr;
};

void HddlUnite_BlobDescr::SetUp() {
    auto workloadContext = workloadContextHelper.getWorkloadContext();
    inferDataPtr = HddlUnite::Inference::makeInferData(_auxBlob, workloadContext);

    WorkloadID workloadId = workloadContext->getWorkloadContextID();
    remoteMemoryHelper.allocateRemoteMemory(workloadId, allocationSize);
    remoteMemory = remoteMemoryHelper.getRemoteMemoryPtr();

    _uniteGraphHelper = std::make_shared<HddlUnite_Graph_Helper>(*workloadContext);

    simpleInputData = std::string(resNetInputSize, '*');
}

void HddlUnite_BlobDescr::callInferenceOnBlobs() {
    auto inputBlob = inferDataPtr->getInputBlob(inputName);
    if (inputBlob == nullptr) {
        std::cout << "Input blob not found : creating default\n";
        try {
            const int isInput = true;

            const bool isRemoteMem = false;
            const bool needAllocate = true;

            HddlUnite::Inference::BlobDesc blobDesc(precision, isRemoteMem, needAllocate, resNetInputSize);
            ASSERT_TRUE(inferDataPtr->createBlob(inputName, blobDesc, isInput));

            blobDesc.m_srcPtr = (void*)simpleInputData.data();
            ASSERT_TRUE(inferDataPtr->getInputBlob(inputName)->updateBlob(blobDesc));
        } catch (const std::exception& ex) {
            IE_THROW() << "Failed to create default input blob: " << ex.what();
        }
    }

    auto outputBlob = inferDataPtr->getOutputBlob(outputName);
    if (outputBlob == nullptr) {
        std::cout << "Output blob not found : creating default\n";
        try {
            const int isInput = false;

            const bool isRemoteMem = false;
            const bool needAllocate = true;

            HddlUnite::Inference::BlobDesc blobDesc(precision, isRemoteMem, needAllocate, resNetOutputSize);
            ASSERT_TRUE(inferDataPtr->createBlob(outputName, blobDesc, isInput));
        } catch (const std::exception& ex) {
            IE_THROW() << "Failed to create default output blob: " << ex.what();
        }
    }

    auto graph = _uniteGraphHelper->getGraph();
    HddlStatusCode inferStatus = HddlUnite::Inference::inferSync(*graph, inferDataPtr);
    if (inferStatus != HDDL_OK) {
        IE_THROW() << "Failed to infer";
    }
}

//------------------------------------------------------------------------------
class HddlUnite_BlobDescr_LocalMemory_Input : public HddlUnite_BlobDescr {
public:
    const int isInput = true;
    const bool isRemoteMem = false;
    /**
     * Local memory always should be allocated by hddlunite.
     * We should provide pointer to data, which will be synced to unite buffer (if required)
     */
    const bool needAllocate = true;
};

// [Track number: S#28523]
TEST_F(HddlUnite_BlobDescr_LocalMemory_Input, DISABLED_CanCreateAndInfer_WithSrcPtr) {
    HddlUnite::Inference::BlobDesc blobDesc(precision, isRemoteMem, needAllocate, resNetInputSize);
    ASSERT_TRUE(inferDataPtr->createBlob(inputName, blobDesc, isInput));

    blobDesc.m_srcPtr = (void*)simpleInputData.data();
    blobDesc.m_dataSize = resNetInputSize;

    ASSERT_TRUE(inferDataPtr->getInputBlob(inputName)->updateBlob(blobDesc));
    ASSERT_NO_THROW(callInferenceOnBlobs());
}

//------------------------------------------------------------------------------
class HddlUnite_BlobDescr_RemoteMemory_Input : public HddlUnite_BlobDescr {
public:
    const int isInput = true;
    const bool isRemoteMem = true;

    const bool needAllocate = true;
};

// TODO Add same test with remote memory wrapping (needAllocate = false)
// We want only to allocate remote memory without providing data to it
// [Track number: S#28523]
TEST_F(HddlUnite_BlobDescr_RemoteMemory_Input, DISABLED_CanCreateAndInfer_WithoutSrcPtr) {
    HddlUnite::Inference::BlobDesc blobDesc(precision, isRemoteMem, needAllocate, resNetInputSize);

    ASSERT_TRUE(inferDataPtr->createBlob(inputName, blobDesc, isInput));
    ASSERT_NO_THROW(callInferenceOnBlobs());
}

// If src ptr provided, data will be synced with remote memory
// [Track number: S#28523]
TEST_F(HddlUnite_BlobDescr_RemoteMemory_Input, DISABLED_CanCreateAndInfer_WithSrcPtr) {
    HddlUnite::Inference::BlobDesc blobDesc(precision, isRemoteMem, needAllocate, resNetInputSize);
    blobDesc.m_srcPtr = (void*)simpleInputData.data();

    ASSERT_NO_THROW(inferDataPtr->createBlob(inputName, blobDesc, isInput));
    ASSERT_NO_THROW(callInferenceOnBlobs());
}

//------------------------------------------------------------------------------
class HddlUnite_BlobDescr_LocalMemory_Output : public HddlUnite_BlobDescr {
public:
    const int isInput = false;
    const bool isRemoteMem = false;
    const bool needAllocate = true;
};

/**
 * After inference memory should be copied manually
 */
// [Track number: S#28523]
TEST_F(HddlUnite_BlobDescr_LocalMemory_Output, DISABLED_CanCreateAndInfer_WithoutSrcPtr) {
    HddlUnite::Inference::BlobDesc blobDesc(precision, isRemoteMem, needAllocate, resNetOutputSize);

    ASSERT_TRUE(inferDataPtr->createBlob(outputName, blobDesc, isInput));
    ASSERT_NO_THROW(callInferenceOnBlobs());
}

// [Track number: S#28523]
TEST_F(
    HddlUnite_BlobDescr_LocalMemory_Output, DISABLED_CreatedOutputBlobDesc_WillHaveSaveSizeAsProvided_SizeFromGraph) {
    HddlUnite_Graph_Helper graphHelper;
    const int isInput = false;

    const bool isRemoteMem = false;
    const bool needAllocate = true;

    // Infer will be performed on precompiled ResNet
    // TODO what kind of value is hiding behind getOutputSize()?
    const size_t sizeToAllocate = graphHelper.getGraph()->getOutputSize();

    HddlUnite::Inference::BlobDesc blobDesc(precision, isRemoteMem, needAllocate, sizeToAllocate);

    ASSERT_NO_THROW(inferDataPtr->createBlob(outputName, blobDesc, isInput));
    ASSERT_NO_THROW(callInferenceOnBlobs());

    //TODO send roiIndex (second parameter)
    const auto actualData = inferDataPtr->getOutputData(outputName);
    const size_t actualSize = actualData.size();
    EXPECT_EQ(actualSize, sizeToAllocate);

    const auto actualDescription = inferDataPtr->getOutputBlob(outputName)->getBlobDesc();
    const size_t actualSizeInDesc = actualDescription.m_dataSize;
    EXPECT_EQ(actualSizeInDesc, sizeToAllocate);

    EXPECT_EQ(actualSize, actualSizeInDesc);
}

// TODO FAIL - HDDLUnite problem? - Data size is differ, but it can be problem because result data
//  filled that way (fixed size) in emulator
TEST_F(HddlUnite_BlobDescr_LocalMemory_Output, DISABLED_CreatedOutputBlobDesc_WillHaveSaveSizeAsProvided_CustomSize) {
    HddlUnite_Graph_Helper graphHelper;
    const int isInput = false;

    const bool isRemoteMem = false;
    const bool needAllocate = true;

    const size_t sizeToAllocate = 1000;

    HddlUnite::Inference::BlobDesc blobDesc(precision, isRemoteMem, needAllocate, sizeToAllocate);

    ASSERT_NO_THROW(inferDataPtr->createBlob(outputName, blobDesc, isInput));
    ASSERT_NO_THROW(callInferenceOnBlobs());

    //TODO send roiIndex (second parameter)
    const auto actualData = inferDataPtr->getOutputData(outputName);
    const size_t actualSize = actualData.size();
    EXPECT_EQ(actualSize, sizeToAllocate);

    const auto actualDescription = inferDataPtr->getOutputBlob(outputName)->getBlobDesc();
    const size_t actualSizeInDesc = actualDescription.m_dataSize;
    EXPECT_EQ(actualSizeInDesc, sizeToAllocate);

    EXPECT_EQ(actualSize, actualSizeInDesc);
}

// [Track number: S#28523]
TEST_F(HddlUnite_BlobDescr, DISABLED_CanInferOnDefaultLocalBlobs) {
    // Inference on default blobs, which will
    ASSERT_NO_THROW(callInferenceOnBlobs());
}

//------------------------------------------------------------------------------
using HddlUnite_Stress = HDDL2_HddlUnite_Tests;
// Stress tests should belong to another test executor
TEST_F(HddlUnite_Stress, MultipleAllocations) {
    const size_t amountOfAllocations = 100;
    const Models::ModelDesc modelToUse = Models::squeezenet1_1;
    IE::ExecutableNetwork executableNetwork =
            ExecutableNetworkFactory::createExecutableNetwork(modelToUse.pathToModel);

    auto blobContentStream = ExecutableNetworkFactory::getGraphBlob(modelToUse.pathToModel);
    const std::string graphName = "alloc_check";
    const std::string graphExt = ".blob";
    const std::string graphPath = DUMP_PATH + graphName + graphExt;

    std::ofstream blobFile(graphPath, std::ios::binary);
    blobFile << blobContentStream.rdbuf();
    blobFile.close();

    HddlUnite::Inference::Graph::Ptr graphPtr = nullptr;
    for (size_t i = 0; i < amountOfAllocations; ++i) {
        ASSERT_EQ(HddlUnite::Inference::loadGraph(graphPtr, graphName, graphPath), HDDL_OK);
        EXPECT_EQ(HddlUnite::Inference::unloadGraph(graphPtr), HDDL_OK);
    }
}
