//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

//

#include "vpux/compiler/dialect/VPU/attributes.hpp"

#include "vpux/compiler/core/attributes/stride_reqs.hpp"
#include "vpux/compiler/core/tiling.hpp"
#include "vpux/compiler/dialect/IE/attributes/structs.hpp"
#include "vpux/compiler/dialect/IE/ops.hpp"
#include "vpux/compiler/dialect/IE/utils/resources.hpp"
#include "vpux/compiler/utils/analysis.hpp"
#include "vpux/compiler/utils/attributes.hpp"

#include "vpux/utils/core/error.hpp"
#include "vpux/utils/core/mem_size.hpp"
#include "vpux/utils/core/numeric.hpp"

#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/IR/Identifier.h>

#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/TypeSwitch.h>

using namespace vpux;

//
// Run-time resources
//

namespace {

constexpr StringLiteral derateFactorAttrName = "VPU.derateFactor";
constexpr StringLiteral bandwidthAttrName = "VPU.bandwidth";
constexpr StringLiteral processorFrequencyAttrName = "VPU.processorFrequency";

}  // namespace

StringLiteral vpux::VPU::getMemoryDerateAttrName() {
    return derateFactorAttrName;
}

StringLiteral vpux::VPU::getMemoryBandwidthAttrName() {
    return bandwidthAttrName;
}

StringLiteral vpux::VPU::getProcessorFrequencyAttrName() {
    return processorFrequencyAttrName;
}

namespace {

constexpr int VPUX30XX_MAX_DPU_GROUPS = 4;
constexpr int VPUX37XX_MAX_DPU_GROUPS = 2;

}  // namespace

uint32_t vpux::VPU::getMaxDPUClusterNum(ArchKind arch) {
    switch (arch) {
    case VPU::ArchKind::VPUX30XX:
        return VPUX30XX_MAX_DPU_GROUPS;
    case VPU::ArchKind::VPUX311X:
        return VPUX30XX_MAX_DPU_GROUPS;
    case VPU::ArchKind::VPUX37XX:
        return VPUX37XX_MAX_DPU_GROUPS;
    default:
        VPUX_THROW("Unsupported architecture '{0}'", arch);
    }
}

uint32_t vpux::VPU::getMaxDPUClusterNum(mlir::Operation* op) {
    return VPU::getMaxDPUClusterNum(VPU::getArch(op));
}

Byte vpux::VPU::getTotalCMXSize(mlir::Operation* op) {
    auto module = getTopLevelModule(op);

    auto cmxRes = IE::getAvailableMemory(module, VPU::MemoryKind::CMX_NN);
    VPUX_THROW_UNLESS(cmxRes != nullptr, "Can't get information about {0} memory", VPU::MemoryKind::CMX_NN);

    return cmxRes.size();
}

//
// ArchKind
//

namespace {

constexpr StringLiteral archAttrName = "VPU.arch";

constexpr Byte DDR_HEAP_SIZE = 500_MB;
constexpr Byte CSRAM_SIZE = 24_MB;

constexpr Byte KMB_CMX_WORKSPACE_SIZE = Byte(896_KB);

constexpr Byte VPUX37XX_CMX_WORKSPACE_SIZE = Byte(1936_KB);

}  // namespace

