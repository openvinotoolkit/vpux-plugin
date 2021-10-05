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
#include "vpux/compiler/dialect/VPUIP/nce_invariant.hpp"
#include "vpux/compiler/utils/attributes.hpp"
#include "vpux/compiler/utils/quantization.hpp"
#include "vpux/compiler/utils/rewriter.hpp"
#include "vpux/compiler/utils/types.hpp"

#include <mlir/IR/PatternMatch.h>
#include <mlir/Transforms/DialectConversion.h>

#include <functional>
#include <numeric>

using namespace vpux;

namespace {

//
// Optimizer
//

class ChannelMajorConvolutionCompatibleOps final {
public:
    explicit ChannelMajorConvolutionCompatibleOps(mlir::MLIRContext* ctx, Logger log, vpux::DimsOrder userDimsOrder)
            : _ctx(ctx), _log(log), _userDimsOrder(userDimsOrder) {
    }

    mlir::BoolAttr isOpChannelMajorCompatible(IE::ConvolutionOp convOp) const;

private:
    mlir::MLIRContext* _ctx;
    Logger _log;
    vpux::DimsOrder _userDimsOrder;
};

mlir::BoolAttr ChannelMajorConvolutionCompatibleOps::isOpChannelMajorCompatible(IE::ConvolutionOp convOp) const {
    const auto inputShape = getShape(convOp.filter().getType().cast<mlir::ShapedType>());
    const auto IC = inputShape[IE::Dims4D::Filter::IC];

    auto inputTensorShape = getShape(convOp.input());
    auto width = inputTensorShape[IE::Dims4D::Act::W];

    if ((IC == 3) && (width % 16 == 0) && _userDimsOrder == DimsOrder::NCHW) 
        return mlir::BoolAttr::get(_ctx, true);
    else 
        return mlir::BoolAttr::get(_ctx, false);
}

//
// ChannelMajorConvolutionRewrite
//

class ChannelMajorConvolutionRewrite final : public mlir::OpRewritePattern<IE::ConvolutionOp> {
public:
    ChannelMajorConvolutionRewrite(mlir::MLIRContext* ctx, const ChannelMajorConvolutionCompatibleOps& userInputInfo,
                                   Logger log)
            : mlir::OpRewritePattern<IE::ConvolutionOp>(ctx), _channelMajorConvolutionChecker(userInputInfo), _log(log) {
    }

public:
    mlir::LogicalResult matchAndRewrite(IE::ConvolutionOp origOp, mlir::PatternRewriter& rewriter) const final;

private:
    const ChannelMajorConvolutionCompatibleOps& _channelMajorConvolutionChecker;
    Logger _log;
};

mlir::LogicalResult ChannelMajorConvolutionRewrite::matchAndRewrite(IE::ConvolutionOp origOp,
                                                                    mlir::PatternRewriter& rewriter) const {
    _log.trace("Found ConvolutionOp Operation '{0}'", origOp->getLoc());

    const auto OpIsCchannelMajorCompitable = _channelMajorConvolutionChecker.isOpChannelMajorCompatible(origOp);

    rewriter.replaceOpWithNewOp<IE::ConvolutionOp>(origOp, origOp.input(), origOp.filter(), origOp.bias(),
                                                   origOp.strides(), origOp.pads_begin(), origOp.pads_end(),
                                                   origOp.dilations(), origOp.post_opAttr(), OpIsCchannelMajorCompitable);

    return mlir::success();
}

//
// IdentifyChannelMajorConvolutionCompatibleOpsPass
//

class IdentifyChannelMajorConvolutionCompatibleOpsPass final :
        public IE::IdentifyChannelMajorConvolutionCompatibleOpsBase<IdentifyChannelMajorConvolutionCompatibleOpsPass> {
public:
    explicit IdentifyChannelMajorConvolutionCompatibleOpsPass(Logger log) {
        Base::initLogger(log, Base::getArgumentName());
    }

private:
    void safeRunOnFunc() final;
};

//
// safeRunOnFunc
//

void IdentifyChannelMajorConvolutionCompatibleOpsPass::safeRunOnFunc() {
    
    auto& ctx = getContext();
    auto func = getFunction();
    auto module = func->getParentOfType<mlir::ModuleOp>();

    IE::CNNNetworkOp netInfo;
    mlir::FuncOp netFunc;
    IE::CNNNetworkOp::getFromModule(module, netInfo, netFunc);
    auto userInputs = netInfo.getInputsInfo();
    vpux::DimsOrder userDimsOrder;

    const auto getTypesWithUserLayout = [](SmallVector<IE::DataInfoOp, 1>& userDataInfo, vpux::DimsOrder& userDimsOrder) {
        for (const auto& p : userDataInfo | indexed) {
            userDimsOrder = p.value().getDimsOrder();
        }
    };

    SmallVector<mlir::Type> newArgTypes(userInputs.size());
    getTypesWithUserLayout(userInputs, userDimsOrder);

    ChannelMajorConvolutionCompatibleOps channelMajorConvolutionChecker(&ctx, _log, userDimsOrder);

    mlir::ConversionTarget target(ctx);

    target.addDynamicallyLegalOp<IE::ConvolutionOp>([&](IE::ConvolutionOp convOp) -> bool {
        if (convOp.channel_major_op())
            return true;

        const auto inputShape = getShape(convOp.filter().getType().cast<mlir::ShapedType>());
        const auto IC = inputShape[IE::Dims4D::Filter::IC];
        auto inputTensorShape = getShape(convOp.input());
        auto width = inputTensorShape[IE::Dims4D::Act::W];
    
        if ((userDimsOrder != DimsOrder::NCHW) || (IC != 3) || (width % 16 != 0)) {
            return true;
        }

        return false;
    });

    mlir::RewritePatternSet patterns(&ctx);
    patterns.insert<ChannelMajorConvolutionRewrite>(&ctx, channelMajorConvolutionChecker, _log);

    if (mlir::failed(mlir::applyPartialConversion(func, target, std::move(patterns)))) {
        signalPassFailure();
    }
}

}  // namespace

//
// createIdentifyChannelMajorConvolutionCompatibleOpsPass
//

std::unique_ptr<mlir::Pass> vpux::IE::createIdentifyChannelMajorConvolutionCompatibleOpsPass(Logger log) {
    return std::make_unique<IdentifyChannelMajorConvolutionCompatibleOpsPass>(log);
}
