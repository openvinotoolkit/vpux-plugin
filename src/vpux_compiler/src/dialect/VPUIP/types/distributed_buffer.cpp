//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

//

#include "vpux/compiler/core/attributes/shape.hpp"
#include "vpux/compiler/dialect/VPUIP/types.hpp"

#include "vpux/compiler/dialect/VPUIP/ops.hpp"

#include <llvm/ADT/TypeSwitch.h>

using namespace vpux;

//
// SubElementTypeInterface
//

void VPUIP::DistributedBufferType::walkImmediateSubElements(llvm::function_ref<void(mlir::Attribute)> walkAttrsFn,
                                                            llvm::function_ref<void(mlir::Type)> walkTypesFn) const {
    walkTypesFn(getElementType());
    if (!getLayout().isIdentity()) {
        walkAttrsFn(getLayout());
    }
    walkAttrsFn(getMemSpace());
    walkAttrsFn(getDistribution());
}

//
// print/parse
//

void VPUIP::DistributedBufferType::print(mlir::DialectAsmPrinter& printer) const {
    printer << getMnemonic() << "<";
    for (auto& dim : getShape()) {
        printer << dim << "x";
    }
    printer << getElementType();

    const auto layout = getLayout();
    if (const auto mapAttr = layout.dyn_cast<mlir::AffineMapAttr>()) {
        printer << ", " << mapAttr;
    } else if (const auto descAttr = layout.dyn_cast<IERT::MemRefAttr>()) {
        printer << ", " << descAttr;
    } else {
        VPUX_THROW("Unsupported MemRefType layout '{0}'", layout);
    }

    printer << ", " << getMemSpace();
    printer << ", {";

    auto distribution = getDistribution();
    printer << "mode = " << VPU::stringifyDistributionMode(distribution.mode().getValue());
    if (distribution.num_tiles() != nullptr) {
        printer << ", num_tiles = " << distribution.num_tiles();
    }
    if (distribution.kernel() != nullptr) {
        printer << ", kernel = " << distribution.kernel();
    }
    if (distribution.pads() != nullptr) {
        printer << ", pads = " << distribution.pads();
    }
    if (distribution.strides() != nullptr) {
        printer << ", strides = " << distribution.strides();
    }
    if (distribution.num_clusters() != nullptr) {
        printer << ", num_clusters = " << distribution.num_clusters();
    }
    if (distribution.alignment() != nullptr) {
        printer << ", alignment = " << distribution.alignment();
    }
    printer << "}";

    printer << ">";
}

mlir::Type VPUIP::DistributedBufferType::parse(mlir::DialectAsmParser& parser) {
    if (parser.parseLess()) {
        return Type();
    }

    SmallVector<int64_t> shape;
    int64_t dim = 0;
    while (parser.parseOptionalInteger(dim).hasValue() && parser.parseXInDimensionList().succeeded()) {
        shape.push_back(dim);
    }

    mlir::Type elemType;
    if (parser.parseType(elemType)) {
        return Type();
    }
    if (parser.parseComma()) {
        return Type();
    }

    mlir::MemRefLayoutAttrInterface layout;

    mlir::AffineMapAttr mapAttr;
    IERT::MemRefAttr memRefAttr;
    if (parser.parseOptionalAttribute(mapAttr).hasValue()) {
        layout = mapAttr;
    } else if (parser.parseOptionalAttribute(memRefAttr).hasValue()) {
        layout = memRefAttr;
    } else {
        return Type();
    }

    if (parser.parseComma()) {
        return Type();
    }

    IndexedSymbolAttr memSpace;
    if (parser.parseAttribute(memSpace)) {
        return Type();
    }
    if (parser.parseComma()) {
        return Type();
    }

    // DistributedTensorAttr

    if (parser.parseLBrace()) {
        return Type();
    }

    // DistributionModeAttr

    if (parser.parseKeyword("mode")) {
        return Type();
    }
    if (parser.parseEqual()) {
        return Type();
    }
    std::string distributionModeStr;
    if (parser.parseKeywordOrString(&distributionModeStr)) {
        return Type();
    }
    const auto distributionMode = VPU::symbolizeDistributionMode(distributionModeStr);
    if (!distributionMode.hasValue()) {
        return Type();
    }
    const auto distributionModeAttr = VPU::DistributionModeAttr::get(parser.getContext(), distributionMode.getValue());

    mlir::ArrayAttr numTiles;
    mlir::ArrayAttr kernel;
    VPU::PaddingAttr pads;
    mlir::ArrayAttr strides;
    mlir::IntegerAttr numClusters;
    mlir::ArrayAttr alignment;

    while (parser.parseOptionalRBrace()) {
        if (parser.parseComma()) {
            return Type();
        }
        std::string attrName;
        if (parser.parseKeywordOrString(&attrName)) {
            return Type();
        }
        if (parser.parseEqual()) {
            return Type();
        }
        if (attrName == "num_tiles") {
            if (parser.parseAttribute(numTiles)) {
                return Type();
            }
        } else if (attrName == "kernel") {
            if (parser.parseAttribute(kernel)) {
                return Type();
            }
        } else if (attrName == "pads") {
            if (parser.parseAttribute(pads)) {
                return Type();
            }
        } else if (attrName == "strides") {
            if (parser.parseAttribute(strides)) {
                return Type();
            }
        } else if (attrName == "num_clusters") {
            if (parser.parseAttribute(numClusters)) {
                return Type();
            }
        } else if (attrName == "alignment") {
            if (parser.parseAttribute(alignment)) {
                return Type();
            }
        } else {
            return Type();
        }
    }

    if (parser.parseGreater()) {
        return Type();
    }
    auto distributedAttr = VPU::DistributedTensorAttr::get(distributionModeAttr, numTiles, kernel, pads, strides,
                                                           numClusters, alignment, parser.getContext());
    return static_cast<mlir::Type>(
            get(parser.getContext(), makeArrayRef(shape), elemType, layout, memSpace, distributedAttr));
}

