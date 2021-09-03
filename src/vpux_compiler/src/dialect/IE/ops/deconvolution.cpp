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

//
// Backprop to data is itself convolution, with inputs/outputs/attributes transmogrified as
// follows.
//
//                          Forward   Backward
// "N" axis for data batch  0         0
// "C" axis for data batch  1         1
// "Co" axis for filters    0         0
// "Ci" axis for filters    1         1
// "N" axis for output      0         0
// "C" axis for output      1         1
// Data batch               x         delta
// Data batch shape         S_x       S_o
// Filters                  f         reverse(f) [on spatial axes]
// Filters shape            S_f       S_f
// Window movement strides  q_x       p_x
// Window dilation strides  p_f       p_f
// Padding below            a_x       (S_f - 1)p_f - a_x
// Padding above            b_x       (S_f - 1)p_f +
//                                      + ((a_x + (S_x - 1)p_x + b_x - (S_f - 1)p_f)
//                                         % q_x)
//                                      - b_x
// Data dilation strides    p_x       q_x
// Output shape             S_o       S_x
//
// To _validate_, we simply need to check/infer the output shape of the forward convolution,
// then check to make sure that the incoming delta has the same shape as the forward output.
//

#include "vpux/compiler/dialect/IE/ops.hpp"

#include "vpux/compiler/dialect/const/ops.hpp"
#include "vpux/compiler/utils/attributes.hpp"
#include "vpux/compiler/utils/error.hpp"

#include "vpux/utils/core/checked_cast.hpp"
#include "vpux/utils/core/error.hpp"

#include <ngraph/coordinate.hpp>
#include <ngraph/op/convolution.hpp>
#include <ngraph/util.hpp>
#include <ngraph/validation_util.hpp>

using namespace vpux;

mlir::LogicalResult vpux::IE::DeconvolutionOp::inferReturnTypeComponents(
        mlir::MLIRContext* ctx, Optional<mlir::Location> optLoc, mlir::ValueShapeRange operands,
        mlir::DictionaryAttr attrs, mlir::RegionRange,
        SmallVectorImpl<mlir::ShapedTypeComponents>& inferredReturnShapes) {
    const auto loc = optLoc.getValueOr(mlir::UnknownLoc::get(ctx));

    IE::DeconvolutionOpAdaptor convBackpropData(operands, attrs);
    if (mlir::failed(convBackpropData.verify(loc))) {
        return mlir::failure();
    }

    const auto featureShape = convBackpropData.feature().getType().cast<mlir::ShapedType>().getShape();
    const auto featureType = convBackpropData.feature().getType().cast<mlir::ShapedType>().getElementType();
    const auto outputShape = convBackpropData.output_shape();
    const auto filterShape = convBackpropData.filter().getType().cast<mlir::ShapedType>().getShape();

    const auto dataPaddingBelow = parseIntArrayAttr<int64_t>(convBackpropData.pads_end());
    const auto dataPaddingAbove = parseIntArrayAttr<int64_t>(convBackpropData.pads_begin());
    const auto windowStrides = parseIntArrayAttr<int64_t>(convBackpropData.strides());
    const auto windowDilations = parseIntArrayAttr<int64_t>(convBackpropData.dilations());
    const auto outputPadding = parseIntArrayAttr<int64_t>(convBackpropData.output_padding());

    if (outputShape != nullptr) {
        auto outputShapeConst = outputShape.getDefiningOp<Const::DeclareOp>();
        if (outputShapeConst == nullptr) {
            return errorAt(loc, "Only constant input is supported for output_shape");
        }

        const auto outputShapeContent = outputShapeConst.content();
        const auto outputShapeVals = outputShapeContent.getValues<int64_t>();

        SmallVector<int64_t> mlirOutputShape;
        mlirOutputShape.push_back(featureShape[0]);
        mlirOutputShape.push_back(filterShape[1]);
        std::copy(outputShapeVals.begin(), outputShapeVals.end(), std::back_inserter(mlirOutputShape));

        inferredReturnShapes.emplace_back(mlirOutputShape, featureType);
    } else {
        const std::vector<ngraph::Dimension> nDataShape(std::next(featureShape.begin(), 2), featureShape.end());
        const std::vector<ngraph::Dimension> nFilterShape(std::next(filterShape.begin(), 2), filterShape.end());

        ngraph::op::v1::ConvolutionBackpropData ngraph_op;
        std::vector<ngraph::Dimension> __resultShape;
        ngraph_op.infer_conv_backprop_output_spatial_shape(
                nDataShape,                                                                // data_shape
                nFilterShape,                                                              // filter_sahpe
                ngraph::Strides(windowStrides.begin(), windowStrides.end()),               // strides
                ngraph::Strides(windowDilations.begin(), windowDilations.end()),           // dilations
                ngraph::CoordinateDiff(dataPaddingBelow.begin(), dataPaddingBelow.end()),  // pads_begin
                ngraph::CoordinateDiff(dataPaddingAbove.begin(), dataPaddingAbove.end()),  // pads_end
                ngraph::CoordinateDiff(outputPadding.begin(), outputPadding.end()),        // output_padding
                __resultShape);
        const auto resultShape = ngraph::PartialShape{__resultShape}.get_shape();

        SmallVector<int64_t> mlirOutputShape;
        mlirOutputShape.push_back(featureShape[0]);
        mlirOutputShape.push_back(filterShape[1]);
        std::copy(resultShape.begin(), resultShape.end(), std::back_inserter(mlirOutputShape));

        inferredReturnShapes.emplace_back(mlirOutputShape, featureType);
    }

    return mlir::success();
}

