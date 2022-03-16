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

#include <llvm/ADT/TypeSwitch.h>
#include "vpux/compiler/dialect/VPU/strategy_manager.hpp"

using namespace vpux;
using namespace VPU;

bool ConvolutionStrategy::doesLayerFitIntoCMX(mlir::Operation* op, StringRef strategy) const {
    auto origOp = mlir::cast<NCEConvolutionOp>(op);
    mlir::ArrayAttr activationAlignment = nullptr;
    mlir::ArrayAttr weightAlignment = nullptr;
    const auto activationTensorDistributionMode = getActivationTensorDistributionMode(strategy);
    const auto activationTensorNumTiles = getIntArrayAttr(
            origOp.getContext(), getActivationTensorNumTiles(origOp.getOperation(), _numClusters, strategy));
    auto weightsTensorDistributionMode = getWeightsTensorDistributionMode(strategy);
    auto weightsTensorNumTiles = getIntArrayAttr(
            origOp.getContext(), getWeightsTensorNumTiles(origOp.getOperation(), _numClusters, strategy));
    auto outputTensorDistributionMode = getOutputTensorDistributionMode(strategy);
    auto outputTensorNumTiles = getIntArrayAttr(origOp.getContext(),
                                                getOutputTensorNumTiles(origOp.getOperation(), _numClusters, strategy));
    const auto arch = VPU::getArch(origOp.getOperation());
    const auto canUseCMajor =
            VPU::NCEInvariant::isChannelMajorCompatible(arch, origOp.input().getType().cast<vpux::NDTypeInterface>());
    //     if (strategy == splitOverKernel) {
    activationAlignment = getIntArrayAttr(
            origOp.getContext(), getActivationTensorAlignment(op, strategy, !canUseCMajor, activationTensorNumTiles));
    weightAlignment = getIntArrayAttr(origOp.getContext(), getWeightsTensorAlignment(op, strategy));
    //     } else if (strategy == splitOverHeight) {

    //         // Attempt to align the height
    //         auto alignment = SmallVector<int64_t>(numTiles.size(), 1);

    //         const auto numTilesArray = parseIntArrayAttr<int64_t>(numTiles);
    //         if (numTilesArray[Dims4D::Act::H.ind()] > 1 && kernel && needAlignment) {
    //             const auto kernelArray = parseIntArrayAttr<int64_t>(kernel);
    //             const auto KY = kernelArray[0];
    //             if (KY > 1) {
    //                 alignment[Dims4D::Act::H.ind()] = getSOHPerClusterHeightAlignment(shape[Dims4D::Act::W]);
    //             }
    //         }
    //         const auto alignmentAttr = getIntArrayAttr(origOp.getContext(), alignment);
    //     }
    auto distributedActivationTensorType =
            createDistributedTensorType(origOp, origOp.input(), activationTensorDistributionMode,
                                        activationTensorNumTiles, activationAlignment, strategy);
    const auto distributeddWeightsTensorType = createDistributedTensorType(
            origOp, origOp.filter(), weightsTensorDistributionMode, weightsTensorNumTiles, weightAlignment, strategy);
    const auto distributedOutputTensorType = createDistributedTensorType(
            origOp, origOp.output(), outputTensorDistributionMode, outputTensorNumTiles, activationAlignment, strategy);

    return origOp.fitIntoCMX(distributedActivationTensorType, distributeddWeightsTensorType,
                             distributedOutputTensorType);
}

// Each DPU should compute at least one output line. Therefore in order for a layer to be SOH
// compitable it must have an output height of at least the number of DPUs x the number of clusters
// specified for compilation.
// For example for 4 cluster compilation with 5 DPUs per cluster the output height must be a
// minimum of 5x4=20.
bool ConvolutionStrategy::isOperationSplitOverHeightCompatible(mlir::Operation* op) const {
    const auto outputShape = getShape(op->getResult(0));

    if (outputShape[Dims4D::Act::H] < _minimumOutputHeightForSOH) {
        return false;
    }

    auto origOp = mlir::dyn_cast<NCEConvolutionOp>(op);
    const auto inputShape = getShape(origOp.input());
    const auto filterShape = Shape(parseIntArrayAttr<int64_t>(origOp.rawFilterShapeAttr()));
    const auto KY = filterShape[Dims4D::Filter::KY];

    return isSplitOverHeightSupportedByDPU(inputShape, KY, _numClusters, false);
}