//
// verify
//
mlir::LogicalResult VPUIP::DistributedBufferType::verify(FuncRef<mlir::InFlightDiagnostic()> emitError,
                                                         ::llvm::ArrayRef<int64_t> shape, mlir::Type /*elementType*/,
                                                         mlir::MemRefLayoutAttrInterface /*layout*/,
                                                         IndexedSymbolAttr /*memSpace*/,
                                                         VPU::DistributedTensorAttr distribution) {
    return VPU::verify(emitError, distribution, shape);
}

//
// getCompactType
//

mlir::MemRefType VPUIP::DistributedBufferType::getCompactType() const {
    return mlir::MemRefType::get(getShape().raw(), getElementType(), getLayout(), getMemSpace());
}

//
// Shape utils
//

// @brief Retrieve the array of compute shapes.
// @warning An important thing to consider with regards to compute shapes,
// is that modes like SEGMENTED and OVERLAPPED take precedence over
// DUPLICATED and MULTICASTED.
// In an example case of a "SEGMENTED | DUPLICATED" (needed for SplitOverK)
// tensor with shape [1, 64, 4, 4], the compute shape in each cluster is
// [1, 16, 4, 4], which is needed when tiling and generating workloads,
// while the allocated shape is [1, 64, 4, 4] (because of duplicated)
// information which is needed for scheduler and strategy manager,
// in order to estimate memory
SmallVector<Shape> VPUIP::DistributedBufferType::getPerClusterComputeShapes() const {
    return VPU::getPerClusterComputeShapes(getShape(), getDistribution());
}

// @brief Retrieve the array of compute buffer offsets with regards to the full buffer.
// @warning An important thing to consider with regards to compute shapes,
// is that modes like SEGMENTED and OVERLAPPED take precedence over
// DUPLICATED and MULTICASTED.
SmallVector<Shape> VPUIP::DistributedBufferType::getPerClusterComputeShapeOffsets() const {
    return VPU::getPerClusterComputeShapeOffsets(getShape(), getDistribution());
}

// @brief Get largest compact compute shape
// @warning This function should not be used for memory size calculation,
// because it does not retrieve the true allocate shape in cases
// of broadcasting.
Shape VPUIP::DistributedBufferType::getLargestCompactShape() const {
    auto tiledComputeShapes = getPerClusterComputeShapes();
    return *std::max_element(tiledComputeShapes.begin(), tiledComputeShapes.end(), [](ShapeRef a, ShapeRef b) {
        return details::calcTotalShapeSize(a.raw()) < details::calcTotalShapeSize(b.raw());
    });
}

// @brief Get the compact compute shape for a specific cluster
// @warning This function should not be used for memory size calculation,
// because it does not retrieve the true allocate shape in cases
// of broadcasting.
Shape VPUIP::DistributedBufferType::getCompactShape(int64_t tileInd) const {
    auto tiledComputeShapes = getPerClusterComputeShapes();
    VPUX_THROW_UNLESS(tileInd < static_cast<int64_t>(tiledComputeShapes.size()),
                      "Requesting tiled shape outside of cluster pool");
    return tiledComputeShapes[tileInd];
}

// @brief Retrieve the array of padding for each cluster
// @warning This function is needed for getting padding in OVERLAPPED mode.
SmallVector<PadInfo> VPUIP::DistributedBufferType::getPerClusterPadding() const {
    return VPU::getPerClusterPadding(getDistribution());
}

