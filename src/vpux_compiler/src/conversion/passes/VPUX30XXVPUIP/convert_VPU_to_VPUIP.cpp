//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

//

#include "vpux/compiler/conversion.hpp"

#include "vpux/compiler/core/attributes/stride_reqs.hpp"
#include "vpux/compiler/core/layers.hpp"
#include "vpux/compiler/dialect/IERT/ops.hpp"
#include "vpux/compiler/dialect/VPU/attributes.hpp"
#include "vpux/compiler/dialect/VPU/nce_sparsity.hpp"
#include "vpux/compiler/dialect/VPU/ops.hpp"
#include "vpux/compiler/dialect/VPUIP/dpu_tiler.hpp"
#include "vpux/compiler/dialect/VPUIP/nce_invariant.hpp"
#include "vpux/compiler/dialect/VPUIP/ops.hpp"
#include "vpux/compiler/dialect/VPUIP/utils.hpp"
#include "vpux/compiler/dialect/const/ops.hpp"
#include "vpux/compiler/utils/analysis.hpp"
#include "vpux/compiler/utils/attributes.hpp"
#include "vpux/compiler/utils/error.hpp"
#include "vpux/compiler/utils/rewriter.hpp"

#include "vpux/utils/core/enums.hpp"
#include "vpux/utils/core/format.hpp"
#include "vpux/utils/core/range.hpp"

#include <mlir/IR/BlockAndValueMapping.h>
#include <mlir/Transforms/Bufferize.h>
#include <mlir/Transforms/DialectConversion.h>

#include <llvm/ADT/TypeSwitch.h>

using namespace vpux;

