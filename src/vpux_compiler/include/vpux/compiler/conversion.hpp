//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

//

#pragma once

#include "vpux/compiler/dialect/IE/ops.hpp"
#include "vpux/compiler/dialect/IERT/ops.hpp"
#include "vpux/compiler/dialect/VPUIP/ops.hpp"
#include "vpux/compiler/dialect/VPURT/ops.hpp"
#include "vpux/compiler/utils/passes.hpp"

#include "vpux/utils/core/logger.hpp"

#include <mlir/Dialect/Quant/QuantOps.h>
#include <mlir/Dialect/StandardOps/IR/Ops.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/Pass/Pass.h>
#include <mlir/Pass/PassManager.h>

#include <memory>

namespace vpux {

//
// LowerIE2VPU
//

std::unique_ptr<mlir::Pass> createConvertIEToVPUNCEPass(Logger log = Logger::global());

//
// LowerIE2IERT
//

//
// Performs full lowering from the IE Dialect to IERT Dialect.
//
// This pipeline performs full IR lowering from IE Dialect to IERT Dialect,
// including Function types, call graph and return operations.
//

void buildLowerIE2IERTPipeline(mlir::OpPassManager& pm, Logger log = Logger::global());

std::unique_ptr<mlir::Pass> createBufferizeIEPass(Logger log = Logger::global());
std::unique_ptr<mlir::Pass> createBufferizeFuncAndReturnPass(Logger log = Logger::global());
std::unique_ptr<mlir::Pass> createAddBuffersForNetResults(Logger log = Logger::global());

//
// Performs partial lowering from the IERT Dialect to VPUIP Dialect.
//
// Converts VPU Operations to VPUIP NCE Operations, where possible.
//

std::unique_ptr<mlir::Pass> createConvertVPUToVPUIPPass(Logger log = Logger::global());
std::unique_ptr<mlir::Pass> createConvertNCEClusterTilingToVPUIPPass(Logger log = Logger::global());

//
// LowerIERT2VPUIP
//

//
// Performs full lowering from the IERT Dialect to VPUIP Dialect.
//
// Replaces Layers with VPUIP UPA/DMA tasks counterparts and declarations with VPUIP analogues.
//

struct LowerIERT2VPUIPOptions : mlir::PassPipelineOptions<LowerIERT2VPUIPOptions> {
    BoolOption enableCompressWeights{*this, "compress-weights", llvm::cl::desc("Enable compress-weights pass"),
                                     llvm::cl::init(false)};

    LowerIERT2VPUIPOptions() = default;

    template <
            class OtherOptions,
            typename = std::enable_if_t<std::is_base_of<mlir::PassPipelineOptions<OtherOptions>, OtherOptions>::value>>
    explicit LowerIERT2VPUIPOptions(const OtherOptions& options) {
        enableCompressWeights = options.enableCompressWeights;
    }
};

void buildLowerIERT2VPUIPPipeline(mlir::OpPassManager& pm, const LowerIERT2VPUIPOptions& options,
                                  Logger log = Logger::global());

std::unique_ptr<mlir::Pass> createConvertSWLayers2VPUIPPass(Logger log = Logger::global());
std::unique_ptr<mlir::Pass> createConvertLayers2VPUIPPass(Logger log = Logger::global());
std::unique_ptr<mlir::Pass> createConvertDeclarations2VPUIPPass(Logger log = Logger::global());
std::unique_ptr<mlir::Pass> createConvertViewOps2VPUIPPass(Logger log = Logger::global());
std::unique_ptr<mlir::Pass> createConvertAsyncOps2VPUIPPass(Logger log = Logger::global());

//
// registerConversionPipelines
//

void registerConversionPipelines();

//
// Generated
//

#define GEN_PASS_CLASSES
#include <vpux/compiler/conversion/generated/passes.hpp.inc>
#undef GEN_PASS_CLASSES

#define GEN_PASS_REGISTRATION
#include <vpux/compiler/conversion/generated/passes.hpp.inc>
#undef GEN_PASS_REGISTRATION

}  // namespace vpux
