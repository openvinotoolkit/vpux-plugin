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
#pragma once

#include "vpux/compiler/dialect/VPURT/barrier_simulator.hpp"
#include "vpux/compiler/core/barrier_resource_state.hpp"

namespace vpux {

namespace VPURT {

constexpr llvm::StringLiteral schedulingNumberAttrName = "SchedulingNumber";
constexpr llvm::StringLiteral uniqueIdAttrName = "uniqueId";
constexpr llvm::StringLiteral virtualIdAttrName = "VPURT.virtualId";

class BarrierScheduler final {
public:
    struct task_operation_comparator_by_schedule_time_t {
        bool operator()(mlir::Operation* op1, mlir::Operation* op2) const {
            int64_t schedulingNumber1 = checked_cast<int64_t>(
                    mlir::dyn_cast<VPURT::TaskOp>(op1)->getAttr("SchedulingNumber").cast<mlir::IntegerAttr>().getInt());
            int64_t schedulingNumber2 = checked_cast<int64_t>(
                    mlir::dyn_cast<VPURT::TaskOp>(op2)->getAttr("SchedulingNumber").cast<mlir::IntegerAttr>().getInt());

            return schedulingNumber1 < schedulingNumber2;
        }
    };
    struct operation_comparator_t {
        bool operator()(mlir::Operation* op1, mlir::Operation* op2) const {
            int64_t uniqueId1 = checked_cast<int64_t>(mlir::dyn_cast<VPURT::DeclareVirtualBarrierOp>(op1)
                                                              ->getAttr(virtualIdAttrName)
                                                              .cast<mlir::IntegerAttr>()
                                                              .getInt());
            int64_t uniqueId2 = checked_cast<int64_t>(mlir::dyn_cast<VPURT::DeclareVirtualBarrierOp>(op2)
                                                              ->getAttr(virtualIdAttrName)
                                                              .cast<mlir::IntegerAttr>()
                                                              .getInt());

            return uniqueId1 < uniqueId2;
        }
    };
    struct HeapElement {
        HeapElement(mlir::Operation* op = NULL, size_t t = 0UL): _op(op), _time(t) {
        }
        mlir::Operation* _op;
        size_t _time;
    };

    struct MinHeapOrdering {
        bool operator()(const HeapElement& a, const HeapElement& b) {
            return a._time > b._time;
        }
    };

    struct ScheduledOpInfo {
        size_t _scheduleTime;
        mlir::Operation* _op;
        size_t _barrierIndex;
        size_t _producerSlotCount;
    };

    struct barrierInfo {
        barrierInfo(size_t bindex = 0UL, size_t slot_count = 0UL): _bindex(bindex), _producerSlotCount(slot_count) {
        }
        size_t _bindex;
        size_t _producerSlotCount;
    };
    class barrierTransitionStructure {
    public:
        barrierTransitionStructure(BarrierScheduler& feasibleBarrierScheduler,
                                   size_t time = std::numeric_limits<size_t>::max());

        void init();
        bool processNextScheduledTask(const ScheduledOpInfo& sinfo, mlir::OpBuilder& builder);
        void closeBarrierProducerList();
        struct operation_comparator_t {
            bool operator()(mlir::Operation* op1, mlir::Operation* op2) const {
                int64_t uniqueId1 = checked_cast<int64_t>(mlir::dyn_cast<VPURT::TaskOp>(op1)
                                                                  ->getAttr(uniqueIdAttrName)
                                                                  .cast<mlir::IntegerAttr>()
                                                                  .getInt());
                int64_t uniqueId2 = checked_cast<int64_t>(mlir::dyn_cast<VPURT::TaskOp>(op2)
                                                                  ->getAttr(uniqueIdAttrName)
                                                                  .cast<mlir::IntegerAttr>()
                                                                  .getInt());

                return uniqueId1 < uniqueId2;
            }
        };

        using producers_t = std::set<mlir::Operation*, operation_comparator_t>;
        using producer_iterator_t = typename producers_t::const_iterator;

    private:
        void maintainInvariantTemporalChange(const ScheduledOpInfo& sinfo, mlir::OpBuilder& builder);
        inline void processCurrentBarrierProducerListCloseEvent(mlir::Operation* bop_curr, mlir::Operation* bop_prev);
        void addScheduledOpToProducerList(const ScheduledOpInfo& sinfo);
        mlir::Operation* createNewBarrierTask(const ScheduledOpInfo& sinfo, mlir::OpBuilder& builder);

        // Outer class
        BarrierScheduler& _feasibleBarrierScheduler;
        size_t time_;
        mlir::Operation* curr_barrier_task_;
        mlir::Operation* prev_barrier_task_;
        producers_t producers_;
    };