namespace {

void addPPETask(mlir::OpBuilder& builder, VPUIP::NCEClusterTaskOp& nceOp, VPU::PPETaskAttr ppeAttr) {
    const auto multList =
            ppeAttr.quant_mult() != nullptr
                    ? builder.getI64ArrayAttr(makeArrayRef(parseIntArrayAttr<int64_t>(ppeAttr.quant_mult())))
                    : nullptr;
    const auto shiftList =
            ppeAttr.quant_shift() != nullptr
                    ? builder.getI64ArrayAttr(makeArrayRef(parseIntArrayAttr<int64_t>(ppeAttr.quant_shift())))
                    : nullptr;
    nceOp.addPPETask(builder, ppeAttr.mode(), ppeAttr.clamp_low(), ppeAttr.clamp_high(), ppeAttr.lrelu_mult(),
                     ppeAttr.lrelu_shift(), multList, shiftList, ppeAttr.quant_post_shift());
}

void addDPUTasks(VPUIP::NCEClusterTaskOp nceOp, mlir::PatternRewriter& rewriter, mlir::Region& workloads) {
    for (auto dpuTaskOp : workloads.getOps<VPU::DPUWorkloadOp>()) {
        SmallVector<int64_t> ends;
        const auto offsets = parseIntArrayAttr<int64_t>(dpuTaskOp.offsets());
        const auto sizes = parseIntArrayAttr<int64_t>(dpuTaskOp.sizes());
        ends.reserve(sizes.size());

        llvm::transform(llvm::seq<size_t>(0, sizes.size()), std::back_inserter(ends), [&](size_t index) {
            return offsets[index] + sizes[index] - 1;
        });

        // as soon as we need workload_x, workload_y, workload_z coords
        const auto dpuStart = {offsets[Dims4D::Act::W.ind()], offsets[Dims4D::Act::H.ind()],
                               offsets[Dims4D::Act::C.ind()]};
        const auto dpuEnds = {ends[Dims4D::Act::W.ind()], ends[Dims4D::Act::H.ind()], ends[Dims4D::Act::C.ind()]};

        nceOp.addDPUTask(rewriter, getIntArrayAttr(rewriter, dpuStart), getIntArrayAttr(rewriter, dpuEnds),
                         dpuTaskOp.pad(), dpuTaskOp.mpe_mode(), dpuTaskOp.cluster_idAttr());
    }
}

//
// Buffer allocation
//

mlir::Value allocateResult(mlir::Location loc, mlir::OpBuilder& builder, mlir::TypeConverter& typeConverter,
                           mlir::Value output) {
    auto origType = output.getType();
    auto memRefType = typeConverter.convertType(origType);
    auto allocOp = builder.create<mlir::memref::AllocOp>(loc, memRefType.cast<mlir::MemRefType>());
    return allocOp.memref();
}

//
// ConvRewriter
//

class ConvRewriter final : public mlir::OpConversionPattern<VPU::NCEConvolutionOp> {
public:
    ConvRewriter(mlir::TypeConverter& typeConverter, mlir::MLIRContext* ctx, Logger log)
            : mlir::OpConversionPattern<VPU::NCEConvolutionOp>(typeConverter, ctx), _log(log) {
        setDebugName("ConvRewriter");
    }

public:
    mlir::LogicalResult matchAndRewrite(VPU::NCEConvolutionOp origOp, OpAdaptor newArgs,
                                        mlir::ConversionPatternRewriter& rewriter) const final;

private:
    Logger _log;
};

mlir::LogicalResult ConvRewriter::matchAndRewrite(VPU::NCEConvolutionOp origOp, OpAdaptor newArgs,
                                                  mlir::ConversionPatternRewriter& rewriter) const {
    //
    // Buffer allocation
    //

    auto* typeConverter = getTypeConverter();
    VPUX_THROW_UNLESS(typeConverter != nullptr, "TypeConverter is not set");

    //
    // Get dimensions
    //

    const auto filterShape = Shape(parseIntArrayAttr<int64_t>(origOp.rawFilterShape()));

    const auto KY = filterShape[Dims4D::Filter::KY];
    const auto KX = filterShape[Dims4D::Filter::KX];

    const auto inOrder = DimsOrder::fromValue(newArgs.input());
    const auto isCMajor = inOrder == DimsOrder::NCHW;

    //
    // Prepare output buffer for DPU
    //

    const auto outputBuffer = allocateResult(origOp.getLoc(), rewriter, *typeConverter, origOp.output());

    //
    // Create NCE per-cluster Operation
    //

    const auto kernelSizeAttr = getIntArrayAttr(getContext(), makeArrayRef({KY, KX}));
    const auto taskType = isCMajor ? VPUIP::NCETaskType::CMCONV : VPUIP::NCETaskType::CONV;

    auto nceOp = rewriter.create<VPUIP::NCEClusterTaskOp>(
            origOp->getLoc(), newArgs.input(), newArgs.filter(), newArgs.weightsTable(), newArgs.activationWindow(),
            /*parent_input=*/newArgs.input(),
            /*parent_output=*/outputBuffer,
            /*output_buff=*/outputBuffer, taskType, kernelSizeAttr, origOp.strides(), origOp.padAttr(),
            origOp.activation_window_channel_lengthAttr());

    addDPUTasks(nceOp, rewriter, origOp.workloads());

    if (origOp.ppe().hasValue()) {
        addPPETask(rewriter, nceOp, origOp.ppeAttr());
    }

    rewriter.replaceOp(origOp, nceOp.output());

    return mlir::success();
}

//
// MaxPoolRewriter
//

class MaxPoolRewriter final : public mlir::OpConversionPattern<VPU::NCEMaxPoolOp> {
public:
    MaxPoolRewriter(mlir::TypeConverter& typeConverter, mlir::MLIRContext* ctx, Logger log)
            : mlir::OpConversionPattern<VPU::NCEMaxPoolOp>(typeConverter, ctx), _log(log) {
    }

public:
    mlir::LogicalResult matchAndRewrite(VPU::NCEMaxPoolOp origOp, OpAdaptor newArgs,
                                        mlir::ConversionPatternRewriter& rewriter) const final;

private:
    Logger _log;
};

mlir::LogicalResult MaxPoolRewriter::matchAndRewrite(VPU::NCEMaxPoolOp origOp, OpAdaptor newArgs,
                                                     mlir::ConversionPatternRewriter& rewriter) const {
    //
    // Buffer allocation
    //

    auto* typeConverter = getTypeConverter();
    VPUX_THROW_UNLESS(typeConverter != nullptr, "TypeConverter is not set");

    //
    // Prepare output buffer for DPU
    //

    const auto outputBuffer = allocateResult(origOp.getLoc(), rewriter, *typeConverter, origOp.output());

    //
    // Create NCE per-cluster Operation
    //

    auto nceOp = rewriter.create<VPUIP::NCEClusterTaskOp>(
            origOp->getLoc(), newArgs.input(), /*weights=*/nullptr, newArgs.weightsTable(), newArgs.activationWindow(),
            /*parent_input=*/newArgs.input(),
            /*parent_output=*/outputBuffer,
            /*output_buff=*/outputBuffer, VPUIP::NCETaskType::MAXPOOL, origOp.kernel_size(), origOp.strides(),
            origOp.pad(), origOp.activation_window_channel_lengthAttr());

    addDPUTasks(nceOp, rewriter, origOp.workloads());

    if (origOp.ppe().hasValue()) {
        addPPETask(rewriter, nceOp, origOp.ppeAttr());
    }

    rewriter.replaceOp(origOp, nceOp.output());

    return mlir::success();
}

//
// DepthwiseConvRewriter
//

class DepthwiseConvRewriter final : public mlir::OpConversionPattern<VPU::NCEDepthConvolutionOp> {
public:
    DepthwiseConvRewriter(mlir::TypeConverter& converter, mlir::MLIRContext* ctx, Logger log)
            : mlir::OpConversionPattern<VPU::NCEDepthConvolutionOp>(converter, ctx), _log(log) {
    }

public:
    mlir::LogicalResult matchAndRewrite(VPU::NCEDepthConvolutionOp origOp, OpAdaptor newArgs,
                                        mlir::ConversionPatternRewriter& rewriter) const final;

private:
    Logger _log;
};

mlir::LogicalResult DepthwiseConvRewriter::matchAndRewrite(VPU::NCEDepthConvolutionOp origOp, OpAdaptor newArgs,
                                                           mlir::ConversionPatternRewriter& rewriter) const {
    //
    // Buffer allocation
    //

    auto* typeConverter = getTypeConverter();
    VPUX_THROW_UNLESS(typeConverter != nullptr, "TypeConverter is not set");

    //
    // Get dimensions
    //

    const auto filterShape = Shape(parseIntArrayAttr<int64_t>(origOp.rawFilterShape()));
    const auto KY = filterShape[Dims4D::Filter::KY];
    const auto KX = filterShape[Dims4D::Filter::KX];

    //
    // Prepare output buffer for DPU
    //

    const auto outputBuffer = allocateResult(origOp.getLoc(), rewriter, *typeConverter, origOp.output());

    //
    // Create NCE per-cluster Operation
    //

    const auto kernelSizeAttr = getIntArrayAttr(getContext(), makeArrayRef({KY, KX}));

    auto nceOp = rewriter.create<VPUIP::NCEClusterTaskOp>(
            origOp->getLoc(), newArgs.input(), newArgs.filter(), newArgs.weightsTable(), newArgs.activationWindow(),
            /*parent_input=*/newArgs.input(),
            /*parent_output=*/outputBuffer,
            /*output_buff=*/outputBuffer, VPUIP::NCETaskType::DWCONV, kernelSizeAttr, origOp.strides(), origOp.pad(),
            origOp.activation_window_channel_lengthAttr());

    addDPUTasks(nceOp, rewriter, origOp.workloads());

    if (origOp.ppe().hasValue()) {
        addPPETask(rewriter, nceOp, origOp.ppeAttr());
    }

    rewriter.replaceOp(origOp, nceOp.output());

    return mlir::success();
}

//
// EltwiseRewriter
//

class EltwiseRewriter final : public mlir::OpConversionPattern<VPU::NCEEltwiseOp> {
public:
    EltwiseRewriter(mlir::TypeConverter& converter, mlir::MLIRContext* ctx, Logger log)
            : mlir::OpConversionPattern<VPU::NCEEltwiseOp>(converter, ctx), _log(log) {
    }

public:
    mlir::LogicalResult matchAndRewrite(VPU::NCEEltwiseOp origOp, OpAdaptor newArgs,
                                        mlir::ConversionPatternRewriter& rewriter) const final;

private:
    Logger _log;
};

mlir::LogicalResult EltwiseRewriter::matchAndRewrite(VPU::NCEEltwiseOp origOp, OpAdaptor newArgs,
                                                     mlir::ConversionPatternRewriter& rewriter) const {
    //
    // Buffer allocation
    //

    auto* typeConverter = getTypeConverter();
    VPUX_THROW_UNLESS(typeConverter != nullptr, "TypeConverter is not set");

    //
    // Prepare output buffer for DPU
    //

    const auto outputBuffer = allocateResult(origOp.getLoc(), rewriter, *typeConverter, origOp.output());

    //
    // Create NCE per-cluster Operation
    //

    const auto activation_window_channel_length = getIntAttr(this->getContext(), static_cast<int32_t>(0));

    auto nceOp = rewriter.create<VPUIP::NCEClusterTaskOp>(origOp->getLoc(), newArgs.input1(), newArgs.input2(),
                                                          /*weightsTable=*/nullptr,
                                                          /*activation_window=*/nullptr,
                                                          /*parent_input=*/newArgs.input1(),
                                                          /*parent_output=*/outputBuffer,
                                                          /*output_buff=*/outputBuffer, VPUIP::NCETaskType::ELTWISE,
                                                          /*kernel_size=*/nullptr,
                                                          /*kernel_strides=*/nullptr,
                                                          /*kernel_padding=*/nullptr, activation_window_channel_length);

    //
    // Create DPU sub-task
    //

    addDPUTasks(nceOp, rewriter, origOp.workloads());

    VPUX_THROW_UNLESS(origOp.ppe().hasValue(), "Eltwise operation should always have PPE info");

    addPPETask(rewriter, nceOp, origOp.ppeAttr());

    rewriter.replaceOp(origOp, nceOp.output());

    return mlir::success();
}

//
// DistributedCastRewriter
//

class DistributedCastRewriter final : public mlir::OpConversionPattern<VPU::DistributedCastOp> {
    using OpAdaptor = typename mlir::OpConversionPattern<VPU::DistributedCastOp>::OpAdaptor;

public:
    DistributedCastRewriter(mlir::TypeConverter& typeConverter, mlir::MLIRContext* ctx, Logger log)
            : mlir::OpConversionPattern<VPU::DistributedCastOp>(typeConverter, ctx), _log(log) {
        this->setDebugName("DistributedCastRewriter");
    }

public:
    mlir::LogicalResult matchAndRewrite(VPU::DistributedCastOp origOp, OpAdaptor newArgs,
                                        mlir::ConversionPatternRewriter& rewriter) const final;

private:
    Logger _log;
};

mlir::LogicalResult DistributedCastRewriter::matchAndRewrite(VPU::DistributedCastOp origOp, OpAdaptor newArgs,
                                                             mlir::ConversionPatternRewriter& rewriter) const {
    _log.trace("Found DistributedCast Operation '{0}' at '{1}'", origOp->getName(), origOp->getLoc());

    auto* typeConverter = getTypeConverter();
    VPUX_THROW_UNLESS(typeConverter != nullptr, "TypeConverter is not set");

    const auto newOutType = typeConverter->convertType(origOp.getType());

    rewriter.replaceOpWithNewOp<VPUIP::DistributedCastOp>(origOp, newOutType, newArgs.input());
    return mlir::success();
}

//
// ConvertVPUToVPUIPPass
//

class ConvertVPUToVPUIPPass final : public ConvertVPUToVPUIPBase<ConvertVPUToVPUIPPass> {
public:
    explicit ConvertVPUToVPUIPPass(Logger log) {
        Base::initLogger(log, Base::getArgumentName());
    }

private:
    void safeRunOnFunc() final;
};

void ConvertVPUToVPUIPPass::safeRunOnFunc() {
    auto& ctx = getContext();
    auto func = getFunction();

    vpux::BufferizeWithDistributedTypeConverter typeConverter;

    const auto isLegalOp = [&](mlir::Operation* op) {
        return typeConverter.isLegal(op);
    };

    mlir::ConversionTarget target(ctx);
    target.addDynamicallyLegalDialect<Const::ConstDialect>(isLegalOp);
    target.addLegalDialect<VPUIP::VPUIPDialect>();
    target.addLegalDialect<IERT::IERTDialect>();
    target.addIllegalDialect<VPU::VPUDialect>();
    // NCEClusterTiling will be handled in follow up pass (convertNCEClusterTilingToVPUIP pass)
    target.addLegalOp<VPU::NCEClusterTilingOp>();
    target.addLegalOp<VPU::YieldOp>();
    target.addLegalOp<mlir::memref::AllocOp>();
    vpux::populateBufferizeWithDistributedMaterializationLegality(target);

    mlir::RewritePatternSet patterns(&ctx);
    patterns.add<ConvRewriter>(typeConverter, &ctx, _log);
    patterns.add<DepthwiseConvRewriter>(typeConverter, &ctx, _log);
    patterns.add<MaxPoolRewriter>(typeConverter, &ctx, _log);
    patterns.add<EltwiseRewriter>(typeConverter, &ctx, _log);
    patterns.add<DistributedCastRewriter>(typeConverter, &ctx, _log);

    if (mlir::failed(mlir::applyPartialConversion(func, target, std::move(patterns)))) {
        signalPassFailure();
    }
}

}  // namespace

//
// createConvertVPUToVPUIPPass
//

std::unique_ptr<mlir::Pass> vpux::createConvertVPUToVPUIPPass(Logger log) {
    return std::make_unique<ConvertVPUToVPUIPPass>(log);
}
