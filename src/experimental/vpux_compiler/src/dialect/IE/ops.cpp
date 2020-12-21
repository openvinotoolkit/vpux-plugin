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

#include "vpux/compiler/dialect/IE/ops.hpp"

#include "vpux/compiler/core/attributes/dims_order.hpp"

#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/IR/OpImplementation.h>

using namespace vpux;

namespace {

class IEDialectAsmHooks final : public mlir::OpAsmDialectInterface {
public:
    using mlir::OpAsmDialectInterface::OpAsmDialectInterface;

public:
    void getAsmResultNames(mlir::Operation* op, mlir::OpAsmSetValueNameFn setNameFn) const final;

    mlir::LogicalResult getAlias(mlir::Attribute attr, llvm::raw_ostream& os) const final;
};

void IEDialectAsmHooks::getAsmResultNames(mlir::Operation* op, mlir::OpAsmSetValueNameFn setNameFn) const {
    if (const auto nameLoc = op->getLoc().dyn_cast<mlir::NameLoc>()) {
        setNameFn(op->getResult(0), nameLoc.getName());
    }
}

mlir::LogicalResult IEDialectAsmHooks::getAlias(mlir::Attribute attr, llvm::raw_ostream& os) const {
    if (const auto affineMapAttr = attr.dyn_cast<mlir::AffineMapAttr>()) {
        if (const auto dimsOrder = DimsOrder::fromAffineMap(affineMapAttr.getValue())) {
            if (const auto name = dimsOrder->getCanonicalName()) {
                os << name.getValue();
                return mlir::success();
            }
        }
    }

    return mlir::failure();
}

}  // namespace

void vpux::IE::IEDialect::initialize() {
    addOperations<
#define GET_OP_LIST
#include <vpux/compiler/dialect/IE/generated/ops.cpp.inc>
#undef GET_OP_LIST
            >();

    addInterfaces<IEDialectAsmHooks>();
}

mlir::Operation* vpux::IE::IEDialect::materializeConstant(mlir::OpBuilder& builder, mlir::Attribute value,
                                                          mlir::Type type, mlir::Location loc) {
    if (!value.isa<mlir::DenseElementsAttr>()) {
        printTo(mlir::emitError(loc), "Can't materialize IE Constant from Attribute '{0}'", value);
        return nullptr;
    }

    if (!type.isa<mlir::RankedTensorType>()) {
        printTo(mlir::emitError(loc), "Can't materialize IE Constant for Type '{0}'", type);
        return nullptr;
    }

    return builder.create<mlir::ConstantOp>(loc, type, value);
}

//
// Generated
//

#define GET_OP_CLASSES
#include <vpux/compiler/dialect/IE/generated/ops.cpp.inc>
#undef GET_OP_CLASSES
