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

#include "vpux/compiler/core/token_barrier_scheduler.hpp"

#include "vpux/compiler/utils/attributes.hpp"

#include "vpux/utils/core/range.hpp"

using namespace vpux;
static constexpr StringLiteral schedulingNumberAttrName = "SchedulingNumber";
//
// Constructor
//

TokenBasedBarrierScheduler::TokenBasedBarrierScheduler(mlir::MLIRContext* ctx, mlir::FuncOp func, int64_t numBarriers,
                                                       int64_t slotCount)
        : _ctx(ctx), _func(func), builder(_func.getBody()), barrierCount_(numBarriers), slotCount_(slotCount) {
}

bool TokenBasedBarrierScheduler::isPathExist(mlir::Operation* a, mlir::Operation* b) {
    auto numa = a->getAttr("SchedulingNumber").cast<mlir::IntegerAttr>().getInt();
    auto numb = b->getAttr("SchedulingNumber").cast<mlir::IntegerAttr>().getInt();
    if (numa >= numb)
        return false;
    else {
        for (auto iter = configureBarrierOpUpdateWaitMap.begin(); iter != configureBarrierOpUpdateWaitMap.end();
             iter++) {
            auto produces = (*iter).second.first;
            if (std::find(produces.begin(), produces.end(), a) != produces.end()) {
                auto consumers = (*iter).second.second;
                if (std::find(consumers.begin(), consumers.end(), b) != consumers.end())
                    return true;
                else {
                    for (auto consumer = consumers.begin(); consumer != consumers.end(); consumer++) {
                        if (isPathExist(*consumer, b))
                            return true;
                    }
                }
            }
        }
        return false;
    }
}