void vpux::VPU::setArch(mlir::ModuleOp module, ArchKind kind, Optional<int> numOfDPUGroups) {
    VPUX_THROW_WHEN(module->hasAttr(archAttrName),
                    "Architecture is already defined, probably you run '--init-compiler' twice");

    module->setAttr(archAttrName, ArchKindAttr::get(module.getContext(), kind));

    const auto addMem = [&](MemoryKind kind, Byte size, double derateFactor, uint32_t bandwidth) {
        auto mem = IE::addAvailableMemory(module, kind, size);
        mem->setAttr(derateFactorAttrName, getFPAttr(module.getContext(), derateFactor));
        mem->setAttr(bandwidthAttrName, getIntAttr(module.getContext(), bandwidth));
    };

    const auto addExecutor = [&](ExecutorKind kind, uint32_t count) {
        return IE::addAvailableExecutor(module, kind, count);
    };

    const auto getNumOfDPUGroupsVal = [&](int maxDpuGroups) {
        int numOfDPUGroupsVal = numOfDPUGroups.hasValue() ? numOfDPUGroups.getValue() : maxDpuGroups;
        VPUX_THROW_UNLESS(1 <= numOfDPUGroupsVal && numOfDPUGroupsVal <= maxDpuGroups,
                          "Invalid number of DPU groups: '{0}'", numOfDPUGroupsVal);
        return numOfDPUGroupsVal;
    };

    IE::ExecutorResourceOp nceCluster;

    switch (kind) {
    case ArchKind::VPUX30XX: {
        addMem(MemoryKind::DDR, DDR_HEAP_SIZE, 0.6, 8);
        addMem(MemoryKind::CMX_NN, KMB_CMX_WORKSPACE_SIZE, 1.0, 32);

        addExecutor(ExecutorKind::DMA_NN, 1);
        addExecutor(ExecutorKind::SHAVE_UPA, 16);
        nceCluster = IE::addAvailableExecutor(module, ExecutorKind::NCE, getNumOfDPUGroupsVal(VPUX30XX_MAX_DPU_GROUPS));
        nceCluster.addSubExecutor(ExecutorKind::DPU, 5);

        break;
    }
    case ArchKind::VPUX311X: {
        addMem(MemoryKind::DDR, DDR_HEAP_SIZE, 0.6, 8);
        addMem(MemoryKind::CSRAM, CSRAM_SIZE, 0.85, 64);
        addMem(MemoryKind::CMX_NN, KMB_CMX_WORKSPACE_SIZE, 1.0, 32);

        addExecutor(ExecutorKind::DMA_NN, 2);
        addExecutor(ExecutorKind::SHAVE_UPA, 16);
        nceCluster = IE::addAvailableExecutor(module, ExecutorKind::NCE, getNumOfDPUGroupsVal(VPUX30XX_MAX_DPU_GROUPS));
        nceCluster.addSubExecutor(ExecutorKind::DPU, 5);

        break;
    }
    case ArchKind::VPUX37XX: {
        addMem(MemoryKind::DDR, DDR_HEAP_SIZE, 0.6, 8);
        addMem(MemoryKind::CMX_NN, VPUX37XX_CMX_WORKSPACE_SIZE, 1.0, 32);

        addExecutor(ExecutorKind::DMA_NN, 2);
        // TODO: SHAVE_NN shouldn't be used here
        addExecutor(ExecutorKind::SHAVE_NN, 1);
        // TODO: move SHAVE_ACT as a sub-executor for NCE
        // TODO: use actual number of ACT SHAVES
        addExecutor(ExecutorKind::SHAVE_ACT, 1);
        nceCluster = IE::addAvailableExecutor(module, ExecutorKind::NCE, getNumOfDPUGroupsVal(VPUX37XX_MAX_DPU_GROUPS));
        nceCluster.addSubExecutor(ExecutorKind::DPU, 1);

        break;
    }
    default:
        VPUX_THROW("Unsupported architecture '{0}'", kind);
    }

    nceCluster->setAttr(processorFrequencyAttrName, getFPAttr(module.getContext(), 700.0));
}

VPU::ArchKind vpux::VPU::getArch(mlir::Operation* op) {
    auto module = getTopLevelModule(op);

    if (auto attr = module->getAttr(archAttrName)) {
        VPUX_THROW_UNLESS(attr.isa<VPU::ArchKindAttr>(), "Module attribute '{0}' has unsupported value '{1}'",
                          archAttrName, attr);
        return attr.cast<VPU::ArchKindAttr>().getValue();
    }

    return VPU::ArchKind::UNKNOWN;
}

//
// CompilationMode
//

namespace {

constexpr StringLiteral compilationModeAttrName = "VPU.compilationMode";

}  // namespace