    using schedulableOpsType = std::list<mlir::Operation*>;
    using schedulableTasksIteratorType = typename schedulableOpsType::iterator;
    using activeBarrierMapType = std::unordered_map<mlir::Operation*, barrierInfo>;
    using barrierAssociationTableType = std::unordered_map<size_t, barrierTransitionStructure>;
    using processedOpsType = std::set<mlir::Operation*>;
    using scheduleHeapType = std::vector<HeapElement>;
    using operationInDegreeType = std::map<mlir::Operation*, size_t>;
    using priorityMapType = std::map<mlir::Operation*, size_t>;
    using barrierResourceUtilityMapType = std::unordered_map<mlir::Operation*, size_t>;
    using barrierUpdateWaitMapType =
            std::map<mlir::Operation*, std::pair<std::set<mlir::Operation*>, std::set<mlir::Operation*>>,
                     operation_comparator_t>;
    using taskOpUpdateWaitMapType =
            std::map<mlir::Operation*, std::pair<std::set<mlir::Operation*>, std::set<mlir::Operation*>>,
                     task_operation_comparator_by_schedule_time_t>;

    BarrierScheduler(mlir::FuncOp func, Logger log);
    void init();
    void assignTaskUniqueIds();
    void saveOriginalIRDependency();
    void assignTaskPriorities();
    void createTaskBarrierResourceUtilityTable();
    void initializeBarrierAssociationTable();
    void initializeBarrierResourceState(const size_t numberOfBarriers, const size_t maxProducersPerBarrier);
    void addTaskToCandidateSet(mlir::Operation* op);
    void addOutGoingOperationsToCandidateList(mlir::Operation* op);
    void pushToHeap(const HeapElement& elem);
    void insertBarriersinIR();
    void populateScheduledTasks(mlir::Operation* scheduledOp);
    void removeRedundantWaitBarriers();
    void populateTasksUpdateWaitBarrierMap(barrierUpdateWaitMapType& barrierOpUpdateWaitMap,
                                           taskOpUpdateWaitMapType& taskOpUpdateWaitMap);
    void removeRedundantDependencies();
    void removeRedundantBarriers();
    void reorderIR();
    void removeVirtualBarriers();

    bool performSchedulingTaskLoop();
    bool isBarrierResourceAvailable(const size_t demand);
    bool scheduleTask(mlir::Operation* op, const size_t demand);
    bool unScheduleTask(mlir::Operation* op);
    bool generateScheduleWithBarriers(const size_t numberOfBarriers, const size_t maxProducersPerBarrier);
    bool doesOpRunOnNCE(mlir::Operation* op);
    bool isTasKInSchedulableCandidates(schedulableTasksIteratorType itr) const;
    bool doesPathExist(mlir::Operation* a, mlir::Operation* b);
    bool performRuntimeSimulation();
    void cleanUpVirtualBarriers();
    void clearTemporaryAttribute();

    HeapElement popFromHeap();
    size_t currentTime() const;
    const BarrierResourceState& barrierResourceState() const;
    schedulableTasksIteratorType findSchedulableTask();
    size_t countProducerConsumerTasks(mlir::Operation* op);
    SmallVector<mlir::Operation*> getConsumerOps(mlir::Operation* op);
    static mlir::IntegerAttr getUniqueID(mlir::Operation* op);
    const barrierInfo& getBarrierInfo(mlir::Operation* op) const;

private:
    // The number of available barriers
    size_t _barrierCount;
    // The number of available producers slots per barrier
    size_t _slotsPerBarrier;
    // The current barrier resource utilization by the schedule i.e active barriers and the number of producers
    BarrierResourceState _barrierResourceState;
    // The in-degree per task
    operationInDegreeType _inDegree;
    // The in-degree per task from original dependency
    operationInDegreeType _originalInDegree;
    // Heap with operation end time
    scheduleHeapType _heap;
    // The current time of the list schedule
    size_t _currentTime;
    // Tasks that can be scheduled i.e. task with in-degree zero
    schedulableOpsType _schedulableCandidates;
    // Tasks that have been scheduled
    processedOpsType _processedTasks;
    // The scheduling priority of tasks
    priorityMapType _priority;
    // container for the schedule output
    SmallVector<ScheduledOpInfo> _scheduledTasks;
    // A map of physical barriers and their transition structure
    barrierAssociationTableType _barrierAssociationTable;
    // The number of producer slots in a barrier that a task requires
    barrierResourceUtilityMapType _barrierResourceUtilizationMap;
    // The output tasks in the IR
    std::set<mlir::Operation*> _outputTasks;
    // The backup of output tasks in the IR
    std::set<mlir::Operation*> _origninalOutputOps;
    // The barrier information for each task
    activeBarrierMapType _barrierMap;
    Logger _log;
    mlir::FuncOp _func;

    // Stores every barrier's associated update and wait operations
    barrierUpdateWaitMapType configureBarrierOpUpdateWaitMap;
    // Stores every task's associated update and wait barriers
    taskOpUpdateWaitMapType configureTaskOpUpdateWaitMap;
    // The consumer tasks per task from original dependency
    std::map<mlir::Operation*, SmallVector<mlir::Operation*>> _taskConsumerMapBackUp;
};

}  // namespace VPURT
}  // namespace vpux