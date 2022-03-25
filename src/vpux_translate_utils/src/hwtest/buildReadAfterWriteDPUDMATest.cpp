//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0

#include <numeric>

#include <mlir/Dialect/Quant/QuantTypes.h>

#include "vpux/compiler/dialect/VPU/nce_sparsity.hpp"
#include "vpux/compiler/dialect/VPU/passes.hpp"
#include "vpux/compiler/dialect/VPUIP/ops.hpp"
#include "vpux/compiler/dialect/VPUIP/passes.hpp"
#include "vpux/compiler/dialect/VPUIP/utils.hpp"
#include "vpux/compiler/dialect/VPURT/ops.hpp"
#include "vpux/compiler/dialect/VPURT/task.hpp"
#include "vpux/compiler/dialect/const/ops.hpp"
#include "vpux/hwtest/hwtest_utils.hpp"
#include "vpux/hwtest/test_case_json_parser.hpp"
#include "vpux/utils/core/error.hpp"
#include "vpux/utils/core/numeric.hpp"

namespace vpux {
namespace hwtest {

void buildReadAfterWriteDPUDMATest(const nb::TestCaseJsonDescriptor& testDesc, mlir::ModuleOp module,
                                   mlir::OpBuilder builder, Logger& log, mlir::Type inputType, mlir::Type weightsType,
                                   mlir::Type outputType) {
    auto* ctx = builder.getContext();
    auto loc = builder.getUnknownLoc();
    const auto int32 = builder.getIntegerType(32, true);
    const auto overwritingType = int32;
    const auto rewritableType = overwritingType;

    const auto input = testDesc.getInputLayer();
    const auto weights = testDesc.getWeightLayer();
    const auto conv = testDesc.getConvLayer();
    const auto output = testDesc.getOutputLayer();
    const auto iterationCount = testDesc.getIterationCount();
    const auto cluster = testDesc.getClusterNumber();

    const SmallVector<std::int64_t> inputShape{input.shape.begin(), input.shape.end()};
    const SmallVector<std::int64_t> outputShape{output.shape.begin(), output.shape.end()};
    const SmallVector<std::int64_t> weightsShape{weights.shape.begin(), weights.shape.end()};
    const SmallVector<std::int64_t> weightsTableShape{weightsShape[0], 1, 1, 4};
    const SmallVector<std::int64_t> overwritingShape{1, 1, 1, 8};
    const auto rewritableShape = overwritingShape;

    VPUX_THROW_UNLESS(!inputShape.empty(), "buildReadAfterWriteDPUDMATest: Got empty inputShape");
    VPUX_THROW_UNLESS(!outputShape.empty(), "buildReadAfterWriteDPUDMATest: Got empty outputShape");
    VPUX_THROW_UNLESS(!weightsShape.empty(), "buildReadAfterWriteDPUDMATest: Got empty weightsShape");
    VPUX_THROW_UNLESS(!weightsTableShape.empty(), "buildReadAfterWriteDPUDMATest: Got empty weightsTableShape");

    const char* weightsFileName = "weights.dat";

    const auto alignmentRequirement = 16;

    const auto weightsCMXSize = vpux::hwtest::totalTensorSize(weightsShape, weightsType);
    const auto outputCMXSize = vpux::hwtest::totalTensorSize(outputShape, outputType);
    const auto inputCMXSize = vpux::hwtest::totalTensorSize(inputShape, inputType);
    const auto overwritingCMXSize = vpux::hwtest::totalTensorSize(overwritingShape, overwritingType);
    const auto weightsTableShapeCMXSize = sizeof(int) * weightsTableShape[0] * weightsTableShape[3];
    const auto rewritableInputCMXSize = overwritingCMXSize;

    const auto alignment =
            (alignmentRequirement * static_cast<vpux::Bit>(getElemTypeSize(inputType)).count()) / CHAR_BIT;
    const auto WEIGHTS_CMX_OFFSET = 0;
    VPUX_THROW_UNLESS(WEIGHTS_CMX_OFFSET % alignment == 0, "WEIGHTS_CMX_OFFSET must be multiple of {0}, got {1}",
                      alignment, WEIGHTS_CMX_OFFSET);
    const auto OVERWRITING_CMX_OFFSET = 0;
    VPUX_THROW_UNLESS(OVERWRITING_CMX_OFFSET % alignment == 0,
                      "OVERWRITING_CMX_OFFSET must be multiple of {0}, got {1}", alignment, OVERWRITING_CMX_OFFSET);

    const auto WEIGHTSTABLE_CMX_OFFSET = WEIGHTS_CMX_OFFSET + weightsCMXSize;
    VPUX_THROW_UNLESS(WEIGHTSTABLE_CMX_OFFSET % alignment == 0,
                      "WEIGHTSTABLE_CMX_OFFSET must be multiple of {0}, got {1}", alignment, WEIGHTSTABLE_CMX_OFFSET);

    const auto INPUT_CMX_OFFSET = WEIGHTSTABLE_CMX_OFFSET + weightsTableShapeCMXSize;
    VPUX_THROW_UNLESS(INPUT_CMX_OFFSET % alignment == 0, "INPUT_CMX_OFFSET must be multiple of {0}, got {1}", alignment,
                      INPUT_CMX_OFFSET);

    auto REWRITABLE_INPUT_CMX_OFFSET = INPUT_CMX_OFFSET + inputCMXSize - rewritableInputCMXSize;
    VPUX_THROW_UNLESS(REWRITABLE_INPUT_CMX_OFFSET % alignment == 0,
                      "REWRITABLE_INPUT_CMX_OFFSET must be multiple of {0}, got {1}", alignment,
                      REWRITABLE_INPUT_CMX_OFFSET);

    auto OUTPUT_CMX_OFFSET = REWRITABLE_INPUT_CMX_OFFSET + rewritableInputCMXSize;
    VPUX_THROW_UNLESS(OUTPUT_CMX_OFFSET % alignment == 0, "OUTPUT_CMX_OFFSET must be multiple of {0}, got {1}",
                      alignment, OUTPUT_CMX_OFFSET);

    const auto inputParamType =
            getMemRefType(VPURT::BufferSection::NetworkInput, inputShape, inputType, DimsOrder::NHWC);
    const auto outputParamType =
            getMemRefType(vpux::VPURT::BufferSection::NetworkOutput, outputShape, outputType, DimsOrder::NHWC);

    const auto funcType = builder.getFunctionType(SmallVector<mlir::Type>{inputParamType, outputParamType},
                                                  SmallVector<mlir::Type>{outputParamType});

    auto function = builder.create<mlir::FuncOp>(
            loc, llvm::formatv("read_after_write_dpu_dma_{0}_{1}_{2}", inputType, weightsType, outputType).str(),
            funcType, builder.getStringAttr("private"));

    auto functionBuilder = mlir::OpBuilder::atBlockBegin(function.addEntryBlock(), builder.getListener());

    auto functionInput = function.getArgument(0);
    auto functionOutput = function.getArgument(1);

    const auto weightsValues = generateWeights(weightsShape, weightsType, ctx, weightsFileName);
    auto weightsAttribute = vpux::Const::ContentAttr::get(weightsValues);
    weightsAttribute = weightsAttribute.reorder(vpux::DimsOrder::OYXI);

    auto qty = weightsType.dyn_cast<mlir::quant::QuantizedType>();

    if (qty != nullptr) {
        if (qty.getStorageType().isInteger(4)) {
            weightsAttribute = weightsAttribute.bitPack(4);
        }
        weightsAttribute = weightsAttribute.quantCast(qty);
    }

    const auto weightsDDRType =
            getMemRefType(VPURT::BufferSection::Constant, weightsShape, weightsType, DimsOrder::NHWC);

    auto weightsStrides = weightsDDRType.cast<vpux::NDTypeInterface>().getStrides();
    auto inputStrides = vpux::getStrides(functionInput);

    auto weightsCMX = createDeclareTensorOp(functionBuilder, VPURT::BufferSection::CMX_NN, weightsShape, weightsType,
                                            DimsOrder::OYXI, weightsStrides, cluster, WEIGHTS_CMX_OFFSET);
    auto inputCMX = createDeclareTensorOp(functionBuilder, VPURT::BufferSection::CMX_NN, inputShape, inputType,
                                          DimsOrder::NHWC, inputStrides, cluster, INPUT_CMX_OFFSET);
    auto overwritingCMX = createDeclareTensorOp(functionBuilder, VPURT::BufferSection::CMX_NN, overwritingShape,
                                                overwritingType, DimsOrder::NHWC, cluster, OVERWRITING_CMX_OFFSET);
    auto rewritableCMX = createDeclareTensorOp(functionBuilder, VPURT::BufferSection::CMX_NN, rewritableShape,
                                               rewritableType, DimsOrder::NHWC, cluster, REWRITABLE_INPUT_CMX_OFFSET);

    auto weightsDDR = functionBuilder.create<vpux::Const::DeclareOp>(loc, weightsDDRType, weightsAttribute);

    auto outputCMX = createDeclareTensorOp(functionBuilder, VPURT::BufferSection::CMX_NN, outputShape, outputType,
                                           DimsOrder::NHWC, cluster, OUTPUT_CMX_OFFSET);

    auto& weightsOutputChannelsStrideInBits = weightsStrides[vpux::Dims4D::Filter::OC];

    const auto weightsTableDDRType = mlir::RankedTensorType::get(weightsTableShape, int32);
    const auto weightsTable = VPU::NCESparsity::getWeightsTable(
            inputType, outputType, static_cast<std::int32_t>(WEIGHTS_CMX_OFFSET),
            static_cast<std::int32_t>(weightsOutputChannelsStrideInBits.count() / CHAR_BIT),
            static_cast<std::int32_t>(0xFFFFFF), testDesc.getArchitecture(), output.shape[1], weightsType);

    const auto weightsTableDDRMemRef =
            getMemRefType(VPURT::BufferSection::Constant, weightsTableShape, int32, DimsOrder::NHWC);
    const auto weightsTableValues =
            mlir::DenseElementsAttr::get(weightsTableDDRType, llvm::makeArrayRef<std::int32_t>(weightsTable));
    auto weightsTableDDR = functionBuilder.create<vpux::Const::DeclareOp>(
            loc, weightsTableDDRMemRef,
            vpux::Const::ContentAttr::get(weightsTableValues).reorder(vpux::DimsOrder::NHWC));

    auto weightsTableCMX = createDeclareTensorOp(functionBuilder, VPURT::BufferSection::CMX_NN, weightsTableShape,
                                                 int32, DimsOrder::NHWC, cluster, WEIGHTSTABLE_CMX_OFFSET);

    auto updateBarrier = functionBuilder.create<vpux::VPURT::ConfigureBarrierOp>(loc, 0);

    VPURT::wrapIntoTaskOp<VPUIP::NNDMAOp>(functionBuilder, mlir::ValueRange(),
                                          mlir::ValueRange(updateBarrier.barrier()), loc, functionInput,
                                          inputCMX.getOperation()->getResult(0));
    VPURT::wrapIntoTaskOp<VPUIP::NNDMAOp>(
            functionBuilder, mlir::ValueRange(), mlir::ValueRange(updateBarrier.barrier()), loc,
            weightsDDR.getOperation()->getResult(0), weightsCMX.getOperation()->getResult(0));
    VPURT::wrapIntoTaskOp<VPUIP::NNDMAOp>(
            functionBuilder, mlir::ValueRange(), mlir::ValueRange(updateBarrier.barrier()), loc,
            weightsTableDDR.getOperation()->getResult(0), weightsTableCMX.getOperation()->getResult(0));

    auto waitBarrier = updateBarrier;

    const auto strides = getIntArrayAttr(ctx, conv.stride);
    std::vector<std::int64_t> paddings = convertNBPadtoNCETaskPad(conv.pad);
    const auto kernelPaddings = VPU::getPaddingAttr(ctx, paddings[PAD_NCETASK_LEFT], paddings[PAD_NCETASK_RIGHT],
                                                    paddings[PAD_NCETASK_TOP], paddings[PAD_NCETASK_BOTTOM]);
    SmallVector<std::int64_t> kernel = {weightsShape[2], weightsShape[3]};
    const auto kernelSize = getIntArrayAttr(ctx, kernel);

    for (std::size_t i = 1; i + 1 < iterationCount; i += 2) {
        if (i != 1) {
            inputCMX = outputCMX;
            OUTPUT_CMX_OFFSET = OUTPUT_CMX_OFFSET + outputCMXSize;
            VPUX_THROW_UNLESS(OUTPUT_CMX_OFFSET % alignment == 0, "OUTPUT_CMX_OFFSET must be multiple of {0}, got {1}",
                              alignment, OUTPUT_CMX_OFFSET);
            outputCMX = createDeclareTensorOp(functionBuilder, VPURT::BufferSection::CMX_NN, outputShape, outputType,
                                              DimsOrder::NHWC, cluster, OUTPUT_CMX_OFFSET);

            REWRITABLE_INPUT_CMX_OFFSET = OUTPUT_CMX_OFFSET - rewritableInputCMXSize;
            VPUX_THROW_UNLESS(REWRITABLE_INPUT_CMX_OFFSET % alignment == 0,
                              "REWRITABLE_INPUT_CMX_OFFSET must be multiple of {0}, got {1}", alignment,
                              REWRITABLE_INPUT_CMX_OFFSET);
            rewritableCMX =
                    createDeclareTensorOp(functionBuilder, VPURT::BufferSection::CMX_NN, rewritableShape,
                                          rewritableType, DimsOrder::NHWC, cluster, REWRITABLE_INPUT_CMX_OFFSET);
        }
        updateBarrier = functionBuilder.create<vpux::VPURT::ConfigureBarrierOp>(loc, i);
        auto nceTask = VPURT::wrapIntoTaskOp<VPUIP::NCEClusterTaskOp>(
                functionBuilder, mlir::ValueRange(waitBarrier.barrier()), mlir::ValueRange(updateBarrier.barrier()),
                loc, inputCMX.buffer(), weightsCMX.buffer(), weightsTableCMX.buffer(), nullptr, inputCMX.buffer(),
                outputCMX.buffer(), outputCMX.buffer(), vpux::VPUIP::NCETaskType::CONV, kernelSize, strides,
                kernelPaddings, nullptr, nullptr);

        const auto start = getIntArrayAttr(ctx, std::vector<std::int64_t>{0, 0, 0});
        const auto end = getIntArrayAttr(
                ctx, std::vector<std::int64_t>{outputShape[3] - 1, outputShape[2] - 1, outputShape[1] - 1});
        const auto pad = VPU::getPaddingAttr(ctx, paddings[PAD_NCETASK_LEFT], paddings[PAD_NCETASK_RIGHT],
                                             paddings[PAD_NCETASK_TOP], paddings[PAD_NCETASK_BOTTOM]);
        nceTask.addDPUTask(functionBuilder, start, end, pad, conv.cube_mode);

        waitBarrier = updateBarrier;
        updateBarrier = functionBuilder.create<vpux::VPURT::ConfigureBarrierOp>(loc, i + 1);

        VPURT::wrapIntoTaskOp<VPUIP::NNDMAOp>(functionBuilder, mlir::ValueRange(waitBarrier.barrier()),
                                              mlir::ValueRange(updateBarrier.barrier()), loc,
                                              overwritingCMX.getOperation()->getResult(0),
                                              rewritableCMX.getOperation()->getResult(0), cluster, false, false);

        waitBarrier = updateBarrier;
    }

    VPURT::wrapIntoTaskOp<VPUIP::NNDMAOp>(functionBuilder, mlir::ValueRange(waitBarrier.barrier()), mlir::ValueRange(),
                                          loc, outputCMX.getOperation()->getResult(0), functionOutput);

    functionBuilder.create<mlir::ReturnOp>(loc, mlir::ValueRange{functionOutput});

    mlir::PassManager pm(ctx, mlir::OpPassManager::Nesting::Implicit);
    pm.addPass(VPU::createInitCompilerPass(testDesc.getArchitecture(), VPU::CompilationMode::DefaultHW, None, log));
    if (conv.compress) {
        pm.addPass(VPUIP::createCompressWeightsPass(log));
    }

    VPUX_THROW_UNLESS(mlir::succeeded(pm.run(module)), "Compilation failed");

    buildCNNOp(builder, function.getName(),
               {getTensorType(ShapeRef(inputShape), inputType, vpux::DimsOrder::NHWC, nullptr)},
               {getTensorType(ShapeRef(outputShape), outputType, vpux::DimsOrder::NHWC, nullptr)});
}

}  // namespace hwtest
}  // namespace vpux
