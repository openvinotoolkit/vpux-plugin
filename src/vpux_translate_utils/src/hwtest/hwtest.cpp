//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

//

#include "vpux/hwtest/hwtest.hpp"

#include <llvm/Support/ToolOutputFile.h>
#include <mlir/Dialect/Quant/QuantTypes.h>
#include <mlir/IR/Verifier.h>
#include <mlir/Support/DebugStringHelper.h>
#include <mlir/Support/FileUtilities.h>

#include "vpux/compiler/dialect/VPUIP/graph-schema/export.hpp"
#include "vpux/compiler/dialect/VPUIP/ops.hpp"
#include "vpux/compiler/dialect/VPURT/ops.hpp"
#include "vpux/compiler/init.hpp"
#include "vpux/compiler/utils/types.hpp"
#include "vpux/hwtest/hwtest_utils.hpp"
#include "vpux/utils/core/error.hpp"

namespace vpux {

mlir::OwningModuleRef importHWTEST(llvm::StringRef sourceJson, mlir::MLIRContext* ctx) {
    mlir::DialectRegistry registry;
    registerDialects(registry);
    ctx->appendDialectRegistry(registry);
    ctx->loadDialect<VPUIP::VPUIPDialect>();
    ctx->loadDialect<VPURT::VPURTDialect>();

    auto module = mlir::ModuleOp::create(mlir::UnknownLoc::get(ctx), StringRef("mainModule"));
    auto log = Logger{"vpux-hwtest", LogLevel::Trace};
    auto builder = mlir::OpBuilder(module.getBodyRegion());

    nb::TestCaseJsonDescriptor jsonDesc(sourceJson);

    // TODO:
    // This will be handled later based on op type in config json
    auto opType = jsonDesc.getCaseStr();

    bool isDMA = jsonDesc.getCaseType() == nb::CaseType::DMA;
    bool isConv = jsonDesc.getCaseType() == nb::CaseType::ZMajorConvolution;
    bool isDepthwiseConv = jsonDesc.getCaseType() == nb::CaseType::DepthWiseConv;
    bool isSparseConv = jsonDesc.getCaseType() == nb::CaseType::SparseZMajorConvolution;
    bool isEltwiseAdd = jsonDesc.getCaseType() == nb::CaseType::EltwiseAdd;
    bool isMaxPool = jsonDesc.getCaseType() == nb::CaseType::MaxPool;
    bool isEltwiseMult = jsonDesc.getCaseType() == nb::CaseType::EltwiseMult;
    bool isAvgPool = jsonDesc.getCaseType() == nb::CaseType::AvgPool;
    bool isDifferentClustersDPU = jsonDesc.getCaseType() == nb::CaseType::DifferentClustersDPU;
    bool isActShave = jsonDesc.getCaseType() == nb::CaseType::ActShave;
    bool isReadAfterWriteDPUDMA = jsonDesc.getCaseType() == nb::CaseType::ReadAfterWriteDPUDMA;
    bool isReadAfterWriteDMADPU = jsonDesc.getCaseType() == nb::CaseType::ReadAfterWriteDMADPU;
    bool isReadAfterWriteACTDMA = jsonDesc.getCaseType() == nb::CaseType::ReadAfterWriteACTDMA;
    bool isReadAfterWriteDMAACT = jsonDesc.getCaseType() == nb::CaseType::ReadAfterWriteDMAACT;
    bool isReadAfterWriteDPUACT = jsonDesc.getCaseType() == nb::CaseType::ReadAfterWriteDPUACT;
    bool isReadAfterWriteACTDPU = jsonDesc.getCaseType() == nb::CaseType::ReadAfterWriteACTDPU;
    bool isRaceConditionDMA = jsonDesc.getCaseType() == nb::CaseType::RaceConditionDMA;
    bool isRaceConditionDPU = jsonDesc.getCaseType() == nb::CaseType::RaceConditionDPU;
    bool isRaceConditionDPUDMA = jsonDesc.getCaseType() == nb::CaseType::RaceConditionDPUDMA;
    bool isRaceConditionDPUDMAACT = jsonDesc.getCaseType() == nb::CaseType::RaceConditionDPUDMAACT;
    bool isRaceCondition = jsonDesc.getCaseType() == nb::CaseType::RaceCondition;

    auto mainOpJsonDesc = isRaceCondition ? *jsonDesc.getUnderlyingOp() : jsonDesc;

    nb::InputLayer input = mainOpJsonDesc.getInputLayer();
    nb::OutputLayer output = mainOpJsonDesc.getOutputLayer();

    mlir::Type input_type = hwtest::parseInputType(builder, input);
    mlir::Type output_type = hwtest::parseOutputType(builder, output);

    auto weightType = [&]() {
        nb::WeightLayer weight = mainOpJsonDesc.getWeightLayer();
        return hwtest::parseWeightsType(builder, weight);
    };

    auto weightInChannels = [&]() {
        nb::WeightLayer weight = mainOpJsonDesc.getWeightLayer();
        return weight.shape[1];
    };

    if (isDMA) {
        hwtest::buildDMA(jsonDesc, module, builder, log, input_type, output_type);
    } else if (isConv) {
        if (weightInChannels() > 8 * 1024) {
            hwtest::buildContinuedConv(jsonDesc, module, builder, log, input_type, weightType(), output_type);
        } else {
            hwtest::buildSimpleZMajorConv(jsonDesc, module, builder, log, input_type, weightType(), output_type);
        }
    } else if (isSparseConv) {
        hwtest::buildSparseZMajorConv(jsonDesc, module, builder, log, input_type, weightType(), output_type);
    } else if (isDepthwiseConv) {
        hwtest::buildDWConv(jsonDesc, module, builder, log, input_type, weightType(), output_type);
    } else if (isEltwiseAdd) {
        hwtest::buildEltwiseAdd(jsonDesc, module, builder, log, input_type, weightType(), output_type);
    } else if (isEltwiseMult) {
        hwtest::buildEltwiseMultWithDwConv(jsonDesc, module, builder, log, input_type, weightType(), output_type);
    } else if (isMaxPool) {
        hwtest::buildMaxPool(jsonDesc, module, builder, log, input_type, output_type);
    } else if (isAvgPool) {
        // hwtest::buildAvgpoolWithDwConv(jsonDesc, module, builder, log, input_type, output_type);
        hwtest::buildAvgpool(jsonDesc, module, builder, log, input_type, output_type);
    } else if (isDifferentClustersDPU) {
        hwtest::buildDifferentClustersDPUTest(jsonDesc, module, builder, log, input_type, weightType(), output_type);
    } else if (isActShave) {
        hwtest::buildActShave(jsonDesc, module, builder, log, input_type, output_type);
    } else if (isReadAfterWriteDPUDMA) {
        hwtest::buildReadAfterWriteDPUDMATest(jsonDesc, module, builder, log, input_type, weightType(), output_type);
    } else if (isReadAfterWriteDMADPU) {
        hwtest::buildReadAfterWriteDMADPUTest(jsonDesc, module, builder, log, input_type, weightType(), output_type);
    } else if (isReadAfterWriteACTDMA) {
        hwtest::buildReadAfterWriteACTDMATest(jsonDesc, module, builder, log, input_type, output_type);
    } else if (isReadAfterWriteDMAACT) {
        hwtest::buildReadAfterWriteDMAACTTest(jsonDesc, module, builder, log, input_type, output_type);
    } else if (isReadAfterWriteDPUACT) {
        hwtest::buildReadAfterWriteDPUACTTest(jsonDesc, module, builder, log, input_type, weightType(), output_type);
    } else if (isReadAfterWriteACTDPU) {
        hwtest::buildReadAfterWriteACTDPUTest(jsonDesc, module, builder, log, input_type, weightType(), output_type);
    } else if (isRaceConditionDMA) {
        hwtest::buildRaceConditionDMATest(jsonDesc, module, builder, log, input_type, output_type);
    } else if (isRaceConditionDPU) {
        hwtest::buildRaceConditionDPUTest(jsonDesc, module, builder, log, input_type, weightType(), output_type);
    } else if (isRaceConditionDPUDMA) {
        hwtest::buildRaceConditionDPUDMATest(jsonDesc, module, builder, log, input_type, weightType(), output_type);
    } else if (isRaceConditionDPUDMAACT) {
        hwtest::buildRaceConditionDPUDMAACTTest(jsonDesc, module, builder, log, input_type, weightType(), output_type);
    } else if (isRaceCondition) {
        hwtest::buildRaceConditionTest(jsonDesc, module, builder, log, input_type, output_type);
    } else {
        VPUX_THROW("Unknown type: {0}", opType);
    }

    // llvm::dbgs() << "Current module: " << mlir::debugString(module);

    VPUX_THROW_UNLESS(mlir::succeeded(mlir::verify(module)),
                      "Failed to create a valid MLIR module for InferenceEngine IR");

    mlir::DefaultTimingManager tm;
    auto timing = tm.getRootScope();
    const std::vector<std::shared_ptr<const ov::Node>> params;
    const std::vector<std::shared_ptr<const ov::Node>> results;
    auto blob = VPUIP::exportToBlob(module, timing, {}, params, results, log);
    std::string err;
    // dump the blob in a file
    std::unique_ptr<llvm::ToolOutputFile> outFile = mlir::openOutputFile("vpuip.blob", &err);
    outFile->os().write(reinterpret_cast<const char*>(blob.data()), blob.size());
    outFile->keep();
    log.info("Saving blob to {0}", outFile->getFilename());

    return module;
}

}  // namespace vpux
