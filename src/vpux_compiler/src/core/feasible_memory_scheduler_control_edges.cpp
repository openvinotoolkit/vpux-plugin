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

#include "vpux/compiler/core/feasible_memory_scheduler_control_edges.hpp"

#include "vpux/compiler/utils/rewriter.hpp"

#include "vpux/compiler/core/control_edge_generator.hpp"
#include "vpux/compiler/core/feasible_scheduler_utils.hpp"

#include <iostream>

using namespace vpux;

//
// Feasible Memory Scheduler Control Edges support
//

FeasibleMemorySchedulerControlEdges::FeasibleMemorySchedulerControlEdges(
        mlir::Attribute memSpace, AsyncDepsInfo& depsInfo, AliasesInfo& aliasInfo, Logger log,
        LinearScan<mlir::Value, LinearScanHandler>& scan)
        : _log(log), _memSpace(memSpace), _depsInfo(depsInfo), _aliasInfo(aliasInfo), _scan(scan) {
    _log.setName("feasible-memory-scheduler-control-edges");
}

// This method will update all AsyncExecOp token dependencies so that resulting
// execution is aligned with order generated by list-scheduler
void FeasibleMemorySchedulerControlEdges::insertDependenciesBasic(
        ArrayRef<FeasibleMemoryScheduler::ScheduledOpInfo> scheduledOps) {
    // Go through all the tasks and add token dependencies between
    // all tasks with start time t to all tasks with time t+1
    _log.trace("Get dependencies based on scheduler time decisions");
    _log = _log.nest();
    for (auto opIt = scheduledOps.begin(); opIt != scheduledOps.end(); opIt++) {
        if (opIt->opType_ != FeasibleMemoryScheduler::EOpType::ORIGINAL_OP) {
            continue;
        }

        // if (!opIt->hasActiveResource()) {
        //     continue;
        // }

        size_t nextTimeDiff = 0;
        for (auto nextTimeOpIt = opIt; nextTimeOpIt != scheduledOps.end(); nextTimeOpIt++) {
            if (nextTimeOpIt->opType_ != FeasibleMemoryScheduler::EOpType::ORIGINAL_OP) {
                continue;
            } else if (nextTimeDiff == 0 && nextTimeOpIt->time_ > opIt->time_) {
                nextTimeDiff = nextTimeOpIt->time_ - opIt->time_;
            }

            if (nextTimeDiff != 0) {
                if (nextTimeOpIt->time_ == opIt->time_ + nextTimeDiff) {
                    // Insert dependency between op at time t to op at
                    // time t+1
                    auto srcAsyncOp = _depsInfo.getExecuteOpAtIndex(opIt->op_);
                    auto dstAsyncOp = _depsInfo.getExecuteOpAtIndex(nextTimeOpIt->op_);
                    _log.trace("Dep: {0} -> {1}", opIt->op_, nextTimeOpIt->op_);
                    VPUX_THROW_UNLESS((srcAsyncOp != nullptr) && (dstAsyncOp != nullptr),
                                      "srcAsyncOp/dstAsyncOp not located based on index");
                    _depsInfo.addDependency(srcAsyncOp, dstAsyncOp);
                } else if (nextTimeOpIt->time_ > (opIt->time_ + nextTimeDiff)) {
                    break;
                }
            }
        }
    }
    _log = _log.unnest();
    _log.trace("Update token dependencies in IR");
    _depsInfo.updateTokenDependencies();
}

// This method will update all AsyncExecOp token dependencies so that resulting
// execution is aligned with order generated by list-scheduler
void FeasibleMemorySchedulerControlEdges::insertDependenciesBasicForNonCmxResources(
        ArrayRef<FeasibleMemoryScheduler::ScheduledOpInfo> scheduledOps) {
    // Go through all the tasks and add token dependencies between
    // all tasks with start time t to all tasks with time t+1
    _log.trace("Get dependencies based on scheduler time decisions");
    _log = _log.nest();
    for (auto opIt = scheduledOps.begin(); opIt != scheduledOps.end(); opIt++) {
        if (opIt->opType_ != FeasibleMemoryScheduler::EOpType::ORIGINAL_OP) {
            continue;
        }

        // If operation has no active resources in current mem space than insert
        // control eges
        if (opIt->hasActiveResource()) {
            continue;
        }

        size_t nextTimeDiff = 0;
        for (auto nextTimeOpIt = opIt; nextTimeOpIt != scheduledOps.end(); nextTimeOpIt++) {
            if (nextTimeOpIt->opType_ != FeasibleMemoryScheduler::EOpType::ORIGINAL_OP) {
                continue;
            } else if (nextTimeOpIt->hasActiveResource()) {
                continue;
            } else if (nextTimeDiff == 0 && nextTimeOpIt->time_ > opIt->time_) {
                nextTimeDiff = nextTimeOpIt->time_ - opIt->time_;
            }

            if (nextTimeDiff != 0) {
                if (nextTimeOpIt->time_ == opIt->time_ + nextTimeDiff) {
                    // Insert dependency between op at time t to op at
                    // time t+1
                    auto srcAsyncOp = _depsInfo.getExecuteOpAtIndex(opIt->op_);
                    auto dstAsyncOp = _depsInfo.getExecuteOpAtIndex(nextTimeOpIt->op_);
                    _log.trace("Dep: {0} -> {1}", opIt->op_, nextTimeOpIt->op_);
                    VPUX_THROW_UNLESS((srcAsyncOp != nullptr) && (dstAsyncOp != nullptr),
                                      "srcAsyncOp/dstAsyncOp not located based on index");
                    _depsInfo.addDependency(srcAsyncOp, dstAsyncOp);
                } else if (nextTimeOpIt->time_ > (opIt->time_ + nextTimeDiff)) {
                    break;
                }
            }
        }
    }
    _log = _log.unnest();
    _log.trace("Update token dependencies in IR");
    _depsInfo.updateTokenDependencies();
}

