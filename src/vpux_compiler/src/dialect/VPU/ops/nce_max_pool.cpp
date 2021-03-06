//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

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

bool vpux::VPU::NCEMaxPoolOp::fitIntoCMX(vpux::NDTypeInterface input, vpux::NDTypeInterface output) {
    Byte requiredCMX(0);

    for (const auto& type : {input, output}) {
        requiredCMX += type.getTotalAllocSize();
    }

    // TODO: VPUX37XX hw doesn't require weights table and activation window for max/average pool ops
    const auto outputShape = output.getShape();
    const auto outputChannels = outputShape[Dims4D::Act::C];

    const auto kernelSize = Shape(parseIntArrayAttr<int64_t>(kernel_size()));

    const auto kernelStrides = Shape(parseIntArrayAttr<int64_t>(strides()));
    const auto strideW = kernelStrides[Dims4D::Strides::X];

    const auto activationWindowSize = NCESparsity::getActivationWindowSize(NCESparsity::Mode::POOL, kernelSize, strideW,
                                                                           input.getElementType(), 1);

    requiredCMX += NCEInvariant::getWeightsTableSize(outputChannels);
    requiredCMX += activationWindowSize * 1_Byte;

    return requiredCMX <= getTotalCMXSize(getOperation());
}

//
// isSupported
//

