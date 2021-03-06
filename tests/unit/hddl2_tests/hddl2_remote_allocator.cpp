//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

//

#include <gtest/gtest.h>
#include <hddl2_helpers/helper_workload_context.h>
#include <hddl2_remote_allocator.h>
#include <vpux_plugin.h>

#include <climits>

#include "hddl2_helpers/helper_remote_memory.h"
#include "helpers/helper_remote_allocator.h"
#include "skip_conditions.h"

using namespace vpux::hddl2;
using namespace InferenceEngine;

//------------------------------------------------------------------------------
class RemoteAllocator_UnitTests : public ::testing::Test {
public:
    void SetUp() override;

    HddlUnite::WorkloadContext::Ptr workloadContextPtr = nullptr;
    const size_t correctSize = 1024 * 1024 * 1;

protected:
    WorkloadContext_Helper::Ptr _workloadContextHelperPtr = nullptr;
};

void RemoteAllocator_UnitTests::SetUp() {
    if (canWorkWithDevice()) {
        _workloadContextHelperPtr = std::make_shared<WorkloadContext_Helper>();
        workloadContextPtr = _workloadContextHelperPtr->getWorkloadContext();
    }
}

//------------------------------------------------------------------------------
TEST_F(RemoteAllocator_UnitTests, constructor_CorrectContext_NoThrow) {
    SKIP_IF_NO_DEVICE();
    ASSERT_NO_THROW(HDDL2RemoteAllocator::Ptr allocator = std::make_shared<HDDL2RemoteAllocator>(workloadContextPtr));
}

TEST_F(RemoteAllocator_UnitTests, constructor_NullContext_Throw) {
    SKIP_IF_NO_DEVICE();
    ASSERT_ANY_THROW(HDDL2RemoteAllocator::Ptr allocator = std::make_shared<HDDL2RemoteAllocator>(nullptr));
}

using RemoteAllocator_WrapMemory = RemoteAllocator_UnitTests;

TEST_F(RemoteAllocator_WrapMemory, IncorrectRemoteMemType_ReturnNull) {
    SKIP_IF_NO_DEVICE();
    auto allocatorPtr = std::make_shared<HDDL2RemoteAllocator>(workloadContextPtr);
    IE::ParamMap paramMap = {{IE::VPUX_PARAM_KEY(REMOTE_MEMORY_FD), nullptr}};
    const auto memoryHandle = allocatorPtr->wrapRemoteMemory(paramMap);
    EXPECT_EQ(memoryHandle, nullptr);
}

//------------------------------------------------------------------------------
enum RemoteMemoryOwner { IERemoteMemoryOwner = 0, ExternalRemoteMemoryOwner = 1 };

//------------------------------------------------------------------------------
class Allocator_Manipulations_UnitTests :
        public RemoteAllocator_UnitTests,
        public ::testing::WithParamInterface<RemoteMemoryOwner> {
public:
    void SetUp() override;
    Allocator_Helper::Ptr allocatorHelper = nullptr;

    struct PrintToStringParamName {
        std::string operator()(testing::TestParamInfo<RemoteMemoryOwner> const& info) const;
    };
};

//------------------------------------------------------------------------------
void Allocator_Manipulations_UnitTests::SetUp() {
    if (canWorkWithDevice()) {
        RemoteAllocator_UnitTests::SetUp();

        auto owner = GetParam();
        if (owner == IERemoteMemoryOwner) {
            allocatorHelper = std::make_shared<Allocator_CreatedRemoteMemory_Helper>(workloadContextPtr);
        } else {
            allocatorHelper = std::make_shared<Allocator_WrappedRemoteMemory_Helper>(workloadContextPtr);
        }
    }
}

std::string Allocator_Manipulations_UnitTests::PrintToStringParamName::operator()(
        const testing::TestParamInfo<RemoteMemoryOwner>& info) const {
    RemoteMemoryOwner memoryOwner = info.param;
    if (memoryOwner == IERemoteMemoryOwner) {
        return "IERemoteMemoryOwner";
    } else if (memoryOwner == ExternalRemoteMemoryOwner) {
        return "ExternalRemoteMemoryOwner";
    } else {
        return "Unknown params";
    }
}

//------------------------------------------------------------------------------
TEST_P(Allocator_Manipulations_UnitTests, DISABLED_canLockAndUnlockMemory) {
    SKIP_IF_NO_DEVICE();
    auto memoryHandle = allocatorHelper->createMemory(correctSize);
    auto allocator = allocatorHelper->allocatorPtr;

    auto locked_data = allocator->lock(memoryHandle);

    ASSERT_NE(locked_data, nullptr);
    std::memset(locked_data, 0xFF, correctSize);

    allocator->unlock(memoryHandle);
}

TEST_P(Allocator_Manipulations_UnitTests, DISABLED_lock_DoubleLock_SecondReturnNull) {
    SKIP_IF_NO_DEVICE();
    auto memoryHandle = allocatorHelper->createMemory(correctSize);
    auto allocator = allocatorHelper->allocatorPtr;

    auto first_locked_data = allocator->lock(memoryHandle);
    ASSERT_NE(first_locked_data, nullptr);

    auto second_locked_data = allocator->lock(memoryHandle);
    ASSERT_EQ(second_locked_data, nullptr);

    allocator->unlock(memoryHandle);
}