void vpux::VPU::setCompilationMode(mlir::ModuleOp module, CompilationMode compilationMode) {
    VPUX_THROW_WHEN(module->hasAttr(compilationModeAttrName),
                    "CompilationMode is already defined, probably you run '--init-compiler' twice");

    module->setAttr(compilationModeAttrName, VPU::CompilationModeAttr::get(module.getContext(), compilationMode));
}

VPU::CompilationMode vpux::VPU::getCompilationMode(mlir::Operation* op) {
    auto module = getTopLevelModule(op);

    if (auto attr = module->getAttr(compilationModeAttrName)) {
        VPUX_THROW_UNLESS(attr.isa<VPU::CompilationModeAttr>(), "Module attribute '{0}' has unsupported value '{1}'",
                          compilationModeAttrName, attr);

        return attr.cast<VPU::CompilationModeAttr>().getValue();
    }

    // Use DefaultHW as a default mode
    return VPU::CompilationMode::DefaultHW;
}

//
// PaddingAttr
//

VPU::PaddingAttr vpux::VPU::getPaddingAttr(mlir::MLIRContext* ctx, int64_t left, int64_t right, int64_t top,
                                           int64_t bottom) {
    return PaddingAttr::get(getIntAttr(ctx, left), getIntAttr(ctx, right), getIntAttr(ctx, top),
                            getIntAttr(ctx, bottom), ctx);
}

VPU::PaddingAttr vpux::VPU::getPaddingAttr(mlir::MLIRContext* ctx, ArrayRef<int64_t> padsBegin,
                                           ArrayRef<int64_t> padsEnd) {
    VPUX_THROW_UNLESS(padsBegin.size() == 2, "Paddings array has unsuppoted size '{0}'", padsBegin.size());
    VPUX_THROW_UNLESS(padsEnd.size() == 2, "Paddings array has unsuppoted size '{0}'", padsEnd.size());
    return getPaddingAttr(ctx, padsBegin[1], padsEnd[1], padsBegin[0], padsEnd[0]);
}

VPU::PaddingAttr vpux::VPU::getPaddingAttr(mlir::MLIRContext* ctx, const PadInfo& pad) {
    return getPaddingAttr(ctx, pad.left, pad.right, pad.top, pad.bottom);
}

PadInfo vpux::VPU::toPadInfo(PaddingAttr attr) {
    const auto left = attr.left().getValue().getSExtValue();
    const auto right = attr.right().getValue().getSExtValue();
    const auto top = attr.top().getValue().getSExtValue();
    const auto bottom = attr.bottom().getValue().getSExtValue();
    return PadInfo(left, right, top, bottom);
}

//
// PPETaskAttr
//

VPU::PPETaskAttr vpux::VPU::getPPETaskAttr(mlir::MLIRContext* ctx, VPU::PPEMode mode) {
    return VPU::PPETaskAttr::get(VPU::PPEModeAttr::get(ctx, mode), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                 nullptr, ctx);
}

VPU::PPETaskAttr vpux::VPU::getPPETaskAttr(mlir::MLIRContext* ctx, VPU::PPEMode mode, int64_t clampLow,
                                           int64_t clampHigh, int64_t lreluMult, int64_t lreluShift) {
    return VPU::PPETaskAttr::get(VPU::PPEModeAttr::get(ctx, mode), getIntAttr(ctx, clampLow),
                                 getIntAttr(ctx, clampHigh), getIntAttr(ctx, lreluMult), getIntAttr(ctx, lreluShift),
                                 nullptr, nullptr, nullptr, ctx);
}

VPU::PPETaskAttr vpux::VPU::getPPETaskAttr(mlir::MLIRContext* ctx, VPU::PPEMode mode, int64_t clampLow,
                                           int64_t clampHigh, int64_t lreluMult, int64_t lreluShift,
                                           ArrayRef<int64_t> quantMult, ArrayRef<int64_t> quantShift,
                                           int64_t quantPostShift) {
    return VPU::PPETaskAttr::get(VPU::PPEModeAttr::get(ctx, mode), getIntAttr(ctx, clampLow),
                                 getIntAttr(ctx, clampHigh), getIntAttr(ctx, lreluMult), getIntAttr(ctx, lreluShift),
                                 getIntArrayAttr(ctx, quantMult), getIntArrayAttr(ctx, quantShift),
                                 getIntAttr(ctx, quantPostShift), ctx);
}

