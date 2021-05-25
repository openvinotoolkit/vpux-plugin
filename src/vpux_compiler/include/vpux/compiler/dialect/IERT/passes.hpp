//
// Copyright Intel Corporation.
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

#include "vpux/compiler/dialect/IERT/ops.hpp"
#include "vpux/compiler/utils/passes.hpp"

#include "vpux/utils/core/logger.hpp"

#include <mlir/Dialect/Linalg/IR/LinalgOps.h>
#include <mlir/Dialect/MemRef/IR/MemRef.h>
#include <mlir/Dialect/StandardOps/IR/Ops.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/Pass/Pass.h>

#include <functional>
#include <memory>

namespace vpux {
namespace IERT {

//
// Passes
//

using AttrCreateFunc = std::function<mlir::Attribute(mlir::MLIRContext*, StringRef)>;

std::unique_ptr<mlir::Pass> createAdjustLayoutsPass(Logger log = Logger::global());
std::unique_ptr<mlir::Pass> createUseUserLayout(Logger log = Logger::global());
std::unique_ptr<mlir::Pass> createSetInternalMemorySpacePass(AttrCreateFunc memSpaceCb, Logger log = Logger::global());
std::unique_ptr<mlir::Pass> createStaticAllocationPass(AttrCreateFunc memSpaceCb, Logger log = Logger::global());
std::unique_ptr<mlir::Pass> createCMXTilingPass(Logger log = Logger::global());

//
// Generated
//

#define GEN_PASS_CLASSES
#include <vpux/compiler/dialect/IERT/generated/passes.hpp.inc>
#undef GEN_PASS_CLASSES

#define GEN_PASS_REGISTRATION
#include <vpux/compiler/dialect/IERT/generated/passes.hpp.inc>
#undef GEN_PASS_REGISTRATION

}  // namespace IERT
}  // namespace vpux
