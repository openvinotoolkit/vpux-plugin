//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

#include "vpux/compiler/pipelines.hpp"

#include "vpux/compiler/conversion.hpp"
#include "vpux/compiler/core/passes.hpp"
#include "vpux/compiler/dialect/IE/passes.hpp"
#include "vpux/compiler/dialect/IERT/passes.hpp"
#include "vpux/compiler/dialect/VPU/passes.hpp"
#include "vpux/compiler/dialect/VPUIP/passes.hpp"
#include "vpux/compiler/dialect/VPURT/passes.hpp"
#include "vpux/compiler/dialect/const/passes.hpp"
#include "vpux/compiler/utils/rewriter.hpp"

#include "vpux/utils/core/optional.hpp"

#include <mlir/Pass/PassManager.h>
#include <mlir/Transforms/Passes.h>

using namespace vpux;

//
// Common utilities
//

namespace {

//
// Common utilities
//

template <VPU::MemoryKind KIND>
mlir::Optional<VPU::MemoryKind> getMemKind(StringRef) {
    return KIND;
}

VPU::ArchKind getArchKind(const StrOption& archKind) {
    VPUX_THROW_UNLESS(archKind.hasValue(), "Platform architecture is not provided. Please try 'vpu-arch=VPUX30XX'");
    const auto archKindStr = archKind.getValue();
    const auto parsed = VPU::symbolizeArchKind(archKindStr);
    VPUX_THROW_UNLESS(parsed.hasValue(), "Unsupported platform architecture '{0}'", archKindStr);
    return parsed.getValue();
}

Optional<int> getNumOfDPUGroups(const IntOption& numOfDPUGroups) {
    if (numOfDPUGroups.hasValue()) {
        return numOfDPUGroups.getValue();
    }
    return None;
}

}  // namespace

//
// ReferenceSWMode
//

void vpux::buildReferenceSWModePipeline(mlir::OpPassManager& pm, const ReferenceSWOptions& options, Logger log) {
    const auto grc = getDefaultGreedyRewriteConfig();

    // Level 3 : Topology

    pm.addPass(IE::createMatMulInputsTo2dPass(log));
    pm.addPass(mlir::createCanonicalizerPass(grc));

    IE::buildAdjustPrecisionPipeline(pm, IE::AdjustPrecisionOptions(options), log);

    IE::buildAdjustForVPUPipeline(pm, IE::AdjustForVPUOptions(options), log);

    pm.addPass(IE::createSplitFakeQuantPass(log));
    pm.addPass(mlir::createCanonicalizerPass(grc));
    pm.addPass(IE::createDequantizeConstPass(log));
    pm.addPass(IE::createMergeFakeQuantPass(log));
    pm.addPass(mlir::createCanonicalizerPass(grc));

    IE::buildAdjustLayoutPipeline(pm, IE::AdjustLayoutOptions(options), log);

    pm.addPass(IE::createConvertToMemPermutePass(log));
    pm.addPass(IE::createConvertReduceToPoolingPass(log));
    pm.addPass(mlir::createCanonicalizerPass(grc));

    // Lowering

    buildLowerIE2IERTPipeline(pm, log);

    pm.addPass(createConvertSWLayers2VPUIPPass(log));

    // Level 2 : Abstract RunTime

    pm.addPass(IERT::createSetInternalMemorySpacePass(getMemKind<VPU::MemoryKind::DDR>, log));

    pm.addPass(IERT::createCopyOpTilingPass(log));
    pm.addPass(mlir::createCanonicalizerPass(grc));

    IERT::buildAsyncSchedulingPipeline(pm, log);

    pm.addPass(IERT::createStaticAllocationPass(getMemKind<VPU::MemoryKind::DDR>, log));
    pm.addPass(IERT::createLinearizationPass(log));
    pm.addPass(IERT::createOptimizeAsyncDepsPass(log));

    pm.addPass(IERT::createBreakDataFlowPass(log));
    pm.addPass(IERT::createConvertScalarToTensorPass(log));

    // Lowering

    buildLowerIERT2VPUIPPipeline(pm, LowerIERT2VPUIPOptions(options), log);

    // Level 1 : VPU RunTime

    if (options.enableProfiling) {
        if (options.enableSWProfiling) {
            pm.addPass(VPUIP::createUPAProfilingPass(log));
        }
        pm.addPass(VPUIP::createGroupProfilingBuffersPass(log));
        pm.addPass(createMoveDeclarationsToTopPass(log));
    }

    pm.addPass(VPURT::createAssignPhysicalBarriersPass(log));
    pm.addPass(VPURT::createBarrierSimulationPass(log));
    pm.addPass(VPUIP::createDumpStatisticsOfTaskOpsPass(log));
    pm.addPass(Const::createConstantFoldingPass());
}