//
// GroupDeconvolution
//

mlir::LogicalResult vpux::IE::GroupDeconvolutionOp::inferReturnTypeComponents(
        mlir::MLIRContext* ctx, Optional<mlir::Location> optLoc, mlir::ValueShapeRange operands,
        mlir::DictionaryAttr attrs, mlir::RegionRange,
        SmallVectorImpl<mlir::ShapedTypeComponents>& inferredReturnShapes) {
    const auto loc = optLoc.getValueOr(mlir::UnknownLoc::get(ctx));

    IE::GroupDeconvolutionOpAdaptor groupDeconv(operands, attrs);
    if (mlir::failed(groupDeconv.verify(loc))) {
        return mlir::failure();
    }

    const auto featureShape = groupDeconv.feature().getType().cast<mlir::ShapedType>().getShape();
    const auto featureType = groupDeconv.feature().getType().cast<mlir::ShapedType>().getElementType();
    const auto outputShape = groupDeconv.output_shape();
    auto filterShape = to_small_vector(groupDeconv.filter().getType().cast<mlir::ShapedType>().getShape());

    const auto dataPaddingBelow = parseIntArrayAttr<int64_t>(groupDeconv.pads_end());
    const auto dataPaddingAbove = parseIntArrayAttr<int64_t>(groupDeconv.pads_begin());
    const auto windowStrides = parseIntArrayAttr<int64_t>(groupDeconv.strides());
    const auto windowDilations = parseIntArrayAttr<int64_t>(groupDeconv.dilations());
    const auto outputPadding = parseIntArrayAttr<int64_t>(groupDeconv.output_padding());

    if (outputShape != nullptr) {
        auto outputShapeConst = outputShape.getDefiningOp<Const::DeclareOp>();
        if (outputShapeConst == nullptr) {
            return errorAt(loc, "Only constant input is supported for output_shape");
        }

        const auto outputShapeContent = outputShapeConst.content();
        const auto outputShapeVals = outputShapeContent.getValues<int64_t>();

        SmallVector<int64_t> mlirOutputShape;
        mlirOutputShape.push_back(featureShape[0]);
        mlirOutputShape.push_back(filterShape[1]);
        std::copy(outputShapeVals.begin(), outputShapeVals.end(), std::back_inserter(mlirOutputShape));

        inferredReturnShapes.emplace_back(mlirOutputShape, featureType);
    } else {
        int64_t groups = filterShape[0];

        // we need to adjust filters_shape to reuse helpers for normal deconvolution
        filterShape[2] *= groups;
        filterShape.erase(filterShape.begin());

        const std::vector<ngraph::Dimension> nDataShape(std::next(featureShape.begin(), 2), featureShape.end());
        const std::vector<ngraph::Dimension> nFilterShape(std::next(filterShape.begin(), 2), filterShape.end());

        ngraph::op::v1::ConvolutionBackpropData ngraph_op;
        std::vector<ngraph::Dimension> __resultShape;
        ngraph_op.infer_conv_backprop_output_spatial_shape(
                nDataShape,                                                                // data_shape
                nFilterShape,                                                              // filter_sahpe
                ngraph::Strides(windowStrides.begin(), windowStrides.end()),               // strides
                ngraph::Strides(windowDilations.begin(), windowDilations.end()),           // dilations
                ngraph::CoordinateDiff(dataPaddingBelow.begin(), dataPaddingBelow.end()),  // pads_begin
                ngraph::CoordinateDiff(dataPaddingAbove.begin(), dataPaddingAbove.end()),  // pads_end
                ngraph::CoordinateDiff(outputPadding.begin(), outputPadding.end()),        // output_padding
                __resultShape);
        const auto resultShape = ngraph::PartialShape{__resultShape}.get_shape();

        SmallVector<int64_t> mlirOutputShape;
        mlirOutputShape.push_back(featureShape[0]);
        mlirOutputShape.push_back(filterShape[1]);
        std::copy(resultShape.begin(), resultShape.end(), std::back_inserter(mlirOutputShape));

        inferredReturnShapes.emplace_back(mlirOutputShape, featureType);
    }

    return mlir::success();
}

