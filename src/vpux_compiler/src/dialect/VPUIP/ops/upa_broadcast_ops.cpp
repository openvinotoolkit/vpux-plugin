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

#include "vpux/compiler/dialect/VPUIP/ops.hpp"

#include "vpux/compiler/dialect/VPUIP/blob_reader.hpp"
#include "vpux/compiler/utils/error.hpp"

using namespace vpux;

void vpux::VPUIP::BroadcastUPAOp::build(mlir::OpBuilder& builder, mlir::OperationState& state, mlir::Value input,
                                   mlir::Value target_shape, mlir::Value output, IE::BroadcastTypeAttr mode) {
    build(builder, state, input, target_shape, output, mlir::ValueRange{}, mlir::ValueRange{}, mode, nullptr, nullptr);
}

VPUIP::BlobWriter::SpecificTask vpux::VPUIP::BroadcastUPAOp::serialize(VPUIP::BlobWriter& writer) {
    MVCNN::BroadcastParamsBuilder builder(writer);

    // VPUIP::BlobWriter::String mode = writer.createString("NUMPY");

    // switch (this->region()) {
    // case IE::BroadcastType::NUMPY:
    //     region = writer.createString("across");
    //     break;
    // case IE::BroadcastType::same:
    //     region = writer.createString("same");
    //     break;
    // default:
    //     VPUX_THROW("Unsupported LRN_IERegion {0}", this->region());
    // }

    // builder.add_mode(mode);
    const auto paramsOff = builder.Finish();

    return writer.createUPALayerTask(*this, {paramsOff.Union(), MVCNN::SoftwareLayerParams_BroadcastParams});
}
