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

#include <vpusmm.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>


#include "kmb_allocator.h"

#include <iostream>

using namespace vpu::KmbPlugin;

void *KmbAllocator::lock(void *handle, InferenceEngine::LockOp) noexcept {
    if (_allocatedMemory.find(handle) == _allocatedMemory.end())
        return nullptr;

    return handle;
}

void KmbAllocator::unlock(void *handle) noexcept {
    UNUSED(handle);
}

unsigned long KmbAllocator::getPhysicalAddress(void *handle) noexcept {
    auto memoryIt = _allocatedMemory.find(handle);
    if (memoryIt == _allocatedMemory.end()) {
        return 0;
    }

    auto memoryDesc = memoryIt->second;
    return memoryDesc.physAddr;
}
