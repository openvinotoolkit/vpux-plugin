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
#include "vpux/compiler/core/token_barrier_scheduler.hpp"
//#include "vpux/compiler/dialect/VPUIP/attributes/arch.hpp"
#include "vpux/compiler/dialect/VPURT/passes.hpp"

#include <mlir/Transforms/DialectConversion.h>

#include <llvm/ADT/DenseMap.h>

using namespace vpux;

namespace {

// Same value for all architectures for now
constexpr int64_t MAX_BARRIERS_FOR_ARCH = 64;

//
// BarrierAllocation
//

class BarrierAllocation final {
public:
    BarrierAllocation(mlir::Operation* op, int64_t numBarriers, Logger log);

    // int64_t getID(mlir::Value val) const;

private:
    llvm::DenseMap<mlir::Value, int64_t> _idMap;
};

// TODO: [#6150] Implement safe static barriers allocation
// BarrierAllocation::BarrierAllocation(mlir::Operation* op, int64_t numBarriers, Logger log) {
//     int64_t barrierID = 0;

//     log.trace("Assign {0} physical barriers", numBarriers);

//     const auto callback = [&](VPURT::DeclareVirtualBarrierOp virtOp) {
//         _idMap.insert({virtOp.barrier(), barrierID});
//         barrierID = (barrierID + 1) % numBarriers;
//     };

//     op->walk(callback);
// }

// int64_t BarrierAllocation::getID(mlir::Value val) const {
//     const auto it = _idMap.find(val);
//     VPUX_THROW_UNLESS(it != _idMap.end(), "Value '{0}' was not covered by BarrierAllocation");
//     return it->second;
// }

//
// VirtualBarrierRewrite
//

class VirtualBarrierRewrite final : public mlir::OpRewritePattern<VPURT::DeclareVirtualBarrierOp> {
public:
    VirtualBarrierRewrite(mlir::MLIRContext* ctx, const BarrierAllocation& allocInfo, Logger log)
            : mlir::OpRewritePattern<VPURT::DeclareVirtualBarrierOp>(ctx), _allocInfo(allocInfo), _log(log) {
    }

public:
    mlir::LogicalResult matchAndRewrite(VPURT::DeclareVirtualBarrierOp origOp,
                                        mlir::PatternRewriter& rewriter) const final;

private:
    const BarrierAllocation& _allocInfo;
    Logger _log;
};

// mlir::LogicalResult VirtualBarrierRewrite::matchAndRewrite(VPURT::DeclareVirtualBarrierOp origOp,
//                                                            mlir::PatternRewriter& rewriter) const {
//     _log.trace("Found DeclareVirtualBarrierOp Operation '{0}'", origOp->getLoc());

//     const auto barrierID = _allocInfo.getID(origOp.barrier());
//     _log.nest().trace("Use physical barrier ID '{0}'", barrierID);

//     rewriter.replaceOpWithNewOp<VPURT::ConfigureBarrierOp>(origOp, barrierID);
//     return mlir::success();
// }

//
// AssignVirtualBarriersPass
//

class AssignVirtualBarriersPass final : public VPURT::AssignVirtualBarriersBase<AssignVirtualBarriersPass> {
public:
    explicit AssignVirtualBarriersPass(Logger log) {
        Base::initLogger(log, Base::getArgumentName());
    }

private:
    void safeRunOnFunc() final;
};

void AssignVirtualBarriersPass::safeRunOnFunc() {
    auto& ctx = getContext();
    auto func = getFunction();

    // auto module = func->getParentOfType<mlir::ModuleOp>();
    // auto resOp = IERT::RunTimeResourcesOp::getFromModule(module);

    // Barrier scheduler
    TokenBasedBarrierScheduler barrierScheduler(&ctx, func, 4, 256);
    barrierScheduler.schedule();
}

}  // namespace

//
// createAssignVirtualBarriersPass
//

std::unique_ptr<mlir::Pass> vpux::VPURT::createAssignVirtualBarriersPass(Logger log) {
    return std::make_unique<AssignVirtualBarriersPass>(log);
}
