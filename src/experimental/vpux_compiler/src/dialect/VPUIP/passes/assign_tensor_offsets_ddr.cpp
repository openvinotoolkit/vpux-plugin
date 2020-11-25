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

#include "vpux/compiler/dialect/VPUIP/passes.hpp"

#include "vpux/compiler/allocator/linear_scan.hpp"
#include "vpux/compiler/core/strides.hpp"
#include "vpux/compiler/dialect/VPUIP/ops.hpp"
#include "vpux/compiler/utils/scalars.hpp"

#include "vpux/utils/core/error.hpp"
#include "vpux/utils/core/format.hpp"
#include "vpux/utils/core/numeric.hpp"

#include <mlir/Analysis/BufferAliasAnalysis.h>
#include <mlir/Analysis/Liveness.h>
#include <mlir/IR/BuiltinOps.h>

using namespace vpux;

namespace {

class AssignTensorOffsetsDDRPass final : public VPUIP::AssignTensorOffsetsDDRBase<AssignTensorOffsetsDDRPass> {
public:
    explicit AssignTensorOffsetsDDRPass(Logger log): _log(log) {
        _log.setName(Base::getArgumentName());
    }

public:
    void runOnOperation() final;

private:
    struct Handler final {
        mlir::DenseSet<mlir::Value> aliveValues;
        AddressType maxAllocatedSize = 0;