// @brief Retrieve the array of strided compute shapes
// @warning This function should not be used for memory size calculation,
// because it does not retrieve the true allocate shape in cases
// of broadcasting.
SmallVector<StridedShape> VPUIP::DistributedBufferType::getPerClusterStridedShapes() const {
    return VPU::getPerClusterStridedShapes(getShape(), getStrides(), getDimsOrder(), getElemTypeSize(),
                                           getDistribution());
}

// @brief Get largest strided compute shape
// @warning This function should not be used for memory size calculation,
// because it does not retrieve the true allocate shape in cases
// of broadcasting.
StridedShape VPUIP::DistributedBufferType::getLargestStridedShape() const {
    const auto stridedShapeSize = [](const StridedShape& stridedShape) {
        return stridedShape.shape.front() * stridedShape.strides.front();
    };

    const auto stridedShapes = getPerClusterStridedShapes();
    VPUX_THROW_UNLESS(!stridedShapes.empty(), "Missing per-cluster strided shapes");
    return *std::max_element(stridedShapes.begin(), stridedShapes.end(),
                             [&](const StridedShape& a, const StridedShape& b) {
                                 return stridedShapeSize(a) < stridedShapeSize(b);
                             });
}

// @brief Get the strided compute shape for a specific cluster
// @warning This function should not be used for memory size calculation,
// because it does not retrieve the true allocate shape in cases
// of broadcasting.
StridedShape VPUIP::DistributedBufferType::getStridedShape(int64_t tileInd) const {
    const auto stridedShapes = getPerClusterStridedShapes();
    VPUX_THROW_UNLESS(tileInd < static_cast<int64_t>(stridedShapes.size()),
                      "Requesting tiled shape outside of cluster pool");
    return stridedShapes[tileInd];
}

//
// NDTypeInterface
//

MemShape VPUIP::DistributedBufferType::getMemShape() const {
    const auto dimsOrder = getDimsOrder();
    const auto shape = getShape();
    return dimsOrder.toMemoryOrder(shape);
}

bool VPUIP::DistributedBufferType::hasRank() const {
    return true;
}

int64_t VPUIP::DistributedBufferType::getRank() const {
    return checked_cast<int64_t>(getShape().size());
}

int64_t VPUIP::DistributedBufferType::getNumElements() const {
    auto shape = getShape().raw();
    VPUX_THROW_UNLESS(!details::isDynamicDimValues(shape), "Cannot get element count of dynamic shaped type");
    return details::calcTotalShapeSize(shape);
}

DimsOrder VPUIP::DistributedBufferType::getDimsOrder() const {
    const auto layout = getLayout();
    if (const auto mapAttr = layout.dyn_cast<mlir::AffineMapAttr>()) {
        return DimsOrder::fromAffineMap(mapAttr.getValue());
    }

    if (const auto descAttr = layout.dyn_cast<IERT::MemRefAttr>()) {
        return DimsOrder::fromAffineMap(descAttr.order().getValue());
    }

    VPUX_THROW("Missing layout information");
}

VPU::MemoryKind VPUIP::DistributedBufferType::getMemoryKind() const {
    const auto memSpace = getMemSpace();
    if (memSpace == nullptr) {
        return VPU::MemoryKind::DDR;
    }

    return VPU::symbolizeEnum<VPU::MemoryKind>(memSpace.getLeafName()).getValue();
}

Strides VPUIP::DistributedBufferType::getStrides() const {
    const auto layout = getLayout();
    if (const auto mapAttr = layout.dyn_cast<mlir::AffineMapAttr>()) {
        VPUX_THROW_UNLESS(mapAttr.getValue().isPermutation(), "Got non permutation layout attribute '{0}'", layout);

        // Missing strides specification means compact strides.
        const auto order = getDimsOrder();
        const auto memShape = getMemShape();
        const auto memStrides = StrideReqs::compact(order.numDims()).calcStrides(getElemTypeSize(), memShape);

        return order.toLogicalOrder(memStrides);
    }

    if (const auto descAttr = layout.dyn_cast<IERT::MemRefAttr>()) {
        const auto elemStrides = parseIntArrayAttr<int64_t>(descAttr.strides());
        const auto elemSize = getElemTypeSize();

        return Strides(to_small_vector(elemStrides | transformed([&](int64_t stride) {
                                           return stride * elemSize;
                                       })));
    }

    VPUX_THROW("Unsupported layout attribute type '{0}'", layout);
}

MemStrides VPUIP::DistributedBufferType::getMemStrides() const {
    const auto order = getDimsOrder();
    const auto strides = getStrides();
    return order.toMemoryOrder(strides);
}

