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

#include "vpux/compiler/dialect/VPU/ops.hpp"

#include "vpux/compiler/core/attributes/shape.hpp"
#include "vpux/compiler/core/layers.hpp"
#include "vpux/compiler/dialect/VPU/nce_invariant.hpp"
#include "vpux/compiler/dialect/VPU/nce_sparsity.hpp"
#include "vpux/compiler/utils/attributes.hpp"
#include "vpux/compiler/utils/error.hpp"
#include "vpux/compiler/utils/rewriter.hpp"

#include <ngraph/validation_util.hpp>

#include <unordered_set>

using namespace vpux;

//
// fitIntoCMX
//

bool vpux::VPU::NCEDepthConvolutionOp::fitIntoCMX(mlir::Operation* op, mlir::ArrayAttr strides, mlir::ShapedType input,
                                                  mlir::ShapedType filter, mlir::ShapedType output) {
    const auto filterShape = getShape(filter);
    const auto OC = filterShape[Dims4D::Filter::OC];
    const auto KY = filterShape[Dims4D::Filter::KY];
    const auto KX = filterShape[Dims4D::Filter::KX];

    const Shape kernelSize{KY, KX};

    const auto kernelStrides = Shape(parseIntArrayAttr<int64_t>(strides));
    const auto SX = kernelStrides[Dims4D::Strides::X];

    const auto activationWindowSize = NCESparsity::getActivationWindowSize(kernelSize, SX, input.getElementType(), OC);

    const auto alignment = NCEInvariant::getAlignment(output.getElementType());

    const int64_t remainder = (KY * KX) % alignment;
    VPUX_THROW_UNLESS(remainder >= 0, "Channel alignment cannot be negative: {0}", remainder);

    const int64_t padding = (remainder > 0) ? (alignment - remainder) : 0;

    const std::array<int64_t, 4> alignedFilterShape{OC, 1, 1, KY * KX + padding};
    const mlir::ShapedType alignedFilter = mlir::RankedTensorType::get(alignedFilterShape, filter.getElementType());

    Byte requiredCMX(0);

    for (const auto& type : {input, alignedFilter, output}) {
        requiredCMX += getTotalSize(type);
    }

    requiredCMX += NCEInvariant::getWeightsTableSize(OC);
    requiredCMX += activationWindowSize * 1_Byte;

    return requiredCMX <= getTotalCMXSize(op);
}

//
// isSupported
//

bool vpux::VPU::NCEDepthConvolutionOp::isSupported(IE::GroupConvolutionOp op, NCEInvariant::LogCb logCb) {
    if (op.getType().getRank() != 4) {
        logCb(llvm::formatv("Only 4D tensors are supported"));
        return false;
    }
    if (getShape(op.filter()).size() != 4) {
        logCb(llvm::formatv("Only 4D tensors are supported"));
        return false;
    }

    const auto dilations = parseIntArrayAttr<int64_t>(op.dilations());
    if (dilations[0] != 1 || dilations[1] != 1) {
        logCb(llvm::formatv("Dilated convolution is not supported"));
        return false;
    }

    const auto inputShape = getShape(op.input());
    const auto IC = inputShape[Dims4D::Act::C];

    const auto filterShape = getShape(op.filter());
    const auto fIC = filterShape[Dims4D::Filter::IC];
    const auto OC = filterShape[Dims4D::Filter::OC];
    const auto KY = filterShape[Dims4D::Filter::KY];
    const auto KX = filterShape[Dims4D::Filter::KX];

    if (!op.groups().hasValue()) {
        logCb(llvm::formatv("Grouped convolution does not have groups attribute"));
        return false;
    }
    if (op.groups().getValue() != OC) {
        logCb(llvm::formatv("Unsupported group size: '{0}' expected '{1}'", op.groups(), OC));
        return false;
    }
    if (fIC != 1) {
        logCb(llvm::formatv("Group Convolution with more than one filter per input channel is not supported"));
        return false;
    }
    if (OC != IC) {
        logCb(llvm::formatv("Group Convolution has '{0}' groups, expected '{1}'", OC, IC));
        return false;
    }

    const auto kernelStrides = Shape(parseIntArrayAttr<int64_t>(op.strides()));
    const auto SY = kernelStrides[Dims4D::Strides::Y];
    const auto SX = kernelStrides[Dims4D::Strides::X];

    const auto pads = PadInfo(op.pads_begin(), op.pads_end());

    if (!NCEInvariant::isAttrsSupported(VPU::getArch(op), KY, KX, SY, SX, pads.top, pads.bottom, pads.left, pads.right,
                                        logCb)) {
        return false;
    }

    const auto inputType = op.input().getType().cast<mlir::ShapedType>();
    const auto filterType = op.filter().getType().cast<mlir::ShapedType>();
    const auto outputType = op.output().getType().cast<mlir::ShapedType>();

    if (!NCEInvariant::isActTypeSupported(inputType, getInputChannelAlignment(inputType)) ||
        !NCEInvariant::isActTypeSupported(outputType, getOutputChannelAlignment(outputType))) {
        logCb(llvm::formatv("Misaligned tensor shape"));
        return false;
    }

    const auto inputOrder = DimsOrder::fromType(inputType);
    const auto filterOrder = DimsOrder::fromType(filterType);
    const auto outputOrder = DimsOrder::fromType(outputType);

    if (inputOrder != DimsOrder::NHWC || filterOrder != DimsOrder::OYXI || outputOrder != DimsOrder::NHWC) {
        logCb(llvm::formatv("Unsupported layout"));
        return false;
    }

    if (!fitIntoCMX(op, op.strides(), inputType, filterType, outputType)) {
        logCb(llvm::formatv("Operation doesn't fit into CMX memory"));
        return false;
    }

    return true;
}