VPU::PPEMode vpux::VPU::getPPEMode(VPU::EltwiseType type) {
    switch (type) {
    case VPU::EltwiseType::ADD:
        return vpux::VPU::PPEMode::ADD;
    case VPU::EltwiseType::AND:
        return vpux::VPU::PPEMode::AND;
    case VPU::EltwiseType::MULTIPLY:
        return vpux::VPU::PPEMode::MULT;
    case VPU::EltwiseType::SUBTRACT:
        return vpux::VPU::PPEMode::SUB;
    case VPU::EltwiseType::MIN:
        return vpux::VPU::PPEMode::MINIMUM;
    case VPU::EltwiseType::MAX:
        return vpux::VPU::PPEMode::MAXIMUM;
    default:
        VPUX_THROW("Unsupported EltwiseType '{0}' for PPEMode", type);
    }
}

//
// DistributedTensorAttr
//

mlir::LogicalResult vpux::VPU::verify(FuncRef<mlir::InFlightDiagnostic()> emitError,
                                      DistributedTensorAttr distributedAttr, ArrayRef<int64_t> shape) {
    const auto distributionMode = distributedAttr.mode().getValue();

    if (distributionMode != VPU::DistributionMode::NONE) {
        if (distributedAttr.num_clusters() == nullptr) {
            return printTo(emitError(), "Missing number of clusters.");
        }

        auto numClusters = distributedAttr.num_clusters().getInt();
        if (numClusters <= 0) {
            return printTo(emitError(), "The number of clusters must be greater than 0. Got: {0}", numClusters);
        }
    }

    const auto isTiledMode = [](VPU::DistributionMode mode) {
        return VPU::bitEnumContains(mode, VPU::DistributionMode::SEGMENTED) ||
               VPU::bitEnumContains(mode, VPU::DistributionMode::OVERLAPPED);
    };

    if (!isTiledMode(distributionMode)) {
        return mlir::success();
    }

    if (isTiledMode(distributionMode)) {
        if (distributedAttr.num_tiles() == nullptr || distributedAttr.num_clusters() == nullptr) {
            return printTo(emitError(), "Missing number of tiles and clusters.");
        }

        // Check for validity of tiling scheme
        const auto tilingScheme = parseIntArrayAttr<int64_t>(distributedAttr.num_tiles());
        const auto numClusters = distributedAttr.num_clusters().getInt();

        const auto isValidTile = [](auto dim) {
            return dim > 1;
        };

        if (llvm::count_if(tilingScheme, isValidTile) != 1) {
            return printTo(emitError(), "Currently supporting single axis cluster tiling.");
        }

        const auto axis = std::distance(tilingScheme.begin(), llvm::find_if(tilingScheme, isValidTile));

        if (tilingScheme[axis] != numClusters) {
            return printTo(emitError(), "Incompatibility between tiling scheme '{0}' and number of clusters '{1}'",
                           tilingScheme[axis], numClusters);
        }

        // Limitations on tiling axes
        if (VPU::bitEnumContains(distributionMode, VPU::DistributionMode::OVERLAPPED)) {
            if (axis != Dims4D::Act::H.ind()) {
                return printTo(emitError(), "Overlapped cluster tiling is only supported for dimension H");
            }

            if (distributedAttr.alignment() != nullptr) {
                const auto alignment = parseIntArrayAttr<int64_t>(distributedAttr.alignment());
                if (alignment[axis] != 1) {
                    return printTo(
                            emitError(),
                            "Overlapped cluster tiling does not support alignment on the same axis used for tiling.");
                }
            }

            if (distributedAttr.kernel() == nullptr || distributedAttr.pads() == nullptr ||
                distributedAttr.strides() == nullptr) {
                return printTo(emitError(), "Overlapped cluster tiling requires kernel, pads and strides to be set");
            }
        }

        if (VPU::bitEnumContains(distributionMode, VPU::DistributionMode::SEGMENTED) &&
            !(axis == Dims4D::Act::H.ind() || axis == Dims4D::Act::C.ind() || axis == Dims4D::Filter::OC.ind())) {
            return printTo(emitError(), "Segmented cluster tiling is only supported for activation dimensions H and K "
                                        "and kernel dimension K");
        }
    }

    if (distributedAttr.alignment() != nullptr) {
        const auto alignment = parseIntArrayAttr<int64_t>(distributedAttr.alignment());
        if (shape.size() != alignment.size()) {
            return printTo(emitError(), "Incompatibility in sizes between tensor shape '{0}' and alignment '{1}'",
                           shape.size(), alignment.size());
        }
    }

    if (distributedAttr.num_tiles() != nullptr) {
        const auto numTiles = parseIntArrayAttr<int64_t>(distributedAttr.num_tiles());
        if (shape.size() != numTiles.size()) {
            return printTo(emitError(), "Incompatibility in sizes between tensor shape '{0}' and tiling scheme '{1}'",
                           shape.size(), numTiles.size());
        }
    }

    if (distributedAttr.kernel() != nullptr) {
        const auto kernel = parseIntArrayAttr<int64_t>(distributedAttr.kernel());
        if (kernel.size() != 2) {
            return printTo(emitError(), "Expected kernel size to be 2. Got '{0}'", kernel.size());
        }
        const auto KY = kernel[Dims4D::Kernel::Y.ind()];
        const auto KX = kernel[Dims4D::Kernel::X.ind()];
        if (KY <= 0 || KX <= 0) {
            return printTo(emitError(), "Invalid kernel size: height '{0}', width '{1}'", KY, KX);
        }
    }

    if (distributedAttr.pads() != nullptr) {
        const auto padTop = distributedAttr.pads().top().getInt();
        const auto padBottom = distributedAttr.pads().bottom().getInt();
        const auto padLeft = distributedAttr.pads().left().getInt();
        const auto padRight = distributedAttr.pads().right().getInt();
        if (padTop < 0 || padBottom < 0 || padLeft < 0 || padRight < 0) {
            return printTo(emitError(), "Invalid pads: top '{0}', bottom '{1}', left '{2}', right '{3}'", padTop,
                           padBottom, padLeft, padRight);
        }
    }

    if (distributedAttr.strides() != nullptr) {
        const auto strides = parseIntArrayAttr<int64_t>(distributedAttr.strides());
        if (strides.size() != 2) {
            return printTo(emitError(), "Expected strides size to be 2. Got '{0}'", strides.size());
        }
        const auto SY = strides[Dims4D::Strides::Y.ind()];
        const auto SX = strides[Dims4D::Strides::X.ind()];
        if (SY <= 0 || SX <= 0) {
            return printTo(emitError(), "Invalid strides: height '{0}', width '{1}'", SY, SX);
        }
    }

    return mlir::success();
}

