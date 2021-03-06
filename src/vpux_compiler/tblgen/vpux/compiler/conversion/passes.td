//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

//

#ifndef VPUX_COMPILER_CONVERSION_PASSES
#define VPUX_COMPILER_CONVERSION_PASSES

include "mlir/Pass/PassBase.td"

//=================================================================================
// LowerIE2VPU
//=================================================================================

//
// ConvertIEToVPUNCE
//

def ConvertIEToVPUNCE : PassBase<"convert-IE-to-VPU-NCE", "vpux::FunctionPass"> {
    let summary = "Convert supported IE layers to VPU NCE operations";

    let description = [{
        The pass converts VPUNCE-friendly operations (such as Conv, Pool, Eltwise)

        It doesn't change the memory spaces or add any new operations such as Copy
    }];

    let constructor = "vpux::createConvertIEToVPUNCEPass()";

    let dependentDialects = [
        "vpux::VPU::VPUDialect"
    ];
}

//=================================================================================
// LowerIE2IERT
//=================================================================================

//
// BufferizeIE
//

def BufferizeIE : PassBase<"bufferize-IE", "vpux::FunctionPass"> {
    let summary = "Bufferize the IE dialect into the IERT dialect on Function level";

    let description = [{
        This pass bufferizes **IE Dialect** into **IERT Dialect**:

        * Updates only Function inner regions.
        * Doesn't change Function signatures.
        * Replaces Layer Operations with IERT analogues.
        * Replaces Quant Dialect Operations with IERT analogues.
    }];

    let constructor = "vpux::createBufferizeIEPass()";

    let dependentDialects = [
        "vpux::IERT::IERTDialect",
        "mlir::StandardOpsDialect",
        "mlir::memref::MemRefDialect"
    ];
}

//
// BufferizeFuncAndReturn
//

def BufferizeFuncAndReturn : PassBase<"bufferize-func-and-return", "vpux::ModulePass"> {
    let summary = "Bufferize func/return ops";

    let description = [{
        A bufferize pass that bufferizes std.func ops.
        In contrast to MLIR standard analogue pass, this pass uses vpux::BufferizeTypeConverter
        to process encoding attribute in mlir::RankedTensorType
    }];

    let constructor = "vpux::createBufferizeFuncAndReturnPass()";
}

//
// AddBuffersForNetResults
//

def AddBuffersForNetResults : PassBase<"add-buffers-for-net-results", "vpux::ModulePass"> {
    let summary = "Add network results in Function parameters";

    let description = [{
        This pass adds buffers to the function parameters to copy the network result to them.
        In contrast to MLIR standard analogue pass, this pass preserves the function results to utilize use-def chains on bufferized IR.
        The `return` operation will take an aliases of output buffers from the function arguments.
    }];

    let constructor = "vpux::createAddBuffersForNetResults()";

    let dependentDialects = [
        "vpux::IERT::IERTDialect",
        "mlir::StandardOpsDialect",
        "mlir::memref::MemRefDialect"
    ];
}

//=================================================================================
// Lowering VPU to VPUIP
//=================================================================================

//
// ConvertVPUToVPUIP
//

def ConvertVPUToVPUIP : PassBase<"convert-vpu-to-vpuip", "vpux::FunctionPass"> {
    let summary = "Convert NCE ops from VPU to VPUIP";

    let description = [{
        Convert ops which can be executed on NCE to explicit NCE ops.
    }];

    let constructor = "vpux::createConvertVPUToVPUIPPass()";

    let dependentDialects = [
        "vpux::VPU::VPUDialect",
        "vpux::IERT::IERTDialect",
        "vpux::VPUIP::VPUIPDialect"
    ];
}

//
// ConvertNCEClusterTilingToVPUIP
//

def ConvertNCEClusterTilingToVPUIP : PassBase<"convert-nce-cluster-tiling-to-vpuip", "vpux::FunctionPass"> {
    let summary = "Convert VPU NCEClusterTiling ops to VPUIP";

    let description = [{
        Convert VPU::NCEClusterTiling ops to VPUIP::NCEClusterTiling.
    }];

    let constructor = "vpux::createConvertNCEClusterTilingToVPUIPPass()";

    let dependentDialects = [
        "vpux::VPU::VPUDialect",
        "vpux::IERT::IERTDialect",
        "vpux::VPUIP::VPUIPDialect",
        "vpux::VPURT::VPURTDialect"
    ];
}

//=================================================================================
// LowerIERT2VPUIP
//=================================================================================

//
// ConvertLayers2VPUIP
//

def ConvertLayers2VPUIP : PassBase<"convert-layers-to-VPUIP", "vpux::FunctionPass"> {
    let summary = "Convert Layers Operations to VPUIP Dialect (UPA and DMA tasks)";

    let constructor = "vpux::createConvertLayers2VPUIPPass()";

    let dependentDialects = [
        "vpux::VPUIP::VPUIPDialect",
        "vpux::VPURT::VPURTDialect"
    ];
}

//
// ConvertSWLayers2VPUIP
//

def ConvertSWLayers2VPUIP : PassBase<"convert-sw-layers-to-VPUIP", "vpux::ModulePass"> {
    let summary = "Convert Software Layers Operations to VPUIP SW_kernel";

    let constructor = "vpux::createConvertSWLayers2VPUIPPass()";

    let dependentDialects = [
        "vpux::VPUIP::VPUIPDialect",
        "mlir::arith::ArithmeticDialect",
        "vpux::VPURT::VPURTDialect"
    ];

}
//
// ConvertDeclarations2VPUIP
//

def ConvertDeclarations2VPUIP : PassBase<"convert-declarations-to-VPUIP", "vpux::FunctionPass"> {
    let summary = "Convert declarations (constants and memory buffers) to VPUIP Dialect";

    let constructor = "vpux::createConvertDeclarations2VPUIPPass()";

    let dependentDialects = [
        "vpux::VPUIP::VPUIPDialect",
        "vpux::VPURT::VPURTDialect"
    ];
}

//
// ConvertViewOps2VPUIP
//

def ConvertViewOps2VPUIP : PassBase<"convert-view-ops-to-VPUIP", "vpux::FunctionPass"> {
    let summary = "Convert view-like Operations to VPUIP Dialect";

    let constructor = "vpux::createConvertViewOps2VPUIPPass()";

    let dependentDialects = [
        "vpux::VPUIP::VPUIPDialect",
        "vpux::VPURT::VPURTDialect"
    ];
}

//
// ConvertAsyncOps2VPUIP
//

def ConvertAsyncOps2VPUIP : PassBase<"convert-async-ops-to-VPUIP", "vpux::FunctionPass"> {
    let summary = "Convert Async Dialect Operations to VPUIP Dialect";

    let description = [{
        This pass inlines 'async.execute' body to parent Block and replaces '!async.token' based dependencies with
        VPUIP virtual barriers.
    }];

    let constructor = "vpux::createConvertAsyncOps2VPUIPPass()";

    let dependentDialects = [
        "vpux::VPUIP::VPUIPDialect",
        "vpux::VPURT::VPURTDialect"
    ];
}

#endif
