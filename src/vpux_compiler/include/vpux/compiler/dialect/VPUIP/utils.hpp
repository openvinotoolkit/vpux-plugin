//
// Copyright Intel Corporation.
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

#include <mlir/IR/Builders.h>
#include <mlir/IR/Location.h>
#include "vpux/compiler/core/attributes/dims_order.hpp"
namespace vpux {
namespace VPUIP {

mlir::Value alignDepthWiseWeightsTensor(mlir::OpBuilder& builder, mlir::Location loc, const mlir::Value origFilter);
mlir::Value alignChannelMajorWeightsTensor(mlir::OpBuilder& builder, mlir::Location loc, const mlir::Value origFilter);
bool isChannelMajorCompatibleOperation(vpux::DimsOrder inDimsOrder, int64_t inputChannels, int64_t inputTensorWidth);

}  // namespace VPUIP
}  // namespace vpux