mlir::LogicalResult vpux::VPU::areDistributionModesCompatible(DistributionMode sourceMode,
                                                              DistributionMode targetMode) {
    // Consecutive distribution modes for a SOK chain or from HKSwitch to SOK
    if ((sourceMode == (DistributionMode::DUPLICATED | DistributionMode::SEGMENTED) ||
         sourceMode == (DistributionMode::MULTICASTED | DistributionMode::SEGMENTED)) &&
        targetMode == DistributionMode::DUPLICATED) {
        return mlir::success();
    }

    return mlir::failure();
}

//
// Tiling utils
//

// Segmentation logic operates on schema and runtime assumption that a segmented tensor should be split equally
// across the axis, with the remainder cluster possibly having a smaller tile.
SmallVector<Shape> splitSegmentedShape(ArrayRef<int64_t> shape, ArrayRef<int64_t> tilingScheme,
                                       const int64_t numClusters, const int64_t axis,
                                       Optional<ArrayRef<int64_t>> alignment) {
    auto tiledShape = to_small_vector(shape);
    tiledShape[axis] = divUp(tiledShape[axis], tilingScheme[axis]);
    tiledShape = alignShape(tiledShape, alignment);

    // Last cluster will have the remainder and it doesn't have to be aligned
    auto remainderTileShape = to_small_vector(shape);
    remainderTileShape[axis] = shape[axis] - tiledShape[axis] * (tilingScheme[axis] - 1);
    VPUX_THROW_UNLESS(remainderTileShape[axis] > 0, "Improper split, '{0}' over '{1}' tiles", shape[axis],
                      tilingScheme[axis]);

    SmallVector<Shape> segmentedTiles(numClusters - 1, Shape(tiledShape));
    segmentedTiles.push_back(Shape(remainderTileShape));
    return segmentedTiles;
}

