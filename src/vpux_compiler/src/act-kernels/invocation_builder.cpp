//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

//

#include "vpux/compiler/act_kernels/invocation_builder.h"

#include <kernels/inc/common_types.h>

#include <vpux/compiler/dialect/VPUIP/ops.hpp>

#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/IR/Matchers.h>
#include <mlir/IR/Operation.h>

using namespace vpux;

void InvocationBuilder::parseBasicAttrTypes(mlir::Attribute attr) {
    if (auto val = attr.dyn_cast_or_null<mlir::IntegerAttr>()) {
        appendValue(_scalarStorage, val.getValue().getSExtValue());
    } else if (auto val = attr.dyn_cast_or_null<mlir::FloatAttr>()) {
        appendValue(_scalarStorage, static_cast<float>(val.getValue().convertToDouble()));
    } else {
        VPUX_THROW("Act Shave Invocation: cannot store arg of type {0}", attr.getType());
    }
}

void InvocationBuilder::addArg(mlir::Attribute attr) {
    if (auto arr = attr.dyn_cast_or_null<mlir::ArrayAttr>()) {
        auto vals = arr.getValue();
        for (auto val : vals) {
            parseBasicAttrTypes(val);
        }
    } else {
        parseBasicAttrTypes(attr);
    }
}

SmallVector<uint8_t> InvocationBuilder::store() const {
    SmallVector<uint8_t> serialStorage(_scalarStorage.begin(), _scalarStorage.end());
    serialStorage.insert(serialStorage.end(), _arrayStorage.begin(), _arrayStorage.end());

    const auto patchBase = _win_e_offset + _scalarStorage.size() + mvds::nce2p7::ACT_KERNEL_DATA_WINDOW;
    for (auto& patchCallback : _deferredPointers) {
        patchCallback(serialStorage, patchBase);
    }

    return serialStorage;
}

void InvocationBuilder::addTensorArg(mlir::Value value, const MVCNN::TensorReference* tensorRef) {
    VPUX_THROW_UNLESS(tensorRef != nullptr, "Got NULL tensor reference");

    sw_params::MemRefData memrefData{};

    const auto shape = getShape(value);
    memrefData.numDims = checked_cast<uint32_t>(shape.size());

    // order
    const auto inOrder = DimsOrder::fromValue(value);
    const auto memShape = inOrder.toMemoryOrder(shape);
    memrefData.dimsOrder = inOrder.invertedCode();

    // dims
    const auto dimsPatcher = [](sw_params::MemRefData& memrefData, uint32_t updateTo) {
        memrefData.dimsAddr = updateTo;
    };
    _deferredPointers.push_back(createPatchPoint<sw_params::MemRefData>(dimsPatcher));

    for (auto& dim : memShape | reversed) {
        appendValue(_arrayStorage, checked_cast<int32_t>(dim));
    }

    // strides
    auto stridesPatcher = [](sw_params::MemRefData& memrefData, uint32_t updateTo) {
        memrefData.stridesAddr = updateTo;
    };
    _deferredPointers.push_back(createPatchPoint<sw_params::MemRefData>(stridesPatcher));

    const auto memStrides = getMemStrides(value);
    for (auto&& stride : memStrides | reversed) {
        appendValue(_arrayStorage, stride);
    }

    const auto indirectDataRef = tensorRef->data();
    VPUX_THROW_UNLESS(indirectDataRef != nullptr, "Got NULL indirect data reference reference");
    const auto addr = indirectDataRef->data_index() + tensorRef->leading_offset();

    memrefData.dataAddr = checked_cast<uint32_t>(mvds::nce2p7::ACT_KERNEL_CMX_WINDOW + addr);
    memrefData.dataType = 0;  // TODO: to be defined
    memrefData.location = sw_params::NN_CMX;

    appendValue(_scalarStorage, memrefData);
}
