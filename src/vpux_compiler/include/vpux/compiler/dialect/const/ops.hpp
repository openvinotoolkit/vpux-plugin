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

#include "vpux/compiler/core/ops_interfaces.hpp"
#include "vpux/compiler/dialect/const/attributes/content.hpp"
#include "vpux/compiler/dialect/const/ops_interfaces.hpp"  // 2021_10_22

#include "vpux/utils/core/logger.hpp"

#include <mlir/IR/Dialect.h>
#include <mlir/Interfaces/SideEffectInterfaces.h>
#include <mlir/Transforms/DialectConversion.h>

//
// Generated
//

#include <vpux/compiler/dialect/const/generated/dialect.hpp.inc>

#define GET_OP_CLASSES
#include <vpux/compiler/dialect/const/generated/ops.hpp.inc>