//
// ReferenceHWMode
//

void vpux::buildReferenceHWModePipeline(mlir::OpPassManager& pm, const ReferenceHWOptions& options, Logger log) {
    const auto grc = getDefaultGreedyRewriteConfig();

    // Level 3 : Topology

    pm.addPass(IE::createMatMulInputsTo2dPass(log));
    pm.addPass(mlir::createCanonicalizerPass(grc));

    IE::buildAdjustPrecisionPipeline(pm, IE::AdjustPrecisionOptions(options), log);

    pm.addPass(IE::createUnrollBatchPass(log));

    if (options.enableConvertFCToConv) {
        pm.addPass(IE::createConvertFCToConvPass(log));
    }
    pm.addPass(IE::createConvertReduceToPoolingPass(log));
    pm.addPass(IE::createHandleLargeKernelsPass(log));
    if (options.enableConvertAvgPoolToDWConv) {
        pm.addPass(IE::createConvertAvgPoolToDWConvPass(log));
    }

    IE::buildAdjustForVPUPipeline(pm, IE::AdjustForVPUOptions(options), log);

    if (options.enableSwapTransposeWithFQ) {
        pm.addPass(IE::createSwapTransposeWithFQPass(log));
    }
    if (options.enableConvertScaleShiftDW) {
        pm.addPass(IE::createConvertScaleShiftToDWPass(log));
    }
    if (options.enableSplitConvWithMultipleFQ) {
        pm.addPass(IE::createSplitConvWithMultipleFQPass(log));
    }
    pm.addPass(mlir::createCanonicalizerPass(grc));

    if (options.enableHandleLargeStrides) {
        pm.addPass(IE::createHandleLargeStridesPass(log));
    }
    if (options.enableHandleAsymmetricStrides) {
        pm.addPass(IE::createHandleAsymmetricStridesPass(log));
    }

    if (options.enableLowPrecision) {
        IE::buildLowPrecisionPipeline(pm, IE::LowPrecisionOptions(options), log);
    }
    pm.addPass(IE::createFusePostOpsPass(log));
    pm.addPass(IE::createResolvePWLPostOpsPass(log));

    IE::buildAdjustLayoutPipeline(pm, IE::AdjustLayoutOptions(options), log);

    if (options.enableExpandActivationChannels) {
        pm.addPass(IE::createExpandActivationChannelsPass(log));
        pm.addPass(mlir::createCanonicalizerPass(grc));

        if (options.enableOptimizeReorders) {
            pm.addPass(IE::createOptimizeReordersPass(log));
            pm.addPass(IE::createUniquifyOpsPass(log));
        }
    }

    pm.addPass(IE::createConvertToMemPermutePass(log));
    pm.addPass(mlir::createCanonicalizerPass(grc));

    pm.addPass(createConvertIEToVPUNCEPass(log));

    pm.addPass(IE::createIsolatedTilingPass(log));
    pm.addPass(mlir::createCanonicalizerPass(grc));

    pm.addPass(VPU::createAdjustMemorySpacePass(log));
    pm.addPass(mlir::createCanonicalizerPass(grc));

    pm.addPass(VPU::createSplitNCEOpsOntoWorkloadsPass(log));

    // Lowering

    buildLowerIE2IERTPipeline(pm, log);

    pm.addPass(createConvertVPUToVPUIPPass(log));
    pm.addPass(createConvertNCEClusterTilingToVPUIPPass(log));
    pm.addPass(createConvertSWLayers2VPUIPPass(log));
    pm.addPass(mlir::createCanonicalizerPass(grc));

    // Level 2 : Abstract RunTime

    pm.addPass(IERT::createSetInternalMemorySpacePass(getMemKind<VPU::MemoryKind::DDR>, log));

    pm.addPass(IERT::createCopyOpTilingPass(log));
    pm.addPass(mlir::createCanonicalizerPass(grc));

    if (options.enableProfiling && options.enableDPUProfiling) {
        pm.addPass(IERT::createDPUProfilingPass(getMemKind<VPU::MemoryKind::CMX_NN>, log));
    }

    IERT::buildAsyncSchedulingPipeline(pm, log);

    if (options.enableProfiling && options.enableDMAProfiling) {
        pm.addPass(IERT::createDMATaskProfilingPass(getMemKind<VPU::MemoryKind::CMX_NN>, log));
    }

    pm.addPass(IERT::createStaticAllocationPass(getMemKind<VPU::MemoryKind::CMX_NN>, log));
    pm.addPass(IERT::createStaticAllocationPass(getMemKind<VPU::MemoryKind::DDR>, log));
    pm.addPass(IERT::createLinearizationPass(log));
    pm.addPass(IERT::createOptimizeAsyncDepsPass(log));

    pm.addPass(IERT::createBreakDataFlowPass(log));
    pm.addPass(IERT::createConvertScalarToTensorPass(log));

    // Lowering

    buildLowerIERT2VPUIPPipeline(pm, LowerIERT2VPUIPOptions(options), log);
    pm.addPass(IERT::createPatchWeightsTablePass(log));

    // Level 1 : VPU RunTime

    if (options.enableProfiling) {
        if (options.enableSWProfiling) {
            pm.addPass(VPUIP::createUPAProfilingPass(log));
        }
        pm.addPass(VPUIP::createGroupProfilingBuffersPass(log));
    }
    pm.addPass(VPURT::createAssignPhysicalBarriersPass(log));
    pm.addPass(VPURT::createBarrierSimulationPass(log));
    pm.addPass(VPUIP::createDumpStatisticsOfTaskOpsPass(log));
    pm.addPass(Const::createConstantFoldingPass());
}