SmallVector<DimRange> getOverlappedInputTileDimRanges(ArrayRef<int64_t> shape, ArrayRef<int64_t> tilingScheme,
                                                      VPU::DistributedTensorAttr distributionAttr, const int64_t axis,
                                                      const int64_t numClusters) {
    const auto N = shape[Dims4D::Act::N.ind()];
    const auto C = shape[Dims4D::Act::C.ind()];
    const auto Y = shape[Dims4D::Act::H.ind()];
    const auto X = shape[Dims4D::Act::W.ind()];

    const auto kernel = parseIntArrayAttr<int64_t>(distributionAttr.kernel());
    const auto KY = kernel[Dims4D::Kernel::Y.ind()];
    const auto KX = kernel[Dims4D::Kernel::X.ind()];

    const auto pads = distributionAttr.pads();
    const auto padTop = pads.top().getInt();
    const auto padBottom = pads.bottom().getInt();
    const auto padLeft = pads.left().getInt();
    const auto padRight = pads.right().getInt();

    const auto strides = parseIntArrayAttr<int64_t>(distributionAttr.strides());
    const auto SY = strides[Dims4D::Strides::Y.ind()];
    const auto SX = strides[Dims4D::Strides::X.ind()];

    const auto outputHeight = (Y - KY + padTop + padBottom) / SY + 1;
    const auto outputWidth = (X - KX + padLeft + padRight) / SX + 1;
    const SmallVector<int64_t> outputShape{N, C, outputHeight, outputWidth};
    // Alignment should only be considered for final input shape,
    // not the intermediary output shape
    const auto outputTiles = splitSegmentedShape(outputShape, tilingScheme, numClusters, axis, None);

    int64_t offset = 0;
    SmallVector<DimRange> inputTileDimRanges;
    for (const auto& outputTile : outputTiles) {
        const auto height = outputTile[Dim(axis)];
        const DimRange tileHeight(offset, offset + height);
        offset += height;

        DimRange inputTile(0, 0);
        std::tie(inputTile, std::ignore, std::ignore) =
                vpux::inputForOutputDim(tileHeight, KY, SY, {0, Y}, padTop, padBottom);
        inputTileDimRanges.push_back(inputTile);
    }
    return inputTileDimRanges;
}