//
// verifyOp
//

mlir::LogicalResult vpux::VPU::verifyOp(NCEDepthConvolutionOp op) {
    const auto arch = getArch(op);
    const NCEConvolutionOpAdaptor convAdaptor(op->getOperands(), op->getAttrDictionary(), op->getRegions());
    return verifyConv(op->getLoc(), arch, convAdaptor, op.output());
}

//
// InferShapedTypeOpInterface
//

mlir::LogicalResult vpux::VPU::NCEDepthConvolutionOp::inferReturnTypeComponents(
        mlir::MLIRContext* ctx, Optional<mlir::Location> optLoc, mlir::ValueShapeRange operands,
        mlir::DictionaryAttr attrs, mlir::RegionRange,
        SmallVectorImpl<mlir::ShapedTypeComponents>& inferredReturnShapes) {
    const auto loc = optLoc.getValueOr(mlir::UnknownLoc::get(ctx));

    NCEDepthConvolutionOpAdaptor op(operands, attrs);
    if (mlir::failed(op.verify(loc))) {
        return mlir::failure();
    }

    const auto filterShape = getShape(op.filter());
    const auto fIC = filterShape[Dims4D::Filter::IC];

    if (fIC != 1) {
        return errorAt(loc, "Non depthwize convolution case");
    }

    // Adjust input shape to reuse helpers for standard convolution
    auto inShape = getShape(op.input()).toValues();
    inShape[Dims4D::Act::C] = 1;

    const auto windowStrides = parseIntArrayAttr<int64_t>(op.strides());
    const auto windowDilations = ngraph::Strides({1, 1});

    const auto padTop = op.pad().top().getValue().getSExtValue();
    const auto padBottom = op.pad().bottom().getValue().getSExtValue();
    const auto padLeft = op.pad().left().getValue().getSExtValue();
    const auto padRight = op.pad().right().getValue().getSExtValue();

    const auto dataPaddingBelow = ngraph::CoordinateDiff({padTop, padLeft});
    const auto dataPaddingAbove = ngraph::CoordinateDiff({padBottom, padRight});

    const auto outputShapeNG = ngraph::infer_convolution_forward(
            nullptr, ngraph::Shape(inShape.begin(), inShape.end()),
            ngraph::Strides(windowStrides.size(), 1),  // dummy data dilations
            dataPaddingBelow, dataPaddingAbove, ngraph::Shape(filterShape.begin(), filterShape.end()),
            ngraph::Strides(windowStrides.begin(), windowStrides.end()), windowDilations);

    const auto outputShape = to_small_vector(outputShapeNG.get_shape() | transformed([](size_t val) {
                                                 return checked_cast<int64_t>(val);
                                             }));

    const auto elemType = op.input().getType().cast<mlir::ShapedType>().getElementType();

    inferredReturnShapes.emplace_back(outputShape, elemType);
    return mlir::success();
}

//
// LayoutInfoOpInterface
//

void vpux::VPU::NCEDepthConvolutionOp::inferLayoutInfo(IE::LayerLayoutInfo& info) {
    info.setInput(0, DimsOrder::NHWC);
    info.setInput(1, DimsOrder::OYXI);
    info.setOutput(0, DimsOrder::NHWC);
}

//
// AlignedChannelsOpInterface
//

bool vpux::VPU::NCEDepthConvolutionOp::checkChannelRestrictions(int64_t channels) {
    const auto arch = getArch(*this);

    if (arch == VPU::ArchKind::MTL) {
        // HW restrictions for channel number
        static const std::unordered_set<int64_t> availableChannels{16, 32, 64};
        return availableChannels.count(channels) != 0;
    }

    return true;
}