//
// DefaultHWMode
//

void vpux::buildDefaultHWModePipeline(mlir::OpPassManager& pm, const DefaultHWOptions& options, Logger log) {
    const auto grc = getDefaultGreedyRewriteConfig();

    // Level 3 : Topology

    pm.addPass(IE::createMatMulInputsTo2dPass(log));
    pm.addPass(mlir::createCanonicalizerPass(grc));

    IE::buildAdjustPrecisionPipeline(pm, IE::AdjustPrecisionOptions(options), log);

    pm.addPass(IE::createUnrollBatchPass(log));
    if (options.enableConvertFCToConv) {
        pm.addPass(IE::createConvertFCToConvPass(log));
    }
    pm.addPass(IE::createConvertReduceToPoolingPass(log));
    pm.addPass(IE::createHandleLargeKernelsPass(log));
    if (options.enableConvertAvgPoolToDWConv) {
        pm.addPass(IE::createConvertAvgPoolToDWConvPass(log));
    }

    IE::buildAdjustForVPUPipeline(pm, IE::AdjustForVPUOptions(options), log);

    if (options.enableSwapTransposeWithFQ) {
        pm.addPass(IE::createSwapTransposeWithFQPass(log));
    }
    if (options.enableConvertScaleShiftDW) {
        pm.addPass(IE::createConvertScaleShiftToDWPass(log));
    }
    if (options.enableSplitConvWithMultipleFQ) {
        pm.addPass(IE::createSplitConvWithMultipleFQPass(log));
    }
    pm.addPass(mlir::createCanonicalizerPass(grc));

    if (options.enableHandleLargeStrides) {
        pm.addPass(IE::createHandleLargeStridesPass(log));
    }
    if (options.enableHandleAsymmetricStrides) {
        pm.addPass(IE::createHandleAsymmetricStridesPass(log));
    }

    if (options.enableLowPrecision) {
        IE::buildLowPrecisionPipeline(pm, IE::LowPrecisionOptions(options), log);
    }
    pm.addPass(IE::createFusePostOpsPass(log));
    pm.addPass(IE::createUnrollBatchPass(log));
    pm.addPass(IE::createResolvePWLPostOpsPass(log));

    if (options.enableUpstreamSlice) {
        pm.addPass(IE::createUpstreamSlicePass(log));
    }

    IE::buildAdjustLayoutPipeline(pm, IE::AdjustLayoutOptions(options), log);

    if (options.enableExpandActivationChannels) {
        pm.addPass(IE::createExpandActivationChannelsPass(log));
        pm.addPass(mlir::createCanonicalizerPass(grc));

        if (options.enableOptimizeReorders) {
            pm.addPass(IE::createOptimizeReordersPass(log));
            pm.addPass(IE::createUniquifyOpsPass(log));
        }
    }

    pm.addPass(IE::createConvertToMemPermutePass(log));
    pm.addPass(mlir::createCanonicalizerPass(grc));

    pm.addPass(createConvertIEToVPUNCEPass(log));
    pm.addPass(VPU::createMultiClusterStrategyAssignmentPass(log));

    // manual strategy debug configuration
    bool writeStrategyToJSON = false;
    StringRef writeStrategyFileLocation = "strategy_out.json";
    bool readStrategyFromJSON = false;
    StringRef readStrategyFileLocation = "strategy_in.json";

    pm.addPass(VPU::createManualStrategyUtilsPass(writeStrategyToJSON, writeStrategyFileLocation, readStrategyFromJSON,
                                                  readStrategyFileLocation, log));

    pm.addPass(VPU::createWrapVPUOpsInNCEClusterTilingPass(log));

    pm.addPass(IE::createManualTilingPass(log));
    pm.addPass(IE::createPrefetchTilingPass(log));
    pm.addPass(mlir::createCanonicalizerPass(grc));

    pm.addPass(VPU::createManualStrategyUtilsPass(writeStrategyToJSON, writeStrategyFileLocation, readStrategyFromJSON,
                                                  readStrategyFileLocation, log));

    pm.addPass(VPU::createAdjustMemorySpacePass(log));
    pm.addPass(mlir::createCanonicalizerPass(grc));

    pm.addPass(VPU::createCMXConcatPass(log));
    pm.addPass(mlir::createCanonicalizerPass(grc));

    pm.addPass(VPU::createSplitNCEOpsOntoWorkloadsPass(log));

    // Lowering

    buildLowerIE2IERTPipeline(pm, log);

    pm.addPass(createConvertVPUToVPUIPPass(log));
    pm.addPass(createConvertNCEClusterTilingToVPUIPPass(log));
    pm.addPass(createConvertSWLayers2VPUIPPass(log));

    pm.addPass(mlir::createCanonicalizerPass(grc));

    // Level 2 : Abstract RunTime

    pm.addPass(IERT::createSetInternalMemorySpacePass(getMemKind<VPU::MemoryKind::DDR>, log));

    if (options.enableOptimizeCopies) {
        pm.addPass(IERT::createOptimizeCopiesPass(log));
        pm.addPass(IERT::createCopyOpHoistingPass(log));
        pm.addPass(IERT::createOptimizeParallelCopiesPass(log));
    }

    pm.addPass(IERT::createCopyOpTilingPass(log));
    pm.addPass(mlir::createCanonicalizerPass(grc));

    if (options.enableProfiling && options.enableDPUProfiling) {
        pm.addPass(IERT::createDPUProfilingPass(getMemKind<VPU::MemoryKind::CMX_NN>, log));
    }

    IERT::buildAsyncSchedulingPipeline(pm, log);

    if (options.enableProfiling && options.enableDMAProfiling) {
        pm.addPass(IERT::createDMATaskProfilingPass(getMemKind<VPU::MemoryKind::CMX_NN>, log));
    }

    pm.addPass(IERT::createFeasibleAllocationPass(getMemKind<VPU::MemoryKind::CMX_NN>, getMemKind<VPU::MemoryKind::DDR>,
                                                  log));

    if (options.enableGroupAsyncExecuteOps) {
        pm.addPass(IERT::createGroupAsyncExecuteOpsPass(log));
    }

    pm.addPass(IERT::createStaticAllocationPass(getMemKind<VPU::MemoryKind::DDR>, log));
    pm.addPass(IERT::createOptimizeAsyncDepsPass(log));

    pm.addPass(IERT::createBreakDataFlowPass(log));
    pm.addPass(IERT::createConvertScalarToTensorPass(log));

    buildLowerIERT2VPUIPPipeline(pm, LowerIERT2VPUIPOptions(options), log);
    // Handle WeightsTable, which requires statically allocated memory
    pm.addPass(IERT::createPatchWeightsTablePass(log));

    // Level 1 : VPU RunTime

    if (options.enableProfiling) {
        if (options.enableSWProfiling) {
            pm.addPass(VPUIP::createUPAProfilingPass(log));
        }
        pm.addPass(VPUIP::createGroupProfilingBuffersPass(log));
        pm.addPass(createMoveDeclarationsToTopPass(log));
    }

    pm.addPass(VPURT::createAssignVirtualBarriersPass(log));
    pm.addPass(VPURT::createAssignPhysicalBarriersPass(log));
    pm.addPass(VPURT::createBarrierSimulationPass(log));
    pm.addPass(VPUIP::createUnrollClusterTilingPass(log));
    pm.addPass(VPUIP::createDumpStatisticsOfTaskOpsPass(log));
    pm.addPass(Const::createConstantFoldingPass());
}