void FeasibleMemorySchedulerControlEdges::insertMemoryControlEdges(
        ArrayRef<FeasibleMemoryScheduler::ScheduledOpInfo> scheduledOps) {
    std::list<ScheduledOpOneResource> scheduledOpsResources;

    _log.trace("Insert control edges for overlapping memory resources");

    // Analyze output from feasible scheduler and prepare list of scheduled
    // operations with their resource and time as needed by control edge
    // generation algorithm
    for (auto& scheduledOp : scheduledOps) {
        // if (!scheduledOp.hasActiveResource()) {
        //     continue;
        // }

        VPUX_THROW_UNLESS(scheduledOp.opType_ == FeasibleMemoryScheduler::EOpType::ORIGINAL_OP || scheduledOp.opType_ == FeasibleMemoryScheduler::EOpType::PREFETCHED_OP,
                          "Invlid operation identified for control edge insertion");

        // buffers used by operation, both inputs adnd outputs
        mlir::DenseSet<mlir::Value> operationBuffers;

        // Get operation buffers for all operands
        auto execOp = _depsInfo.getExecuteOpAtIndex(scheduledOp.op_);

        auto* bodyBlock = &execOp.body().front();
        for (auto& innerOp : bodyBlock->getOperations()) {
            if (!mlir::isa<IERT::LayerOpInterface>(innerOp)) {
                continue;
            }

            for (const auto& operand : innerOp.getOperands()) {
                const auto type = operand.getType().dyn_cast<mlir::MemRefType>();
                if (type == nullptr || type.getMemorySpace() != _memSpace) {
                    continue;
                }

                auto rootBuffers = _aliasInfo.getRoots(operand);
                VPUX_THROW_UNLESS(rootBuffers.size() == 1, "Value '{0}' expected to have only one root. Got {1}",
                                  operand, rootBuffers.size());
                auto rootBuffer = *rootBuffers.begin();
                operationBuffers.insert(rootBuffer);
            }
        }

        // // Get operation output buffers
        // for (size_t resourceIdx = 0; resourceIdx < scheduledOp.numOfResources(); resourceIdx++) {
        //     operationBuffers.insert(scheduledOp.getBuffer(resourceIdx));
        // }

        for (auto& buf : operationBuffers) {
            auto addressStart = _scan.handler().getAddress(buf);
            auto addressEnd = addressStart + _scan.handler().getSize(buf) - 1;
            scheduledOpsResources.push_back(ScheduledOpOneResource(scheduledOp.op_, addressStart, addressEnd));
        }

        // // Since operations can have multiple outputs create separate entry for
        // // each resource for the same op
        // for (size_t resourceIdx = 0; resourceIdx < scheduledOp.numOfResources(); resourceIdx++) {
        //     auto rbegin = scheduledOp.beginResource(resourceIdx);
        //     auto rend = scheduledOp.endResource(resourceIdx) - 1;
        //     scheduledOpsResources.push_back(ScheduledOpOneResource(scheduledOp.op_, rbegin, rend));
        // }
    }

    // for (auto& scheduledOpResource : scheduledOpsResources) {
    //     _log.trace("op - {0} res {1} - {2}", scheduledOpResource._op, scheduledOpResource._addressStart,
    //                scheduledOpResource._addressEnd);
    // }

    ControlEdgeSet controlEdges;
    ControlEdgeGenerator<ScheduledOpOneResource> controlEdgeGenerator;
    // Generate control edges for overlapping memory regions
    controlEdgeGenerator.generateControlEdges(scheduledOpsResources.begin(), scheduledOpsResources.end(), controlEdges);

    // Apply dependencies from controlEdges set in depsInfo and
    // later transfer this to token based dependencies between AsyncExecuteOp
    _log = _log.nest();
    for (auto itr = controlEdges.begin(); itr != controlEdges.end(); ++itr) {
        if (itr->_source == itr->_sink) {
            continue;
        }
        _log.trace("Dep: {0} -> {1}", itr->_source, itr->_sink);
        auto sourceOp = _depsInfo.getExecuteOpAtIndex(itr->_source);
        auto sinkOp = _depsInfo.getExecuteOpAtIndex(itr->_sink);
        _depsInfo.addDependency(sourceOp, sinkOp);
    }
    _log = _log.unnest();

    _log.trace("Update token dependencies in IR");
    _depsInfo.updateTokenDependencies();
}