SmallVector<Shape> vpux::VPU::getPerClusterComputeShapes(ShapeRef shapeRef, DistributedTensorAttr distributionAttr) {
    auto shape = to_small_vector(shapeRef.raw());
    const auto distributionMode = distributionAttr.mode().getValue();

    const auto numClusters = distributionAttr.num_clusters().getInt();
    auto tiledComputeShapes = SmallVector<Shape>(numClusters);

    const auto isValidTile = [](auto dim) {
        return dim > 1;
    };

    Optional<ArrayRef<int64_t>> optionalAlignment = None;
    auto alignment = SmallVector<int64_t>(numClusters);
    if (distributionAttr.alignment() != nullptr) {
        alignment = parseIntArrayAttr<int64_t>(distributionAttr.alignment());
        optionalAlignment = Optional<ArrayRef<int64_t>>(alignment);
    }

    if (VPU::bitEnumContains(distributionMode, VPU::DistributionMode::SEGMENTED)) {
        const auto tilingScheme = parseIntArrayAttr<int64_t>(distributionAttr.num_tiles());
        const auto axis = std::distance(tilingScheme.begin(), llvm::find_if(tilingScheme, isValidTile));

        tiledComputeShapes = splitSegmentedShape(shape, tilingScheme, numClusters, axis, optionalAlignment);
    } else if (VPU::bitEnumContains(distributionMode, VPU::DistributionMode::OVERLAPPED)) {
        const auto tilingScheme = parseIntArrayAttr<int64_t>(distributionAttr.num_tiles());
        const auto axis = std::distance(tilingScheme.begin(), llvm::find_if(tilingScheme, isValidTile));
        const auto inputTileDimRanges =
                getOverlappedInputTileDimRanges(shape, tilingScheme, distributionAttr, axis, numClusters);

        for (auto p : inputTileDimRanges | indexed) {
            const auto inputTile = p.value();
            const auto cluster = p.index();
            shape[axis] = inputTile.end - inputTile.begin;
            tiledComputeShapes[cluster] = Shape(alignShape(shape, optionalAlignment));
        }
    } else {
        std::fill_n(tiledComputeShapes.begin(), tiledComputeShapes.size(), Shape(alignShape(shape, optionalAlignment)));
    }

    return tiledComputeShapes;
}

SmallVector<Shape> vpux::VPU::getPerClusterComputeShapeOffsets(ShapeRef shapeRef,
                                                               DistributedTensorAttr distributionAttr) {
    const auto shape = to_small_vector(shapeRef.raw());
    const auto distributionMode = distributionAttr.mode().getValue();

    const auto numClusters = distributionAttr.num_clusters().getInt();
    auto tiledComputeShapeOffsets = SmallVector<Shape>(numClusters, Shape(shapeRef.size(), 0));

    const auto isValidTile = [](auto dim) {
        return dim > 1;
    };

    if (VPU::bitEnumContains(distributionMode, VPU::DistributionMode::SEGMENTED)) {
        const auto tiledComputeShapes = getPerClusterComputeShapes(shapeRef, distributionAttr);
        const auto tilingScheme = parseIntArrayAttr<int64_t>(distributionAttr.num_tiles());
        const auto axis = std::distance(tilingScheme.begin(), llvm::find_if(tilingScheme, isValidTile));
        int64_t offset = 0;
        for (int64_t idx = 0; idx < numClusters; idx++) {
            tiledComputeShapeOffsets[idx][Dim(axis)] = offset;
            offset += tiledComputeShapes[idx][Dim(axis)];
        }
    } else if (VPU::bitEnumContains(distributionMode, VPU::DistributionMode::OVERLAPPED)) {
        const auto tilingScheme = parseIntArrayAttr<int64_t>(distributionAttr.num_tiles());
        const auto axis = std::distance(tilingScheme.begin(), llvm::find_if(tilingScheme, isValidTile));
        const auto inputTileDimRanges =
                getOverlappedInputTileDimRanges(shape, tilingScheme, distributionAttr, axis, numClusters);

        for (auto p : inputTileDimRanges | indexed) {
            const auto inputTile = p.value();
            const auto cluster = p.index();
            tiledComputeShapeOffsets[cluster][Dim(axis)] = inputTile.begin;
        }
    }
    return tiledComputeShapeOffsets;
}

SmallVector<PadInfo> vpux::VPU::getPerClusterPadding(DistributedTensorAttr distributionAttr) {
    const auto mode = distributionAttr.mode().getValue();
    VPUX_THROW_UNLESS(mode == VPU::DistributionMode::OVERLAPPED,
                      "Currently getting per cluster padding is supported only for OVERLAPPED, mode - {0}",
                      VPU::stringifyDistributionMode(mode));

    SmallVector<PadInfo> perClusterPadInfo;
    const auto pads = distributionAttr.pads();
    const auto top = pads.top().getInt();
    const auto bottom = pads.bottom().getInt();
    const auto left = pads.left().getInt();
    const auto right = pads.right().getInt();

    perClusterPadInfo.push_back(PadInfo(left, right, top, 0));
    for (auto cluster = 1; cluster < distributionAttr.num_clusters().getInt() - 1; cluster++) {
        perClusterPadInfo.push_back(PadInfo(left, right, 0, 0));
    }
    perClusterPadInfo.push_back(PadInfo(left, right, 0, bottom));

    return perClusterPadInfo;
}