Bit VPUIP::DistributedBufferType::getElemTypeSize() const {
    return vpux::getElemTypeSize(getElementType());
}

Byte VPUIP::DistributedBufferType::getTotalAllocSize() const {
    return getCompactAllocSize();
}

Byte VPUIP::DistributedBufferType::getCompactAllocSize() const {
    auto shape = getShape();
    const auto distribution = getDistribution();
    const auto distributionMode = distribution.mode();

    // DUPLICATED|MULTICASTED takes priority since it means that each cluster will have the entire
    // tensor, regardless whether it's tiled or not.
    Shape tiledShape;
    if (VPU::bitEnumContains(distributionMode.getValue(), VPU::DistributionMode::DUPLICATED) ||
        VPU::bitEnumContains(distributionMode.getValue(), VPU::DistributionMode::MULTICASTED)) {
        tiledShape = Shape(shape.raw());
    } else if (VPU::bitEnumContains(distributionMode.getValue(), VPU::DistributionMode::SEGMENTED) ||
               VPU::bitEnumContains(distributionMode.getValue(), VPU::DistributionMode::OVERLAPPED)) {
        tiledShape = getLargestCompactShape();
    } else {
        // No distribution mode.
        tiledShape = Shape(shape.raw());
    }

    if (distribution.alignment() != nullptr) {
        const auto alignment = parseIntArrayAttr<int64_t>(distribution.alignment());
        const auto optionalAlignment = Optional<ArrayRef<int64_t>>(alignment);
        tiledShape = Shape(alignShape(tiledShape.raw(), optionalAlignment));
    }

    return Byte(getElemTypeSize()) * details::calcTotalShapeSize(tiledShape.raw());
}

NDTypeInterface VPUIP::DistributedBufferType::changeShape(ShapeRef /*shape*/) const {
    VPUX_THROW("changeShape method is not implemented for DistributedBufferType");
}

NDTypeInterface VPUIP::DistributedBufferType::changeElemType(mlir::Type /*elemType*/) const {
    VPUX_THROW("changeElemType method is not implemented for DistributedBufferType");
}

NDTypeInterface VPUIP::DistributedBufferType::changeShapeElemType(ShapeRef /*shape*/, mlir::Type /*elemType*/) const {
    VPUX_THROW("changeShapeElemType method is not implemented for DistributedBufferType");
}

NDTypeInterface VPUIP::DistributedBufferType::changeDimsOrder(DimsOrder /*order*/) const {
    VPUX_THROW("changeDimsOrder method is not implemented for DistributedBufferType");
}

NDTypeInterface VPUIP::DistributedBufferType::changeMemSpace(IndexedSymbolAttr /*memSpace*/) const {
    VPUX_THROW("changeMemSpace method is not implemented for DistributedBufferType");
}

NDTypeInterface VPUIP::DistributedBufferType::changeStrides(StridesRef strides) const {
    const auto ctx = getContext();
    const auto elemSize = getElemTypeSize().count();
    const auto order = mlir::AffineMapAttr::get(getDimsOrder().toAffineMap(ctx));
    const auto newStrides = to_small_vector(strides | transformed([&](Bit stride) {
                                                return stride.count() / elemSize;
                                            }));
    const auto newStridesAttr = getIntArrayAttr(ctx, newStrides);
    const auto newDescAttr = IERT::MemRefAttr::get(order, newStridesAttr, ctx);
    return VPUIP::DistributedBufferType::get(ctx, getShape().raw(), getElementType(), newDescAttr, getMemSpace(),
                                             getDistribution());
}

NDTypeInterface VPUIP::DistributedBufferType::extractDenseTile(ShapeRef /*tileOffsets*/, ShapeRef /*tileShape*/) const {
    VPUX_THROW("extractDenseTile method is not implemented for DistributedBufferType");
}

NDTypeInterface VPUIP::DistributedBufferType::extractViewTile(vpux::ShapeRef /*tileOffsets*/,
                                                              vpux::ShapeRef /*tileShape*/,
                                                              vpux::ShapeRef /*tileElemStrides*/) const {
    VPUX_THROW("extractViewTile method is not implemented for DistributedBufferType");
}

NDTypeInterface VPUIP::DistributedBufferType::eraseTiledInfo() const {
    VPUX_THROW("eraseTiledInfo method is not implemented for DistributedBufferType");
}

NDTypeInterface VPUIP::DistributedBufferType::pad(ShapeRef /*padBefore*/, ShapeRef /*padAfter*/) const {
    VPUX_THROW("pad method is not implemented for DistributedBufferType");
}
