//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

//

#pragma once

#include "vpux/compiler/core/attributes/dims_order.hpp"
#include "vpux/compiler/core/attributes/indexed_symbol_attr.hpp"
#include "vpux/compiler/core/attributes/shape.hpp"

#include "vpux/utils/core/enums.hpp"
#include "vpux/utils/core/mem_size.hpp"

#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/IR/Value.h>

namespace vpux {

template <typename Enum, typename OutT = mlir::RankedTensorType>
using ranked_tensor_type_if = enable_t<OutT, std::is_enum<Enum>, details::HasStringifyEnum<Enum>>;

template <typename Enum, typename OutT = mlir::MemRefType>
using memref_type_if = enable_t<OutT, std::is_enum<Enum>, details::HasStringifyEnum<Enum>>;

//
// get<scalar>Type
//

mlir::IntegerType getInt4Type(mlir::MLIRContext* ctx);
mlir::IntegerType getInt8Type(mlir::MLIRContext* ctx);
mlir::IntegerType getInt16Type(mlir::MLIRContext* ctx);
mlir::IntegerType getInt32Type(mlir::MLIRContext* ctx);
mlir::IntegerType getInt64Type(mlir::MLIRContext* ctx);

mlir::IntegerType getSInt4Type(mlir::MLIRContext* ctx);
mlir::IntegerType getSInt8Type(mlir::MLIRContext* ctx);
mlir::IntegerType getSInt16Type(mlir::MLIRContext* ctx);
mlir::IntegerType getSInt32Type(mlir::MLIRContext* ctx);
mlir::IntegerType getSInt64Type(mlir::MLIRContext* ctx);

mlir::IntegerType getUInt4Type(mlir::MLIRContext* ctx);
mlir::IntegerType getUInt8Type(mlir::MLIRContext* ctx);
mlir::IntegerType getUInt16Type(mlir::MLIRContext* ctx);
mlir::IntegerType getUInt32Type(mlir::MLIRContext* ctx);
mlir::IntegerType getUInt64Type(mlir::MLIRContext* ctx);
mlir::IntegerType getBool8Type(mlir::MLIRContext* ctx);

//
// TypeSize
//

Bit getElemTypeSize(mlir::Type type);

// Calculates tensor size based on stride information
// Example:
// #map0 = affine_map<(d0, d1, d2, d3) -> (d0 * 768 + d1 * 48 + d2 * 3 + d3)>
// memref<2x3x8x4xf32, #NHWC, #map0>
// result: 768 * 2 * 4 = 6144, where
// 768 - stride, to get the next element by N dim
// 2 - size of N dim, 4 - element size
Byte getTotalSize(mlir::Value val);

// Calculates tensor size ignoring stride information
// Example:
// #map0 = affine_map<(d0, d1, d2, d3) -> (d0 * 768 + d1 * 48 + d2 * 3 + d3)>
// memref<2x3x8x4xui8, #NHWC, #map0>
// result: 2 * 3 * 8 * 4 = 192
Byte getCompactSize(mlir::Value val);

// compute axis permutation
Optional<int32_t> getQuantizedAxis(int32_t axis, ShapeRef prevShape, ShapeRef newShape);

//
// MemRefType utilities
//

mlir::MemRefType getMemRefType(ShapeRef shape, mlir::Type elemType, DimsOrder order, IndexedSymbolAttr memSpace);
template <typename Enum>
memref_type_if<Enum> getMemRefType(ShapeRef shape, mlir::Type elemType, DimsOrder order, Enum kind) {
    return getMemRefType(shape, elemType, order, IndexedSymbolAttr::get(elemType.getContext(), stringifyEnum(kind)));
}

mlir::MemRefType getMemRefType(ShapeRef shape, mlir::Type elemType, DimsOrder order, StridesRef strides,
                               IndexedSymbolAttr memSpace);
template <typename Enum>
memref_type_if<Enum> getMemRefType(ShapeRef shape, mlir::Type elemType, DimsOrder order, StridesRef strides,
                                   Enum kind) {
    return getMemRefType(shape, elemType, order, strides,
                         IndexedSymbolAttr::get(elemType.getContext(), stringifyEnum(kind)));
}

IndexedSymbolAttr getMemorySpace(mlir::MemRefType type);

//
// RankedTensorType utilities
//

mlir::RankedTensorType getTensorType(ShapeRef shape, mlir::Type elemType, DimsOrder order, IndexedSymbolAttr memSpace,
                                     bool sparse = false);

mlir::RankedTensorType changeSparse(mlir::RankedTensorType origType, bool sparse);

mlir::RankedTensorType getDilatedType(mlir::RankedTensorType origType, ShapeRef dilations);

//
// ShapedType utilities
//

mlir::ShapedType changeSparse(mlir::ShapedType origType, bool sparse);

}  // namespace vpux
