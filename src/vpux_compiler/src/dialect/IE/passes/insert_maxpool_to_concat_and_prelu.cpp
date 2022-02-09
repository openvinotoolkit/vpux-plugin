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

#include "vpux/compiler/dialect/IE/passes.hpp"

#include "vpux/compiler/utils/rewriter.hpp"

#include <mlir/Transforms/GreedyPatternRewriteDriver.h>

using namespace vpux;

namespace {

//
// GenericConverter
//

class GenericConverter final : public mlir::OpRewritePattern<IE::LeakyReluOp> {
public:
    GenericConverter(mlir::MLIRContext* ctx, Logger log): mlir::OpRewritePattern<IE::LeakyReluOp>(ctx), _log(log) {
        this->setDebugName("InsertMaxpoolToConcatLRelu::GenericConverter");
    }

private:
    mlir::LogicalResult matchAndRewrite(IE::LeakyReluOp leakyReluOp, mlir::PatternRewriter& rewriter) const final;

private:
    Logger _log;
};

mlir::LogicalResult GenericConverter::matchAndRewrite(IE::LeakyReluOp leakyReluOp,
                                                      mlir::PatternRewriter& rewriter) const {
    auto concatOp = leakyReluOp.getOperand().getDefiningOp<IE::ConcatOp>();
    if (concatOp == nullptr) {
        return mlir::failure();
    }

    const SmallVector<int64_t> maxPoolStrides = {1, 1};
    const SmallVector<int64_t> maxPoolKernels = {1, 1};
    const SmallVector<int64_t> pads = {0, 0};
    auto ctx = leakyReluOp.getContext();
    const auto padsAttr = getIntArrayAttr(ctx, pads);

    auto maxPoolOp = rewriter.create<IE::MaxPoolOp>(
            leakyReluOp.getLoc(), leakyReluOp.getOperand(), getIntArrayAttr(ctx, maxPoolKernels),
            getIntArrayAttr(ctx, maxPoolStrides), padsAttr, padsAttr,
            vpux::IE::RoundingTypeAttr::get(ctx, vpux::IE::RoundingType::FLOOR), nullptr);

    rewriter.replaceOpWithNewOp<IE::LeakyReluOp>(leakyReluOp, maxPoolOp.output(), leakyReluOp.negative_slopeAttr());

    return mlir::success();
}

//
// InsertMaxpoolToConcatLReluPass
//

class InsertMaxpoolToConcatLReluPass final : public IE::InsertMaxpoolToConcatLReluBase<InsertMaxpoolToConcatLReluPass> {
public:
    explicit InsertMaxpoolToConcatLReluPass(Logger log): _log(log) {
        _log.setName(Base::getArgumentName());
    }

private:
    void safeRunOnFunc() final;

private:
    Logger _log;
};

void InsertMaxpoolToConcatLReluPass::safeRunOnFunc() {
    auto& ctx = getContext();

    mlir::OwningRewritePatternList patterns(&ctx);
    patterns.add<GenericConverter>(&ctx, _log);

    auto func = getFunction();
    if (mlir::failed(applyPatternsAndFoldGreedily(func, std::move(patterns), getDefaultGreedyRewriteConfig()))) {
        signalPassFailure();
    }
}

}  // namespace

std::unique_ptr<mlir::Pass> vpux::IE::createInsertMaxpoolToConcatLReluPass(Logger log) {
    return std::make_unique<InsertMaxpoolToConcatLReluPass>(log);
}