bool vpux::VPU::NCEMaxPoolOp::isSupported(IE::MaxPoolOp op, LogCb logCb) {
    if (op.getType().getRank() != 4) {
        logCb(formatv("Only 4D tensors are supported"));
        return false;
    }

    if (op.rounding_type() != IE::RoundingType::FLOOR) {
        logCb(formatv("Unsupported rounding mode '{0}'", op.rounding_type()));
        return false;
    }

    const auto kernelSize = Shape(parseIntArrayAttr<int64_t>(op.kernel_size()));
    const auto KY = kernelSize[Dims4D::Kernel::Y];
    const auto KX = kernelSize[Dims4D::Kernel::X];

    if (KY != KX) {
        logCb(formatv("Asymmetric kernel is not supported"));
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

    const auto inputType = op.input().getType().cast<vpux::NDTypeInterface>();
    const auto outputType = op.output().getType().cast<vpux::NDTypeInterface>();

    if (!NCEInvariant::isActTypeSupported(inputType, getInputChannelAlignmentImpl(inputType)) ||
        !NCEInvariant::isActTypeSupported(outputType, getOutputChannelAlignmentImpl(outputType))) {
        logCb(formatv("Misaligned tensor shape"));
        return false;
    }

    const auto inputOrder = inputType.getDimsOrder();
    const auto outputOrder = outputType.getDimsOrder();

    if (inputOrder != DimsOrder::NHWC || outputOrder != DimsOrder::NHWC) {
        logCb(formatv("Unsupported layout"));
        return false;
    }

    return true;
}

//
// verifyOp
//

mlir::LogicalResult vpux::VPU::verifyOp(NCEMaxPoolOp op) {
    const auto arch = getArch(op);

    const auto logCb = [op](const formatv_object_base& msg) {
        (void)errorAt(op, "{0}", msg.str());
    };

    const auto kernelSize = Shape(parseIntArrayAttr<int64_t>(op.kernel_size()));
    const auto KY = kernelSize[Dims4D::Kernel::Y];
    const auto KX = kernelSize[Dims4D::Kernel::X];

    const auto kernelStrides = Shape(parseIntArrayAttr<int64_t>(op.strides()));
    const auto SY = kernelStrides[Dims4D::Strides::Y];
    const auto SX = kernelStrides[Dims4D::Strides::X];

    const auto padTop = op.pad().top().getValue().getSExtValue();
    const auto padBottom = op.pad().bottom().getValue().getSExtValue();
    const auto padLeft = op.pad().left().getValue().getSExtValue();
    const auto padRight = op.pad().right().getValue().getSExtValue();

    if (!NCEInvariant::isAttrsSupported(arch, KY, KX, SY, SX, padTop, padBottom, padLeft, padRight, logCb)) {
        return mlir::failure();
    }

    const auto inputType = op.input().getType().cast<NDTypeInterface>();
    const auto IC = inputType.getShape()[Dims4D::Act::C];

    const auto outputType = op.output().getType().cast<NDTypeInterface>();
    const auto OC = outputType.getShape()[Dims4D::Act::C];

    const auto weightsTableShape = getShape(op.weightsTable());
    const auto expectedWeightsTableShape = NCESparsity::inferWeightsTableShape(OC);

    if (weightsTableShape != expectedWeightsTableShape) {
        return errorAt(op, "Got wrong shape for 'weightsTable' '{0}', expected '{1}'", weightsTableShape,
                       expectedWeightsTableShape);
    }

    const auto activationWindowShape = getShape(op.activationWindow());
    const auto expectedActivationWindowShape = NCESparsity::inferActivationWindowShape(
            NCESparsity::Mode::POOL, kernelSize, SX, inputType.getElementType(), 1);

    if (activationWindowShape != expectedActivationWindowShape) {
        return errorAt(op, "Got wrong shape for 'activationWindow' '{0}', expected '{1}'", activationWindowShape,
                       expectedActivationWindowShape);
    }

    const auto bitPatternSize = VPU::NCESparsity::getBitPatternSize(VPU::NCESparsity::Mode::POOL, kernelSize, SX,
                                                                    inputType.getElementType(), IC);

    if (op.activation_window_channel_length() != bitPatternSize) {
        return errorAt(op, "Got wrong value for 'activation_window_channel_length' '{0}', expected '{1}'",
                       op.activation_window_channel_length(), bitPatternSize);
    }

    return mlir::success();
}

//
// InferShapedTypeOpInterface
//

mlir::LogicalResult vpux::VPU::NCEMaxPoolOp::inferReturnTypeComponents(
        mlir::MLIRContext* ctx, Optional<mlir::Location> optLoc, mlir::ValueShapeRange operands,
        mlir::DictionaryAttr attrs, mlir::RegionRange,
        SmallVectorImpl<mlir::ShapedTypeComponents>& inferredReturnShapes) {
    const auto loc = optLoc.getValueOr(mlir::UnknownLoc::get(ctx));

    NCEMaxPoolOpAdaptor op(operands, attrs);
    if (mlir::failed(op.verify(loc))) {
        return mlir::failure();
    }

    const auto inShape = getShape(op.input());

    const auto windowShape = parseIntArrayAttr<int64_t>(op.kernel_size());
    const auto windowStrides = parseIntArrayAttr<int64_t>(op.strides());

    const auto padTop = op.pad().top().getValue().getSExtValue();
    const auto padBottom = op.pad().bottom().getValue().getSExtValue();
    const auto padLeft = op.pad().left().getValue().getSExtValue();
    const auto padRight = op.pad().right().getValue().getSExtValue();

    const auto dataPaddingBelow = ngraph::CoordinateDiff({padTop, padLeft});
    const auto dataPaddingAbove = ngraph::CoordinateDiff({padBottom, padRight});

    const auto outputShapeNG = ngraph::infer_batched_pooling_forward(
            nullptr, ngraph::Shape(inShape.begin(), inShape.end()), dataPaddingBelow, dataPaddingAbove,
            ngraph::Shape(windowShape.begin(), windowShape.end()),
            ngraph::Strides(windowStrides.begin(), windowStrides.end()), /*is_window_all_in_padding_allowed=*/true,
            /*ceil_mode=*/false);

    const auto outputShape = to_small_vector(outputShapeNG.get_shape() | transformed([](size_t val) {
                                                 return checked_cast<int64_t>(val);
                                             }));

    const auto elemType = op.input().getType().cast<vpux::NDTypeInterface>().getElementType();

    inferredReturnShapes.emplace_back(outputShape, elemType);
    return mlir::success();
}

//
// LayoutInfoOpInterface
//

void vpux::VPU::NCEMaxPoolOp::inferLayoutInfo(IE::LayerLayoutInfo& info) {
    info.fill(DimsOrder::NHWC);
}

//
// AlignedChannelsOpInterface
//

bool vpux::VPU::NCEMaxPoolOp::checkChannelRestrictions(int64_t channels) {
    const auto arch = getArch(*this);

    if (arch == VPU::ArchKind::VPUX37XX) {
        // HW restrictions for channel number
        static const std::unordered_set<int64_t> availableChannels{16, 32, 64};
        return availableChannels.count(channels) != 0;
    }

    return true;
}

//
// TilingBuilderOpInterface
//

vpux::InputTiling vpux::VPU::NCEMaxPoolOp::backInferTileInfo(const vpux::TileInfo& outputTile) {
    const auto origInputShape = getShape(input());
    const auto origPadding = toPadInfo(pad());

    auto inputTiling = vpux::backInferPoolTile(outputTile, origInputShape, kernel_size(), strides(), origPadding);

    inputTiling.tiles.push_back(VPU::getWeightsTableTile(this, outputTile));
    inputTiling.tiles.push_back(VPU::getActivationWindowTile(this, outputTile));

    return inputTiling;
}

void vpux::VPU::NCEMaxPoolOp::adjustAttrs(const TilingInfo& inputTiling, const TileInfo& /*outputTile*/) {
    VPU::adjustPaddings(this, inputTiling);

    const auto inputType = input().getType().cast<NDTypeInterface>();
    const auto IC = inputTiling.tiles[0].shape[Dims4D::Act::C];

    const auto kernelSize = Shape(parseIntArrayAttr<int64_t>(kernel_size()));

    const auto kernelStrides = Shape(parseIntArrayAttr<int64_t>(strides()));
    const auto SX = kernelStrides[Dims4D::Strides::X];

    const auto bitPatternSize = VPU::NCESparsity::getBitPatternSize(VPU::NCESparsity::Mode::POOL, kernelSize, SX,
                                                                    inputType.getElementType(), IC);

    activation_window_channel_lengthAttr(getIntAttr(getContext(), bitPatternSize));
}

//
// NCEOpInterface
//

SmallVector<int64_t> vpux::VPU::NCEMaxPoolOp::getKernelSize() {
    return parseIntArrayAttr<int64_t>(kernel_size());
}

SmallVector<int64_t> vpux::VPU::NCEMaxPoolOp::getStrides() {
    return parseIntArrayAttr<int64_t>(strides());
}

vpux::VPU::PaddingAttr vpux::VPU::NCEMaxPoolOp::getPad() {
    return padAttr();
}
