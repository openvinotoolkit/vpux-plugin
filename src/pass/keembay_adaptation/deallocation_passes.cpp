#include "include/mcm/pass/pass_registry.hpp"
#include "meta/include/mcm/op_model.hpp"
#include "include/mcm/computation/model/control_model.hpp"
#include "include/mcm/computation/model/data_model.hpp"
#include <algorithm>

static void addDeallocationTasksFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::json::Object&);
static void removeDeallocationTasksFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::json::Object&);

namespace mv
{
    namespace pass
    {
        MV_REGISTER_PASS(AddDeallocationTasks)
            .setFunc(addDeallocationTasksFcn)
            .setDescription(
               "Add deallocation tasks where needed in the Task graph");

        MV_REGISTER_PASS(RemoveDeallocationTasks)
            .setFunc(removeDeallocationTasksFcn)
            .setDescription(
               "Remove all deallocation tasks from the Task graph");
    }
}

// Pass role: Add deallocation tasks for each Tensor
void addDeallocationTasksFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::json::Object&)
{

    mv::OpModel om(model);
    mv::DataModel dm(model);
    mv::ControlModel cm(model);

    // Sorting ops in dataflow topological order. Will be needed later.
    auto removeOps = [] (std::vector<mv::Data::OpListIterator>& list, const std::string& opType)
    {
        list.erase(std::remove_if(list.begin(), list.end(), [opType](mv::Data::OpListIterator it) { return it->getOpType() == opType;}), list.end());
    };

    auto sortedOps = om.topologicalSort();
    removeOps(sortedOps, "Constant");
    removeOps(sortedOps, "ConstantInt");
    removeOps(sortedOps, "ConstantDataElement");

    for(auto dataFlowIt = dm.flowBegin(); dataFlowIt != dm.flowEnd(); ++dataFlowIt)
    {
        auto inputOp = dataFlowIt.source();
        auto outputOp = dataFlowIt.sink();

        // Final tensor shall not be deallocated
        if(outputOp->getOpType() == "Output")
            continue;

        // Input tensor shall not be deallocated (will be DMAed, then that will be deallocated)
        if(inputOp->getOpType() == "Input")
            continue;

        // Constant tensors shall not be deallocated (will be DMAed, then those will be deallocated)
        if(inputOp->getOpType() == "Constant" || inputOp->getOpType() == "ConstantInt" || inputOp->getOpType() == "ConstantDataElement" ||
            inputOp->getOpType() == "WeightsTable" || inputOp->getOpType() == "SparsityMap")
            continue;

        auto opId = inputOp->get<unsigned>("opId");
        auto inputOpName = inputOp->getName();
        auto inputTensor = dataFlowIt->getTensor();

        std::string deallocationName("Deallocate"+inputOpName);

        // Each tensor must be deallocated once
        if(!om.checkOp(deallocationName))
        {
            // Flows names must be taken before the insertion of deallocation ops
            // Otherwise deallocation will appear as well
            auto flowsNames = inputTensor->get<std::set<std::string>>("flows");

            // Creating deallocation operation for the tensor and attaching it through a dataflow
            // to the operation that created it
            om.deallocate(inputTensor, deallocationName);
            auto deallocateInputOp = om.getOp(deallocationName);

            // Attaching also through a ControlFlow
            auto flowIt = cm.defineFlow(inputOp, deallocateInputOp);
            auto outputTensor = flowIt.source()->getOutputTensor(0);

            flowIt->set<int>("MemoryRequirement", outputTensor->computeMemoryRequirement());
            flowIt->set<bool>("PositiveMemory", true);
            deallocateInputOp->set<unsigned>("opId", opId);

            // Control flows for the newly created operation must be attached now.
            // Checking all the ops that have this tensor as input

            std::vector<mv::Data::OpListIterator> sinkOperations;
            for(auto flowName : flowsNames)
            {
                auto df = dm.getDataFlow(flowName);
                sinkOperations.push_back(df.sink());
            }

            bool found = false;
            auto chosenOp = sortedOps.rbegin();
            for(; chosenOp != sortedOps.rend(); ++chosenOp)
            {
                if(std::find(sinkOperations.begin(), sinkOperations.end(), *chosenOp) != sinkOperations.end())
                {
                    found = true;
                    break;
                }
            }

            if(!found)
                throw "MarcoYouScrewedUpException";

            if(!cm.checkControlFlow(*chosenOp, deallocateInputOp))
                cm.defineFlow(*chosenOp, deallocateInputOp);

            // For DATA model the loop is slightly different
            // DATA
            auto chosenOpData = *chosenOp;
            std::vector<mv::Data::OpListIterator> dataSons;
            for(auto son = chosenOpData.leftmostChild(); son != om.opEnd(); ++son)
                if(son->getOpType() != "Deallocate")
                    dataSons.push_back(son);

            unsigned maxIndex = 0;
            unsigned maxIndexPosition = 0;
            for(unsigned i = 0; i < dataSons.size(); ++i)
            {
                auto son = dataSons[i];
                auto index = std::distance(sortedOps.begin(), std::find(sortedOps.begin(), sortedOps.end(), son));
                if(index > maxIndex)
                {
                    maxIndex = index;
                    maxIndexPosition = i;
                }
            }
            cm.defineFlow(deallocateInputOp, dataSons[maxIndexPosition]);

            // CONTROL
            auto chosenOpControl = cm.switchContext(*chosenOp);
            auto deallocateInputOpControl = cm.switchContext(deallocateInputOp);
            for(auto son = chosenOpControl.leftmostChild(); son != cm.opEnd(); ++son)
                if(!cm.checkControlFlow(deallocateInputOpControl, son))
                    if(son->getOpType() != "Deallocate")
                        cm.defineFlow(deallocateInputOpControl, son);

        }
    }

    // Remove deallocation for last operation

    auto outputOp = om.getOutput();
    auto lastDMAOp = outputOp.leftmostParent();
    auto lastOpBeforeLastDma = lastDMAOp.leftmostParent();

    auto lastDeallocation = om.getOp("Deallocate"+lastOpBeforeLastDma->getName());
    om.removeOp(lastDeallocation);
}