//
// registerPipelines
//

void vpux::registerPipelines() {
    mlir::PassPipelineRegistration<ReferenceSWOptions>(
            "reference-sw-mode", "Compile IE Network in Reference Software mode (SW only execution)",
            [](mlir::OpPassManager& pm, const ReferenceSWOptions& options) {
                const auto archKind = getArchKind(options.arch);
                pm.addPass(VPU::createInitCompilerPass(archKind, VPU::CompilationMode::ReferenceSW));

                buildReferenceSWModePipeline(pm, options);
            });

    mlir::PassPipelineRegistration<ReferenceHWOptions>(
            "reference-hw-mode", "Compile IE Network in Reference Hardware mode (HW and SW execution)",
            [](mlir::OpPassManager& pm, const ReferenceHWOptions& options) {
                const auto archKind = getArchKind(options.arch);
                const auto numOfDPUGroups = getNumOfDPUGroups(options.numberOfDPUGroups);
                pm.addPass(VPU::createInitCompilerPass(archKind, VPU::CompilationMode::ReferenceHW, numOfDPUGroups));

                buildReferenceHWModePipeline(pm, options);
            });

    mlir::PassPipelineRegistration<DefaultHWOptions>(
            "default-hw-mode", "Compile IE Network in Default Hardware mode (HW and SW execution)",
            [](mlir::OpPassManager& pm, const DefaultHWOptions& options) {
                const auto archKind = getArchKind(options.arch);
                const auto numOfDPUGroups = getNumOfDPUGroups(options.numberOfDPUGroups);
                pm.addPass(VPU::createInitCompilerPass(archKind, VPU::CompilationMode::DefaultHW, numOfDPUGroups));

                buildDefaultHWModePipeline(pm, options);
            });
}