namespace {

class GroupsToAttr final : public mlir::OpRewritePattern<IE::GroupDeconvolutionOp> {
public:
    using mlir::OpRewritePattern<IE::GroupDeconvolutionOp>::OpRewritePattern;

public:
    mlir::LogicalResult matchAndRewrite(IE::GroupDeconvolutionOp convOp, mlir::PatternRewriter& rewriter) const final;
};

mlir::LogicalResult GroupsToAttr::matchAndRewrite(IE::GroupDeconvolutionOp deconvOp,
                                                  mlir::PatternRewriter& rewriter) const {
    if (deconvOp.groups().hasValue()) {
        return mlir::failure();
    }

    auto filterShape = to_small_vector(deconvOp.filter().getType().cast<mlir::ShapedType>().getShape());
    const auto groups = filterShape[0];

    const auto elemType = deconvOp.feature().getType().cast<mlir::ShapedType>().getElementType();
    const SmallVector<int64_t> weightShape = {filterShape[0], filterShape[1], filterShape[2], filterShape[3],
                                              filterShape[4]};
    const auto dataStorageType = mlir::RankedTensorType::get(weightShape, elemType);
    const auto dwConvFilterContent = deconvOp.filter().getDefiningOp<Const::DeclareOp>().content();

    // Weights reverse according to ngraph implementation
    auto dwConvFilterVals = dwConvFilterContent.getValues<float16>();
    SmallVector<float16> vals(dwConvFilterVals.size());
    size_t spatialDims = filterShape[4] * filterShape[3];
    for (size_t i = 0; i < dwConvFilterVals.size() / spatialDims; i++)
        for (size_t j = 0; j < spatialDims; j++)
            vals[i * spatialDims + j] = dwConvFilterVals[i * spatialDims + spatialDims - j - 1];

    const auto dataAttr = mlir::DenseElementsAttr::get(dataStorageType, makeArrayRef(vals));
    auto dwConvFilter =
            rewriter.create<Const::DeclareOp>(deconvOp.getLoc(), dataStorageType, Const::ContentAttr::get(dataAttr));

    rewriter.replaceOpWithNewOp<IE::GroupDeconvolutionOp>(
            deconvOp, deconvOp.feature(), dwConvFilter.output() /* deconvOp.filter() */ /*newFilter.output()*/,
            deconvOp.output_shape(), deconvOp.stridesAttr(), deconvOp.pads_beginAttr(), deconvOp.pads_endAttr(),
            deconvOp.dilationsAttr(), deconvOp.output_paddingAttr(), getIntAttr(deconvOp.getContext(), groups));

    return mlir::success();
}

}  // namespace

void vpux::IE::GroupDeconvolutionOp::getCanonicalizationPatterns(mlir::RewritePatternSet& patterns,
                                                                 mlir::MLIRContext* context) {
    patterns.insert<GroupsToAttr>(context);
}
