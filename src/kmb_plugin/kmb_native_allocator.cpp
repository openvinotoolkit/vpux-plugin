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

#include "kmb_native_allocator.h"

#include <iostream>
#include <string>
#include <sstream>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace vpu::KmbPlugin;

void *KmbNativeAllocator::alloc(size_t size) noexcept {
    void *virtAddr = nullptr;
    int fileDesc = -1;
    virtAddr = static_cast<unsigned char*>(mmap(nullptr, size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, fileDesc, 0));

    if (virtAddr == MAP_FAILED)
        return nullptr;

    MemoryDescriptor memDesc = {
            size,  // size
            fileDesc,    // file descriptor
            0  // physical address
    };
    _allocatedMemory[virtAddr] = memDesc;

    return virtAddr;
}

bool KmbNativeAllocator::free(void *handle) noexcept {
    auto memoryIt = _allocatedMemory.find(handle);
    if (memoryIt == _allocatedMemory.end()) {
        return false;
    }

    auto memoryDesc = memoryIt->second;

    auto out = munmap(handle, memoryDesc.size);
    if (out == -1) {
        return false;
    }

    _allocatedMemory.erase(handle);

    return true;
}