        bool isAlive(mlir::Value val) const;
        bool isFixedAlloc(mlir::Value val) const;
        AddressType getSize(mlir::Value val) const;
        AddressType getAlignment(mlir::Value val) const;
        AddressType getAddress(mlir::Value val) const;
        void allocated(mlir::Value val, AddressType addr);
        void freed(mlir::Value val) const;
        int getSpillWeight(mlir::Value val) const;
        bool spilled(mlir::Value val) const;
    };

private:
    void passBody();

private:
    Logger _log;
};

bool AssignTensorOffsetsDDRPass::Handler::isAlive(mlir::Value val) const {
    return aliveValues.contains(val);
}

bool AssignTensorOffsetsDDRPass::Handler::isFixedAlloc(mlir::Value) const {
    return false;
}

AddressType AssignTensorOffsetsDDRPass::Handler::getSize(mlir::Value val) const {
    return getTypeByteSize(val.getType().cast<mlir::MemRefType>());
}

AddressType AssignTensorOffsetsDDRPass::Handler::getAlignment(mlir::Value) const {
    return 64;
}

AddressType AssignTensorOffsetsDDRPass::Handler::getAddress(mlir::Value val) const {
    auto producerOp = mlir::dyn_cast<VPUIP::DeclareTensorOp>(val.getDefiningOp());

    VPUX_THROW_UNLESS(producerOp != nullptr, "Can allocate only DeclareTensorOp results");
    VPUX_THROW_UNLESS(getPhysicalMemory(producerOp.locale()) == VPUIP::PhysicalMemory::DDR,
                      "Can allocate only DDR memory");
    VPUX_THROW_UNLESS(producerOp.dataIndex().hasValue(), "DeclareTensorOp offset was not set");

    return producerOp.dataIndex().getValue();
}

void AssignTensorOffsetsDDRPass::Handler::allocated(mlir::Value val, AddressType addr) {
    VPUX_THROW_UNLESS(addr != InvalidAddress, "Trying to assign invalid address");

    auto producerOp = mlir::dyn_cast<VPUIP::DeclareTensorOp>(val.getDefiningOp());

    VPUX_THROW_UNLESS(producerOp != nullptr, "Can allocate only DeclareTensorOp results");
    VPUX_THROW_UNLESS(getPhysicalMemory(producerOp.locale()) == VPUIP::PhysicalMemory::DDR,
                      "Can allocate only DDR memory");
    VPUX_THROW_UNLESS(!producerOp.dataIndex().hasValue(), "DeclareTensorOp offset was already set");

    producerOp.dataIndexAttr(getInt64Attr(val.getContext(), addr));

    maxAllocatedSize = std::max(maxAllocatedSize, alignVal<AddressType>(addr + getSize(val), 64));
}

void AssignTensorOffsetsDDRPass::Handler::freed(mlir::Value) const {
}

int AssignTensorOffsetsDDRPass::Handler::getSpillWeight(mlir::Value) const {
    VPUX_THROW("Spills is not allowed for DDR");
}

bool AssignTensorOffsetsDDRPass::Handler::spilled(mlir::Value) const {
    VPUX_THROW("Spills is not allowed for DDR");
}

void AssignTensorOffsetsDDRPass::runOnOperation() {
    try {
        passBody();
    } catch (const std::exception& e) {
        printTo(getOperation().emitError(), "AssignTensorOffsetsDDRPass failed : {0}", e.what());
        signalPassFailure();
    }
}

void AssignTensorOffsetsDDRPass::passBody() {
    auto module = getOperation();

    VPUIP::GraphOp graphOp;
    mlir::FuncOp graphFunc;
    if (mlir::failed(VPUIP::GraphOp::getFromModule(module, graphOp, graphFunc))) {
        signalPassFailure();
        return;
    }

    auto& liveness = getAnalysis<mlir::Liveness>();
    auto& aliases = getAnalysis<mlir::BufferAliasAnalysis>();

    mlir::BufferAliasAnalysis::ValueMapT allocatedVals;

    LinearScan<mlir::Value, Handler> scan(std::numeric_limits<uint32_t>::max());

    auto callback = [&](mlir::Operation* op) -> mlir::WalkResult {
        if (auto allocOp = mlir::dyn_cast<VPUIP::DeclareTensorOp>(op)) {
            if (getPhysicalMemory(allocOp.locale()) == VPUIP::PhysicalMemory::DDR) {
                auto memVal = allocOp.memory();

                if (!scan.alloc({memVal}, /*allowSpills*/ false)) {
                    printTo(op->emitError(), "Failed to allocate DDR memory");
                    signalPassFailure();
                    return mlir::failure();
                }

                scan.handler().aliveValues.insert(memVal);

                allocatedVals.insert({memVal, aliases.resolve(memVal)});
            }
        } else {
            for (auto input : op->getOperands()) {
                const auto valAliasesIt = allocatedVals.find(input);
                if (valAliasesIt == allocatedVals.end()) {
                    continue;
                }

                bool isLastUse = true;
                for (auto valAlias : valAliasesIt->second) {
                    if (!liveness.isLastUse(valAlias, op)) {
                        isLastUse = false;
                        break;
                    }
                }

                if (isLastUse) {
                    scan.handler().aliveValues.erase(input);
                }
            }

            scan.freeNonAlive();
        }

        return mlir::success();
    };

    graphFunc.walk(std::move(callback));

    auto oldResources = graphOp.resourcesAttr();

    auto oldMemSizes = oldResources.memory_sizes();
    SmallVector<mlir::Attribute, 2> newMemSizes;

    for (auto memMap : oldMemSizes.getAsRange<VPUIP::MemoryMappingAttr>()) {
        if (memMap.item().getValue() != VPUIP::PhysicalMemory::DDR) {
            newMemSizes.push_back(memMap);
        }
    }

    newMemSizes.push_back(VPUIP::MemoryMappingAttr::get(
            VPUIP::PhysicalMemoryAttr::get(VPUIP::PhysicalMemory::DDR, module.getContext()),
            getInt64Attr(module.getContext(), scan.handler().maxAllocatedSize), module.getContext()));

    auto newResources =
            VPUIP::ResourcesAttr::get(oldResources.processor_allocation(), oldResources.processor_frequencies(),
                                      mlir::ArrayAttr::get(newMemSizes, module.getContext()),
                                      oldResources.memory_bandwidth(), module.getContext());

    graphOp.resourcesAttr(newResources);
}

}  // namespace

std::unique_ptr<mlir::Pass> vpux::VPUIP::createAssignTensorOffsetsDDRPass(Logger log) {
    return std::make_unique<AssignTensorOffsetsDDRPass>(log);
}
