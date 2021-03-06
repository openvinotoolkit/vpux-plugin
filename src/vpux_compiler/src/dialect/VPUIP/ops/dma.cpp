//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

//

#include "vpux/compiler/dialect/VPUIP/ops.hpp"
#include "vpux/compiler/dialect/VPURT/types.hpp"
#include "vpux/compiler/utils/error.hpp"

#include "vpux/utils/core/checked_cast.hpp"

#include <mlir/IR/BuiltinTypes.h>

using namespace vpux;

namespace {

mlir::LogicalResult verifyTensorSize(mlir::Location loc, mlir::Value tensor) {
    const auto size = static_cast<Byte>(getCompactSize(tensor));

    if (size <= VPUIP::DMA_LIMIT) {
        return mlir::success();
    }

    return errorAt(loc, "The size of the DMA transaction {0} for a {1} tensor is greater than the limit {2}", size,
                   getShape(tensor), VPUIP::DMA_LIMIT);
}

}  // namespace

//
// NNDMAOp
//

void vpux::VPUIP::NNDMAOp::build(mlir::OpBuilder& builder, mlir::OperationState& state, mlir::Value input,
                                 mlir::Value output_buff) {
    build(builder, state, input, output_buff, /*port=*/0, /*is_out_of_order=*/false, /*is_critical=*/false);
}

VPUIP::BlobWriter::SpecificTask vpux::VPUIP::NNDMAOp::serialize(VPUIP::BlobWriter& writer) {
    const auto srcOff = writer.getTensorRef(input());
    const auto dstOff = writer.getTensorRef(output_buff());

    MVCNN::NNDMATaskBuilder builder(writer);
    builder.add_src(srcOff);
    builder.add_dst(dstOff);
    builder.add_port(checked_cast<uint8_t>(port()));
    builder.add_set_ord(static_cast<uint8_t>(!is_out_of_order()));  // ORD
    builder.add_set_crit(static_cast<uint8_t>(is_critical()));      // CRIT
    return {builder.Finish().Union(), MVCNN::SpecificTask_NNDMATask};
}

mlir::LogicalResult vpux::VPUIP::verifyOp(NNDMAOp op) {
    auto loc = op.getLoc();

    if (auto distributedOut = op.output().getType().dyn_cast<VPUIP::DistributedBufferType>()) {
        auto mode = distributedOut.getDistribution().mode().getValue();
        // In this case DUPLICATED|SEGMENTED is an alias of DUPLICATED mode
        if (mode != VPU::DistributionMode::DUPLICATED &&
            mode != (VPU::DistributionMode::DUPLICATED | VPU::DistributionMode::SEGMENTED)) {
            return errorAt(loc, "Only duplicated mode supported for output operand. Got: {0}",
                           VPU::stringifyDistributionMode(mode));
        }
    }

    return verifyTensorSize(loc, op.input());
}

//
// CompressedDMAOp
//

void vpux::VPUIP::CompressedDMAOp::build(mlir::OpBuilder& builder, mlir::OperationState& state, mlir::Value input,
                                         mlir::Value output_buff) {
    build(builder, state, input, output_buff, /*port=*/0, /*is_out_of_order=*/false, /*is_critical=*/false);
}

VPUIP::BlobWriter::SpecificTask vpux::VPUIP::CompressedDMAOp::serialize(VPUIP::BlobWriter& writer) {
    const auto srcOff = writer.getTensorRef(input());
    const auto dstOff = writer.getTensorRef(output_buff());

    MVCNN::NNDMATaskBuilder builder(writer);
    builder.add_src(srcOff);
    builder.add_dst(dstOff);
    builder.add_compression(true);
    builder.add_port(checked_cast<uint8_t>(port()));
    builder.add_set_ord(static_cast<uint8_t>(!is_out_of_order()));  // ORD
    builder.add_set_crit(static_cast<uint8_t>(is_critical()));      // CRIT
    return {builder.Finish().Union(), MVCNN::SpecificTask_NNDMATask};
}