size_t TokenBasedBarrierScheduler::schedule() {
    size_t btask_count = 0UL;

    // last associated barrier task associated with index //
    barrier_association_table_t barrier_association;

    Logger::global().error("STEP-0: Initialize the association table");
    for (size_t barrierId = 1; barrierId <= barrierCount_; barrierId++) {
        auto bitr = barrier_association.insert(std::make_pair(barrierId, barrier_transition_structure_t(_func, *this)));
        barrier_transition_structure_t& bstructure = (bitr.first)->second;
        bstructure.init();
    }
    {
        Logger::global().error("STEP-1: run the scheduler");
        BarrierScheduleGenerator scheduler_begin(_ctx, _func, barrierCount_, slotCount_);
        BarrierScheduleGenerator scheduler_end(_ctx, _func);
        size_t scheduling_number = 0UL;

        std::unordered_map<schedule_time_t, std::set<size_t>> active_barriers_at_time;
        std::unordered_set<mlir::Operation*> scheduled_ops;
        size_t make_span = 0UL;

        for (; scheduler_begin != scheduler_end; ++scheduler_begin) {
            const BarrierScheduleGenerator::schedule_info_t& sinfo = *scheduler_begin;

            Logger::global().error("Getting the schedule information sinfo from the Barrier_Schedule_Generator class");
            Logger::global().error("The time is {0}, the Operation is {1} end time is {1}", sinfo.schedule_time_,
                                   FeasibleScheduleGenerator::getUniqueID(sinfo.op_));
            Logger::global().error("The barrier index is {0}, , the slot cout is {1}", sinfo.barrier_index_,
                                   sinfo.slot_count_);

            Logger::global().error("Now looking up the barrier association table for the barrier {0} and getting the "
                                   "barrier association table",
                                   sinfo.barrier_index_);

            auto bitr = barrier_association.find(sinfo.barrier_index_);
            assert(bitr != barrier_association.end());
            barrier_transition_structure_t& bstructure = bitr->second;

            int64_t opID = checked_cast<int64_t>(sinfo.op_->getAttr("uniqueId").cast<mlir::IntegerAttr>().getInt());
            printf("[time=%lu barrier_id=%lu slot_count=%lu op=%lu]\n", sinfo.schedule_time_, sinfo.barrier_index_,
                   sinfo.slot_count_, opID);

            scheduled_ops.insert(sinfo.op_);
            if (active_barriers_at_time.find(sinfo.schedule_time_) == active_barriers_at_time.end()) {
                active_barriers_at_time[sinfo.schedule_time_] = std::set<size_t>();
            }
            active_barriers_at_time[sinfo.schedule_time_].insert(sinfo.barrier_index_);
            scheduled_ops.insert(sinfo.op_);
            make_span = std::max(make_span, sinfo.schedule_time_);

            // Set scheduling number
            Logger::global().error("Assigning scheduling number {0} to the Operation {1} ", scheduling_number,
                                   FeasibleScheduleGenerator::getUniqueID(sinfo.op_));
            sinfo.op_->setAttr(schedulingNumberAttrName, getIntAttr(_ctx, scheduling_number));

            scheduling_number++;
            // STEP-2: update barrier structure invariant //
            bool new_barrier_task_created = bstructure.process_next_scheduled_op(sinfo, builder);

            if (new_barrier_task_created) {
                ++btask_count;
            }
        }
        size_t max_active = 0UL;
        for (auto aitr = active_barriers_at_time.begin(); aitr != active_barriers_at_time.end(); ++aitr) {
            max_active = std::max(max_active, (aitr->second).size());
        }
        std::cout << "The max active barrier are: " << max_active << std::endl;
    }

    // STEP-2.5: process trailing barrier control structures //
    {
        for (auto bitr = barrier_association.begin(); bitr != barrier_association.end(); ++bitr) {
            barrier_transition_structure_t& bstruct = bitr->second;
            bstruct.close_barrier_producer_list();
        }
    }

    // update,wait
    for (auto& barrier : configureBarrierOpUpdateWaitMap) {
        Logger::global().error("Barrier ID {0} has the following producers", barrier.first->getAttr("id"));
        for (auto op : barrier.second.first)
            Logger::global().error("producer Op with ID {0} to barrier {1}", FeasibleScheduleGenerator::getUniqueID(op),
                                   barrier.first->getAttr("id"));
    }

    for (auto& barrier : configureBarrierOpUpdateWaitMap) {
        Logger::global().error("Barrier ID {0} has the following consumers", barrier.first->getAttr("id"));
        for (auto op : barrier.second.second)
            Logger::global().error("consumer Op with ID {0} to barrier {1}", FeasibleScheduleGenerator::getUniqueID(op),
                                   barrier.first->getAttr("id"));
    }

    std::cout << "Done scheduling" << std::endl;

    for (auto iter = configureBarrierOpUpdateWaitMap.begin(); iter != configureBarrierOpUpdateWaitMap.end(); iter++) {
        auto barrierOp = (*iter).first;
        auto producers = (*iter).second.first;
        auto consumers = (*iter).second.second;
        for (auto prod = producers.begin(); prod != producers.end(); prod++) {
            auto taskUpateItr = configureTaskOpUpdateWaitMap.find(*prod);
            if (taskUpateItr != configureTaskOpUpdateWaitMap.end()) {
                taskUpateItr->second.second.insert(barrierOp);
            } else {
                std::set<mlir::Operation*> newBarrierProducers{};
                std::set<mlir::Operation*> newBarrierConsumers{barrierOp};
                configureTaskOpUpdateWaitMap.insert(
                        std::make_pair(*prod, std::make_pair(newBarrierProducers, newBarrierConsumers)));
            }
        }

        for (auto cons = consumers.begin(); cons != consumers.end(); cons++) {
            auto taskWaitItr = configureTaskOpUpdateWaitMap.find(*cons);
            if (taskWaitItr != configureTaskOpUpdateWaitMap.end()) {
                taskWaitItr->second.first.insert(barrierOp);
            } else {
                std::set<mlir::Operation*> newBarrierProducers{barrierOp};
                std::set<mlir::Operation*> newBarrierConsumers{};
                configureTaskOpUpdateWaitMap.insert(
                        std::make_pair(*cons, std::make_pair(newBarrierProducers, newBarrierConsumers)));
            }
        }
    }

    std::cout << "Done creating configureTaskOpUpdateWaitMap" << std::endl;

    _func->walk([](VPURT::TaskOp op) {
        op.updateBarriersMutable().clear();
        op.waitBarriersMutable().clear();
    });

    // remove redundant deps
    for (auto iter = configureBarrierOpUpdateWaitMap.begin(); iter != configureBarrierOpUpdateWaitMap.end(); iter++) {
        // producers
        auto producers = (*iter).second.first;
        for (auto prod = producers.begin(); prod != producers.end();) {
            auto prod1 = prod;
            prod1++;
            for (; prod1 != producers.end();) {
                if (isPathExist(*prod1, *prod)) {
                    auto removedIter = prod1;
                    prod1++;
                    producers.erase(removedIter);
                } else if (isPathExist(*prod, *prod1)) {
                    auto removedIter = prod;
                    prod++;
                    producers.erase(removedIter);
                    break;
                } else
                    prod1++;
            }
            if (prod1 == producers.end())
                prod++;
        }
        (*iter).second.first = producers;

        // consumers
        auto consumers = (*iter).second.second;
        for (auto cons = consumers.begin(); cons != consumers.end();) {
            auto cons1 = cons;
            cons1++;
            for (; cons1 != consumers.end();) {
                if (isPathExist(*cons, *cons1)) {
                    auto removedIter = cons1;
                    cons1++;
                    consumers.erase(removedIter);
                } else if (isPathExist(*cons1, *cons)) {
                    auto removedIter = cons;
                    cons++;
                    consumers.erase(removedIter);
                    break;
                } else
                    cons1++;
            }
            if (cons1 == consumers.end())
                cons++;
        }
        (*iter).second.second = consumers;
    }

    // remove redundant barriers
    for (auto iter = configureBarrierOpUpdateWaitMap.begin(); iter != configureBarrierOpUpdateWaitMap.end(); iter++) {
        auto consumers = (*iter).second.second;
        auto iter1 = iter;
        iter1++;
        for (; iter1 != configureBarrierOpUpdateWaitMap.end();) {
            auto consumers1 = (*iter1).second.second;
            if (consumers1 == consumers) {
                Logger::global().error("found barrier {0} and {1} have same consumers", (*iter).first->getAttr("id"),
                                       (*iter1).first->getAttr("id"));
                auto producers = (*iter1).second.first;
                // auto mergedProducers = (*iter).second.first
                for (auto& task : producers) {
                    (*iter).second.first.insert(task);
                }
                auto removedIter = iter1;
                iter1++;
                configureBarrierOpUpdateWaitMap.erase(removedIter);
            } else
                iter1++;
        }
    }

    for (const auto& p : configureBarrierOpUpdateWaitMap) {
        auto barrierOp = mlir::dyn_cast_or_null<VPURT::DeclareVirtualBarrierOp>(p.first);
        Logger::global().error("Virtual Barrier ID {0} has {1} consumers", barrierOp->getAttr("id"),
                               p.second.second.size());
    }

    for (const auto& p : configureBarrierOpUpdateWaitMap) {
        auto barrierOp = mlir::dyn_cast_or_null<VPURT::DeclareVirtualBarrierOp>(p.first);
        Logger::global().error("Virtual Barrier ID {0} has {1} producers", barrierOp->getAttr("id"),
                               p.second.first.size());
    }

    for (auto itr = configureBarrierOpUpdateWaitMap.begin(); itr != configureBarrierOpUpdateWaitMap.end();) {
        if (itr->second.second.empty()) {
            Logger::global().error("Earsing virtual Barrier ID {0} as it has no producers", itr->first->getAttr("id"));
            itr = configureBarrierOpUpdateWaitMap.erase(itr);
        } else {
            ++itr;
        }
    }

    _func->walk([&](VPURT::DeclareVirtualBarrierOp op) {
        if (!configureBarrierOpUpdateWaitMap.count(op)) {
            Logger::global().error("Erasing Barrier ID {0} ", op->getAttr("id"));
            op->dropAllUses();
            op.erase();
        }
    });

    for (const auto& p : configureBarrierOpUpdateWaitMap) {
        auto barrierOp = mlir::dyn_cast_or_null<VPURT::DeclareVirtualBarrierOp>(p.first);
        for (auto* user : p.second.first) {
            auto taskOp = mlir::dyn_cast_or_null<VPURT::TaskOp>(user);
            assert(taskOp != NULL);
            assert(barrierOp.barrier() != NULL);
            Logger::global().error("Adding Barrier ID {0} as an update barrier for operation {1}",
                                   barrierOp->getAttr("id"), FeasibleScheduleGenerator::getUniqueID(user));
            taskOp.updateBarriersMutable().append(barrierOp.barrier());
        }
    }

    for (const auto& p : configureBarrierOpUpdateWaitMap) {
        auto barrierOp = mlir::dyn_cast_or_null<VPURT::DeclareVirtualBarrierOp>(p.first);
        for (auto* user : p.second.second) {
            auto taskOp = mlir::dyn_cast_or_null<VPURT::TaskOp>(user);
            assert(taskOp != NULL);
            assert(barrierOp.barrier() != NULL);
            Logger::global().error("Adding Barrier ID {0} as an wait barrier for operation {1}",
                                   barrierOp->getAttr("id"), FeasibleScheduleGenerator::getUniqueID(user));
            taskOp.waitBarriersMutable().append(barrierOp.barrier());
        }
    }

    std::cout << "Removed all declare virtual barrier ops" << std::endl;

    // reorder IR by scheduling number
    mlir::Operation* preTask = nullptr;
    for (auto iter = configureTaskOpUpdateWaitMap.begin(); iter != configureTaskOpUpdateWaitMap.end(); iter++) {
        auto curTask = (*iter).first;
        if (preTask) {
            curTask->moveAfter(preTask);
        }
        preTask = curTask;
    }

    return btask_count;
}