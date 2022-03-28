//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

//

#include "vpux/compiler/dialect/IERT/passes.hpp"

#include "vpux/compiler/core/aliases_info.hpp"

using namespace vpux;

namespace {

//
// SetInternalMemorySpacePass
//

class SetInternalMemorySpacePass final : public IERT::SetInternalMemorySpaceBase<SetInternalMemorySpacePass> {
public:
    SetInternalMemorySpacePass(IERT::AttrCreateFunc memSpaceCb, Logger log);

public:
    mlir::LogicalResult initialize(mlir::MLIRContext* ctx) final;

private:
    void safeRunOnFunc() final;

private:
    IERT::AttrCreateFunc _memSpaceCb;
    IndexedSymbolAttr _memSpace;
};

SetInternalMemorySpacePass::SetInternalMemorySpacePass(IERT::AttrCreateFunc memSpaceCb, Logger log)
        : _memSpaceCb(std::move(memSpaceCb)) {
    VPUX_THROW_UNLESS(_memSpaceCb != nullptr, "Missing memSpaceCb");
    Base::initLogger(log, Base::getArgumentName());
}

mlir::LogicalResult SetInternalMemorySpacePass::initialize(mlir::MLIRContext* ctx) {
    if (mlir::failed(Base::initialize(ctx))) {
        return mlir::failure();
    }

    _memSpace = _memSpaceCb(ctx, memSpaceName.getValue());

    if (_memSpace == nullptr) {
        return mlir::failure();
    }

    return mlir::success();
}

void SetInternalMemorySpacePass::safeRunOnFunc() {
    auto& aliasInfo = getAnalysis<AliasesInfo>();

    const auto callback = [&](mlir::memref::AllocOp allocOp) {
        _log.trace("Got Alloc Operation '{0}'", allocOp->getLoc());

        if (allocOp.getType().getMemorySpace() != nullptr) {
            _log.nest().trace("It already has a memory space '{0}'", allocOp.getType().getMemorySpace());
            return;
        }

        const auto& aliases = aliasInfo.getAllAliases(allocOp.memref());

        for (auto var : aliases) {
            _log.nest().trace("Process alias buffer '{0}'", var);

            if (const auto futureType = var.getType().dyn_cast<mlir::async::ValueType>()) {
                const auto origType = futureType.getValueType().dyn_cast<vpux::NDTypeInterface>();
                VPUX_THROW_UNLESS(origType != nullptr, "Got non vpux::NDTypeInterface Type '{0}'", var.getType());

                const auto newType = origType.changeMemSpace(_memSpace);
                const auto newFutureType = mlir::async::ValueType::get(newType);

                var.setType(newFutureType);
            } else {
                const auto origType = var.getType().dyn_cast<vpux::NDTypeInterface>();
                VPUX_THROW_UNLESS(origType != nullptr, "Got non vpux::NDTypeInterface Type '{0}'", var.getType());

                const auto newType = origType.changeMemSpace(_memSpace);
                var.setType(newType);
            }
        }
    };

    getFunction().walk(callback);
}

}  // namespace

std::unique_ptr<mlir::Pass> vpux::IERT::createSetInternalMemorySpacePass(AttrCreateFunc memSpaceCb, Logger log) {
    return std::make_unique<SetInternalMemorySpacePass>(std::move(memSpaceCb), log);
}