// Pass role: Remove deallocation tasks for each Tensor

// Data flows should not be propagated, control flows yes
void removeDeallocationTasksFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::json::Object&)
{
    mv::OpModel om(model);
    mv::DataModel dm(model);
    mv::ControlModel cm(model);
    
    std::vector<std::pair<mv::Data::OpListIterator, mv::Data::OpListIterator>> oldEdges ;
    std::vector<std::pair<mv::Data::OpListIterator, mv::Data::OpListIterator>> newEdges ;

    // build a list of all the control flow edges
    for (auto ctlFlow = om.opBegin(); ctlFlow != om.opEnd(); ++ctlFlow)
    {
        auto cmCtlFlow = cm.switchContext(ctlFlow);
        for ( auto parentOp = cmCtlFlow.leftmostParent(); parentOp != cm.opEnd(); ++parentOp)
        {
            std::pair<mv::Data::OpListIterator, mv::Data::OpListIterator> oldEdge(om.switchContext(parentOp), ctlFlow);
            oldEdges.push_back(oldEdge);
        }
    }

    // build list of edges to add around deallocs
    for (auto ctlFlow = om.opBegin(); ctlFlow != om.opEnd(); ++ctlFlow)
    {
        auto ctlFlowOpType = ctlFlow->getOpType();
        if (ctlFlowOpType == "Deallocate")
        {
            auto cmCtlFlow = cm.switchContext(ctlFlow);
            for (auto parentOp = cmCtlFlow.leftmostParent(); parentOp != cm.opEnd(); ++parentOp)
            {
                for (auto childOp = cmCtlFlow.leftmostChild(); childOp != cm.opEnd(); ++childOp)
                {
                    auto childOpName = childOp->getName();

                    // add edge around dealloc if it does not exist
                    bool AddNewEdge = true ;
                    for ( auto tryEdge : newEdges )
                    {
                        if ((tryEdge.first->getName()==parentOp->getName()) && (tryEdge.second->getName()==childOp->getName()))
                        {
                            AddNewEdge = false;
                            break;
                        }
                    }
                    if (AddNewEdge)
                    {
                        std::pair<mv::Data::OpListIterator, mv::Data::OpListIterator> newEdge(om.switchContext(parentOp), om.switchContext(childOp));
                        newEdges.push_back(newEdge);
                    }
                }
            }
        }
    }

    // insert the new edges around the deallocs into the graph
    for (auto newPair : newEdges )
    {
        // do not add edge if it already was there
        bool AddNewEdge = true ;
        for ( auto tryEdge : oldEdges )
        {
            if ((tryEdge.first->getName()==newPair.first->getName()) && (tryEdge.second->getName()==newPair.second->getName()))
            {
                AddNewEdge = false;
                break;
            }
        }
        if (AddNewEdge)
        {
            cm.defineFlow(newPair.first , newPair.second );
        }
    }

    // remove the deallocs
    auto deallocTasks = om.getOps("Deallocate");
    for(auto vecIt = deallocTasks.begin(); vecIt != deallocTasks.end(); ++vecIt)
    {
        auto deallocTaskDataIt = *vecIt;
        om.removeOp(deallocTaskDataIt);
    }
}