SmallVector<StridedShape> vpux::VPU::getPerClusterStridedShapes(ShapeRef shape, StridesRef strides, DimsOrder dimsOrder,
                                                                Bit elemSize, DistributedTensorAttr distributionAttr) {
    const auto distributionMode = distributionAttr.mode().getValue();
    const auto computeShapes = getPerClusterComputeShapes(shape, distributionAttr);

    SmallVector<StridedShape> stridedShapes;
    if (VPU::bitEnumContains(distributionMode, VPU::DistributionMode::DUPLICATED)) {
        for (const auto& computeShape : computeShapes) {
            stridedShapes.emplace_back(computeShape, strides);
        }
        return stridedShapes;
    }

    if (VPU::bitEnumContains(distributionMode, VPU::DistributionMode::SEGMENTED) ||
        VPU::bitEnumContains(distributionMode, VPU::DistributionMode::OVERLAPPED)) {
        const auto memShape = dimsOrder.toMemoryOrder(shape);
        const auto memStrides = dimsOrder.toMemoryOrder(strides);

        // Compute the ratio between the compact and actual strides and apply it to the tiles.
        // For example, a distributed type SEGMENTED over height into 4 tiles with:
        //   shape              [1, 64, 16, 4]
        //   compact strides    [64*4*16, 1, 64*4, 64]
        //   actual strides     [128*4*16, 1, 128*4, 128]
        //   strides multiplier [2, 1, 2, 2]
        // will have the following strided shapes:
        //   shape              [1, 64, 4, 4]
        //   compact strides    [64*4*4, 1, 64*4, 64]
        //   actual strides     [128*4*4, 1, 128*4, 128]
        const auto compactStrideReqs = StrideReqs::compact(dimsOrder.numDims());
        const auto compactMemStrides = compactStrideReqs.calcStrides(elemSize, memShape);
        SmallVector<int64_t> stridesMultiplier(memStrides.size());
        std::transform(memStrides.begin(), memStrides.end(), compactMemStrides.begin(), stridesMultiplier.begin(),
                       [](Bit stride, Bit compactStride) {
                           VPUX_THROW_UNLESS(stride.count() % compactStride.count() == 0,
                                             "Stride {0} is not a multiple of the compact stride {1}", stride.count(),
                                             compactStride.count());
                           return stride.count() / compactStride.count();
                       });

        for (const auto& computeShape : computeShapes) {
            const auto computeMemShape = dimsOrder.toMemoryOrder(Shape(computeShape));
            const auto computeCompactMemStrides = compactStrideReqs.calcStrides(elemSize, computeMemShape);

            SmallVector<Bit> computeMemStrides(computeCompactMemStrides.size());
            for (auto p : zip(computeCompactMemStrides, stridesMultiplier) | indexed) {
                const auto dim = std::get<0>(p.value());
                const auto mul = std::get<1>(p.value());
                computeMemStrides[p.index()] = Bit(dim.count() * mul);
            }
            const auto computeStrides = dimsOrder.toLogicalOrder(MemStrides(computeMemStrides));
            stridedShapes.emplace_back(computeShape, computeStrides);
        }

        return stridedShapes;
    }

    VPUX_THROW("Unsupported mode '{0}'", VPU::stringifyEnum(distributionMode));
}

//
// Generated
//

#include <vpux/compiler/dialect/VPU/generated/attributes/enums.cpp.inc>
#include <vpux/compiler/dialect/VPU/generated/attributes/structs.cpp.inc>
