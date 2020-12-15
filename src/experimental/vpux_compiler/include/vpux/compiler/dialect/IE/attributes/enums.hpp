//
// Copyright 2020 Intel Corporation.
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

#include "vpux/compiler/core/attributes/dims_order.hpp"

#include "vpux/utils/core/optional.hpp"
#include "vpux/utils/core/string_ref.hpp"

#include <mlir/IR/AffineMap.h>
#include <mlir/IR/BuiltinAttributes.h>

//
// Generated
//

#include <vpux/compiler/dialect/IE/generated/attributes/enums.hpp.inc>

//
// Layout utilities
//

namespace vpux {
namespace IE {

int32_t getRank(Layout layout);

DimsOrder getDimsOrder(Layout layout);

mlir::AffineMap getAffineMap(mlir::MLIRContext* ctx, Layout layout);

}  // namespace IE
}  // namespace vpux