TEST_P(Allocator_Manipulations_UnitTests, DISABLED_unlock_MemoryChanged_RemoteMemoryWillChange) {
    SKIP_IF_NO_DEVICE();
    auto owner = GetParam();
    if (owner == IERemoteMemoryOwner) {
        GTEST_SKIP() << "If Inference Engine own remote memory, we can't get remote memory";
    }

    auto memoryHandle = allocatorHelper->createMemory(correctSize);
    auto allocator = allocatorHelper->allocatorPtr;

    auto mem = static_cast<char*>(allocator->lock(memoryHandle));
    const char testValue = 'V';
    mem[0] = testValue;
    allocator->unlock(memoryHandle);

    const std::string remoteMemory = allocatorHelper->getRemoteMemory(sizeof(char));
    const char remoteMemoryValue = remoteMemory[0];

    ASSERT_EQ(remoteMemoryValue, testValue);
}

TEST_P(Allocator_Manipulations_UnitTests, DISABLED_lock_BeforeUnlock_RemoteMemoryNotChange) {
    SKIP_IF_NO_DEVICE();
    auto owner = GetParam();
    if (owner == IERemoteMemoryOwner) {
        GTEST_SKIP() << "If Inference Engine own remote memory, we can't get remote memory";
    }

    auto memoryHandle = allocatorHelper->createMemory(correctSize);
    auto allocator = allocatorHelper->allocatorPtr;

    auto mem = static_cast<char*>(allocator->lock(memoryHandle));
    const char testValue = 'V';
    mem[0] = testValue;

    const std::string remoteMemory = allocatorHelper->getRemoteMemory(sizeof(char));
    const char remoteMemoryValue = remoteMemory[0];
    ASSERT_NE(remoteMemoryValue, testValue);

    allocator->unlock(memoryHandle);
}

//------------------------------------------------------------------------------
TEST_P(Allocator_Manipulations_UnitTests, DISABLED_free_CorrectAddressMemory_ReturnTrue) {
    SKIP_IF_NO_DEVICE();
    auto memoryHandle = allocatorHelper->createMemory(correctSize);
    auto allocator = allocatorHelper->allocatorPtr;

    ASSERT_TRUE(allocator->free(memoryHandle));
}

TEST_P(Allocator_Manipulations_UnitTests, DISABLED_free_InvalidAddressMemory_ReturnFalse) {
    SKIP_IF_NO_DEVICE();
    auto allocator = allocatorHelper->allocatorPtr;
    void* invalidHandle = nullptr;

    ASSERT_FALSE(allocator->free(invalidHandle));
}

TEST_P(Allocator_Manipulations_UnitTests, DISABLED_free_DoubleCall_ReturnFalseOnSecond) {
    SKIP_IF_NO_DEVICE();
    auto memoryHandle = allocatorHelper->createMemory(correctSize);
    auto allocator = allocatorHelper->allocatorPtr;

    ASSERT_TRUE(allocator->free(memoryHandle));
    ASSERT_FALSE(allocator->free(memoryHandle));
}

TEST_P(Allocator_Manipulations_UnitTests, DISABLED_free_LockedMemory_ReturnFalse) {
    SKIP_IF_NO_DEVICE();
    auto memoryHandle = allocatorHelper->createMemory(correctSize);
    auto allocator = allocatorHelper->allocatorPtr;

    allocator->lock(memoryHandle);

    ASSERT_FALSE(allocator->free(memoryHandle));
}

//------------------------------------------------------------------------------
TEST_P(Allocator_Manipulations_UnitTests, DISABLED_ChangeLocalMemory_RemoteDoesNotChanged) {
    SKIP_IF_NO_DEVICE();
    auto allocator = allocatorHelper->allocatorPtr;

    const float testValue = 42.;
    const int size = 100 * sizeof(float);

    auto memoryHandle = allocatorHelper->createMemory(size);
    auto mem = static_cast<float*>(allocator->lock(memoryHandle));

    mem[0] = testValue;

    // Sync memory to device
    allocator->unlock(memoryHandle);

    // Change local memory
    mem[0] = -1;

    // Sync memory from device
    mem = static_cast<float*>(allocator->lock(memoryHandle));

    // Check that same as set
    ASSERT_EQ(testValue, mem[0]);
}

TEST_P(Allocator_Manipulations_UnitTests, DISABLED_ChangeLockedForReadMemory_RemoteDoesNotChanged) {
    SKIP_IF_NO_DEVICE();
    auto allocator = allocatorHelper->allocatorPtr;

    const float testValue = 42.;
    const int size = 100 * sizeof(float);

    auto memoryHandle = allocatorHelper->createMemory(size);

    // Set memory on device side
    auto mem = static_cast<float*>(allocator->lock(memoryHandle));
    mem[0] = testValue;
    allocator->unlock(memoryHandle);

    // Get memory for read and change it
    mem = static_cast<float*>(allocator->lock(memoryHandle, InferenceEngine::LOCK_FOR_READ));
    mem[0] = -1;
    allocator->unlock(memoryHandle);

    // Sync memory from device
    mem = static_cast<float*>(allocator->lock(memoryHandle));

    // Check that same as set
    ASSERT_EQ(testValue, mem[0]);
}

//------------------------------------------------------------------------------
// TODO IERemoteMemoryOwner should not be allowed. Remove tests with it
const static std::vector<RemoteMemoryOwner> memoryOwners = {ExternalRemoteMemoryOwner};

INSTANTIATE_TEST_SUITE_P(RemoteMemoryOwner, Allocator_Manipulations_UnitTests, ::testing::ValuesIn(memoryOwners),
                         Allocator_Manipulations_UnitTests::PrintToStringParamName());
