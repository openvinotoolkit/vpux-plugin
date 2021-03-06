//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/pass/pass_registry.hpp"
#include "include/mcm/op_model.hpp"
#include "include/mcm/pass/pass_utils.hpp"
#include "include/mcm/computation/model/control_model.hpp"
#include "include/mcm/computation/model/data_model.hpp"
#include "include/mcm/computation/flow/implicit_flow.hpp"

static void resolveImplicitOperationsFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);
//NOTE: this function was mostly a hack, but in general the idea is correct, any implicit operation between
//ddr and output should not be translated with a dma but the output buffer should contain the ddr one
//static void ensureNoOddDMAsBetweenDDROutputFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);
namespace mv
{
    namespace pass
    {
        MV_REGISTER_PASS(ResolveImplicitOperations)
                .setFunc(resolveImplicitOperationsFcn)
                .setDescription("loops over all the candidate implicit operations and will try to add DMA to them");

//        MV_REGISTER_PASS(EnsureNoOddDMAsBetweenDDROutput)
//                .setFunc(ensureNoOddDMAsBetweenDDROutputFcn)
//                .setDescription("loops over all the candidate implicit operations and will try to add DMA to them");
    }
}

// Recursively search up or down (based on provided iterator) for concat ops in the chain of
// Implicit operations. Stop the search if concat op was located or if operation is not an implicit operation.
template <typename T>
void searchImplicitOpsChainForConcatRecursive(T (mv::detail::OpListIterator::*getIterator)(void), mv::Data::OpListIterator exploreOp, std::vector<mv::Data::OpListIterator>& concatOps, mv::OpModel& om)
{
    for(T nextOp = (exploreOp.*getIterator)(); nextOp != om.opEnd(); ++nextOp)
    {
        auto opType = nextOp->getOpType();
        if(opType == "Concat" || opType == "ImplicitConcat")
        {
            concatOps.push_back(nextOp);
        }
        else if(!nextOp->hasTypeTrait("executable"))
        {
            searchImplicitOpsChainForConcatRecursive(getIterator, nextOp, concatOps, om);
        }
    }
}

void updateConcatForSliceOpInDdr(mv::Data::OpListIterator sliceOp, mv::OpModel& om)
{
    // Search for any Concat operation within a sequence of Implicit operations in the
    // chain with this Slice op. Store those ops in a vector
    std::vector<mv::Data::OpListIterator> concatOps;
    // Search for concat recursively up in the graph
    searchImplicitOpsChainForConcatRecursive(&mv::detail::OpListIterator::leftmostParent, sliceOp, concatOps, om);
    // Search for concat recursively down in the graph
    searchImplicitOpsChainForConcatRecursive(&mv::detail::OpListIterator::leftmostChild, sliceOp, concatOps, om);
    // Configure "avoid_cmx_concat" in detected Concat operations to prevent LpScheduler
    // from placing them in CMX
    for (auto& concatOp : concatOps)
    {
        concatOp->set<bool>("avoid_cmx_concat", true);
    }
}

//TODO:: unify all these enums.....
static std::map<const std::string,mv::DmaDirectionEnum> dmaDirectionStrings =
{
      {"NNCMX2DDR",mv::DmaDirectionEnum::NNCMX2DDR},
      {"DDR2NNCMX",mv::DmaDirectionEnum::DDR2NNCMX},
      {"BLOB2NNCMX",mv::DmaDirectionEnum::DDR2NNCMX},
      {"NNCMX2UPACMX",mv::DmaDirectionEnum::NNCMX2UPACMX},
      {"UPACMX2NNCMX",mv::DmaDirectionEnum::UPACMX2NNCMX},
      {"INPUT2NNCMX",mv::DmaDirectionEnum::DDR2NNCMX},
      {"NNCMX2OUTPUT",mv::DmaDirectionEnum::NNCMX2DDR},
      {"INPUT2DDR",mv::DmaDirectionEnum::DDR2DDR},
      {"DDR2OUTPUT",mv::DmaDirectionEnum::DDR2DDR}
};

namespace {
bool isStridingOp(mv::Data::OpListIterator opIt)
{
    if (opIt->getOpType() == "Slice" || opIt->getOpType() == "Crop")
    {
        auto const inputTensor = opIt->getInputTensor(mv::IO_TENSOR_INPUT);
        auto const inOrder = inputTensor->getOrder();
        auto const inShape = inputTensor->getShape();

        std::size_t dim = inOrder.lastContiguousDimensionIndex();
        for ( ; dim >= inOrder.firstContiguousDimensionIndex() ; --dim) {
            if (inShape[inOrder[dim]] > 1)
                break;
        }

        auto const sliceSz = inShape - opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT)->getShape();
        auto const majorDim = inOrder[dim];

        for (std::size_t idx = 0; idx < sliceSz.ndims(); idx++)
        {
            if (idx != majorDim && sliceSz[idx] != 0)
                return true;
        }
    }

    return false;
}

void retrieveChildConcats(mv::OpModel& om, std::set<std::string>& concats, mv::Data::OpListIterator opIt)
{
    for (auto child_itr = opIt.leftmostChild(); child_itr != om.opEnd(); ++child_itr )
    {
        // using a set to prevent duplicate entries
        auto child_op_type = child_itr->getOpType();
        if (child_op_type == "ImplicitConcat")
            concats.insert(child_itr->getName());
        // DMAs will be removed if this concat will be CMX-ed
        else if (child_itr->isImplicit() || 
                (child_op_type == "DMATask" && !child_itr->hasAttr("explicitRelocate")) ||
                (child_op_type == "DPUTask" && child_itr->hasAttr("splitted")))
            retrieveChildConcats(om, concats, child_itr);
    }
}

void ensureTopConcatCanBeCMXed(mv::OpModel& om, mv::Data::OpListIterator& opIt)
{
    auto globalParams = om.getGlobalConfigParams();
    std::size_t cluster_memory = globalParams->get<int>("cmx");
    std::size_t curr_cmx = opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT)->getClusterSize();

    // NOTE: if the top concat can not fit, accuracy issues might arise
    if (curr_cmx > cluster_memory)
        return;
    
    if (opIt->hasAttr("avoid_cmx_concat"))
        opIt->set<bool>("avoid_cmx_concat", false);

    std::set<std::string> child_concats;
    retrieveChildConcats(om, child_concats, opIt);

    // verify that the concats do not exceed cmx
    for (auto& concat_name : child_concats)
    {
        auto child_concat = om.getOp(concat_name);
        auto concat_size = child_concat->getOutputTensor(mv::IO_TENSOR_OUTPUT)->getClusterSize();
        // if exceeds do not CMX the child concat
        if (curr_cmx + concat_size > cluster_memory)
            child_concat->set<bool>("avoid_cmx_concat", true);
        else
            curr_cmx += concat_size;
    }
}

void moveTensorToSameMemLoc(mv::OpModel& om, mv::Data::OpListIterator opIt, std::vector<mv::Data::FlowListIterator> flows,
    mv::Tensor::MemoryLocation const& target, mv::Tensor::MemoryLocation const& intermediary)
{
    std::vector<mv::Data::OpListIterator> sinks;
    std::vector<std::size_t> slots;

    std::string const targetStr = target.toString();
    std::string const intermediaryStr = intermediary.toString();

    for (auto& flow : flows)
    {
        sinks.push_back(flow.sink());
        slots.push_back(flow->get<std::size_t>("sinkInput"));
    }

    auto intermediaryT = mv::insertDMAReplacementRemoveFlows(om, opIt,
                opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT),
                dmaDirectionStrings[targetStr + "2" + intermediaryStr],
                0, flows, slots, sinks,
                opIt->getName() + "_to" + intermediaryStr);
    intermediaryT->set<mv::Tensor::MemoryLocation>("Location", intermediary);
    auto iOp = om.getSourceOp(intermediaryT);
    iOp->set<bool>("explicitRelocate", true);

    flows.clear();
    for (auto flow = iOp.leftmostOutput(); flow != om.flowEnd(); ++flow)
    {
        flows.push_back(flow);
    }

    for (auto& flow : flows)
    {
        auto sink = flow.sink();
        auto targetT = mv::insertDMAReplacementRemoveFlows(om, opIt, intermediaryT,
                dmaDirectionStrings[intermediaryStr + "2" + targetStr], 0,
                {flow}, {flow->get<std::size_t>("sinkInput")}, {sink},
                flow.source()->getName() + "_" + sink->getName() + "_to" + targetStr);
        targetT->set<mv::Tensor::MemoryLocation>("Location", target);
        om.getSourceOp(targetT)->set<bool>("explicitRelocate", true);
    }
}

// If input location is the same as output location for Align op, there should be
// DMAs inserted to actually "align" the tensor.
void resolveAlign(mv::OpModel& om)
{
    auto alignOps = om.getOps("Align");

    for (auto op : alignOps)
    {
        const auto inputTensor = op->getInputTensor(mv::IO_TENSOR_INPUT);
        for (auto flow = op.leftmostOutput(); flow != om.flowEnd(); ++flow)
        {
            const auto outputLocation = flow->getTensor()->get<mv::Tensor::MemoryLocation>("Location");

            auto inputLocation = inputTensor->get<mv::Tensor::MemoryLocation>("Location");
            auto sourceOp = om.getSourceOp(inputTensor);

            while (sourceOp->isImplicit()
                    && (sourceOp->inputSlots() == 1)
                    && (inputLocation == outputLocation))
            {
                const auto scrInputTensor = sourceOp->getInputTensor(mv::IO_TENSOR_INPUT);
                inputLocation = scrInputTensor->get<mv::Tensor::MemoryLocation>("Location");
                sourceOp = om.getSourceOp(scrInputTensor);
            }

            if (inputLocation != outputLocation)
                continue;

            auto src = om.getSourceOp(inputTensor);
            for (const auto& flowName: inputTensor->getFlowNames())
            {
                auto srcFlow = om.getDataFlow(flowName);
                if (srcFlow.sink()->getName() == op->getName())
                {
                    const auto intermediary =
                        (inputLocation == mv::Tensor::MemoryLocation::NNCMX) ?
                        mv::Tensor::MemoryLocation::DDR :
                        mv::Tensor::MemoryLocation::NNCMX;
                    moveTensorToSameMemLoc(om, src, {srcFlow}, inputLocation, intermediary);
                    break;
                }
            }
        }
    }
}
}

void propagateImplicitOpsFromOutput(mv::Op& op, mv::OpModel& om)
{
    for (auto output : op.getOutputTensor())
        output->set<mv::Tensor::MemoryLocation>("Location", mv::Tensor::MemoryLocation::OUTPUT);

    for (auto input : op.getInputTensor())
    {
        auto previousOp = om.getSourceOp(input);
        if (previousOp->hasAttr("propagateLocation") && !previousOp->get<bool>("propagateLocation"))
            continue;
        else if (previousOp->isImplicit())
            propagateImplicitOpsFromOutput(*previousOp, om);
        else if (previousOp->get<std::string>("opType") == "DMATask")
            input->set<mv::Tensor::MemoryLocation>("Location", mv::Tensor::MemoryLocation::OUTPUT);
    }
}

void resolveImplicitConcats(mv::OpModel& om)
{
    auto concatOps = om.getOps("ImplicitConcat");

    std::function<bool(mv::Data::OpListIterator, mv::Data::OpListIterator,
                       std::vector<mv::Data::OpListIterator>&)> checkPath = [&](mv::Data::OpListIterator operation1,
            mv::Data::OpListIterator operation2,
            std::vector<mv::Data::OpListIterator>& operations) {
        return om.pathExists(operation1, operation2) && om.getPath(operation1, operation2, operations);
    };

    for (auto& concat : concatOps) {

        mv::Data::TensorIterator outputTensor = concat->getOutputTensor(mv::IO_TENSOR_OUTPUT);
        mv::Tensor::MemoryLocation outLocation = outputTensor->get<mv::Tensor::MemoryLocation>("Location");

        if (outLocation != mv::Tensor::MemoryLocation::DDR)
            continue;

        // just to speed up checking assume that if we find at least one unsuitable concat
        // stop iterating
        bool found = false;

        std::vector<mv::Data::FlowListIterator> concats;
        concats.reserve(concat.childrenSize());

        for (mv::Data::FlowSiblingIterator flow = concat.leftmostOutput(); flow != om.flowEnd(); ++flow) {
            mv::Data::OpListIterator nextOp = flow.sink();

            if (nextOp->getOpType() != "ImplicitConcat"
                    || flow->getTensor()->get<mv::Tensor::MemoryLocation>("Location") != outLocation)
                continue;

            // check that one concat follows the other
            if (!found) {

                if (concats.empty()) {
                    concats.push_back(flow);
                    continue;
                }

                // find the path between two concats
                std::vector<mv::Data::OpListIterator> operations;

                // because of uncertainty of operations' order check both options.
                if (!checkPath(concats.front().sink(), nextOp, operations) &&
                        !checkPath(nextOp, concats.front().sink(), operations))
                    break;

                // also check that there is any DPU task on the path between concats
                found = !operations.empty()
                    && std::find_if(operations.begin(), operations.end(),
                                                           [&](mv::Data::OpListIterator& operation) {
                    return operation->isHardwarizable();
                }) != operations.end();

                if (!found)
                    break;
            }

            concats.push_back(flow);
        }

        if (!found || concats.size() < 2)
            continue;

        moveTensorToSameMemLoc(om, concat, concats,
            mv::Tensor::MemoryLocation::DDR, mv::Tensor::MemoryLocation::NNCMX);
        
        // accuracy issues when the top concat is not CMX-ed
        ensureTopConcatCanBeCMXed(om, concat);
    }
}

void resolveImplicitOperationsOp(mv::Data::OpListIterator opIt, const mv::pass::PassEntry& pass, mv::ComputationModel& model)
{
    mv::OpModel om(model);
    mv::DataModel dm(model);

    const std::vector<std::string> inputInOutputOps = {"Concat",
        "ImplicitConcat", "ImplicitReshape", "ImplicitPermute",
        "ImplicitOutput", "ImplicitUnion", "ImplicitJoin", "Copy",
        "Align", "ImplicitResample"};
    
    const std::vector<std::string> outputInInputOps = {"Slice",
        "Crop", "ImplicitInputSlice", "ImplicitInput"};

    //TODO::the following attributes need to come either from JSON config or from OP definition
    const auto& opType = opIt->getOpType();
    if (std::find(inputInOutputOps.begin(), inputInOutputOps.end(), opType) != inputInOutputOps.end())
        opIt->set<mv::ImplicitFlow>("ImplicitFlow", mv::ImplicitFlow(mv::ImplicitFlow::INPUT_IN_OUTPUT));
    else if (std::find(outputInInputOps.begin(), outputInInputOps.end(), opType) != outputInInputOps.end())
        opIt->set<mv::ImplicitFlow>("ImplicitFlow", mv::ImplicitFlow(mv::ImplicitFlow::OUTPUT_IN_INPUT));

    if (!opIt->hasAttr("ImplicitFlow"))
        return;

    auto implicitFlow = opIt->get<mv::ImplicitFlow>("ImplicitFlow");

    // The attribute is defined at leyer definition... But someone (like JSON)
    // may override the decision if it get's a candidate or not.
    // If it's not a candidate skip
    if (!implicitFlow.isCandidate())
            return;

    if (implicitFlow.isImplicit())
    {
        //From design perspective, no pass should decide implictness before this pass
        pass.log(mv::Logger::MessageType::Warning, "found OP " + opIt->getName() + " already decided as implicit. Skipping");
        return;
    }

    if (opType == "Slice" && opIt->hasAttr("force_slice_in_DDR") && opIt->get<bool>("force_slice_in_DDR"))
        updateConcatForSliceOpInDdr(opIt, om);

    pass.log(mv::Logger::MessageType::Debug, "Solving: # " + opIt->getName() + " #");

    //currently this phase will assume that memory locality is the only solution for implicitness.
    //TODO:: if other conditions appear, then structure them separately

    //TODO:: for now multiple output tensors is not really supported...
    // once it becomes supported, revise the logic.

    auto inputTensors = opIt->getInputTensor();
    auto outputTensor = opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT);

    auto outputLocation  = outputTensor->get<mv::Tensor::MemoryLocation>("Location");

    int ctr =  0;
    std::vector<mv::Data::FlowSiblingIterator> flowsToRemove;
    std::vector<mv::Data::TensorIterator> compensationOutputs;
    std::vector<std::size_t> sinkIndexes;

    if (implicitFlow.getCompensationDirection() == mv::ImplicitFlow::INPUT_IN_OUTPUT)
    {
        auto sourceFlowStart = opIt.leftmostInput();
        for (mv::Data::FlowSiblingIterator sourceFlow(sourceFlowStart); sourceFlow != om.flowEnd() ; ++ sourceFlow)
        {
            auto inputTensor = sourceFlow->getTensor();
            auto inputLocation = inputTensor->get<mv::Tensor::MemoryLocation>("Location");
            pass.log(mv::Logger::MessageType::Debug, "Input tensor " + inputTensor->getName() + " location " + inputLocation.toString());
            pass.log(mv::Logger::MessageType::Debug, "Output tensor " + outputTensor->getName() + " location " + outputLocation.toString());

//                if (inputLocation != outputLocation &&
//                        !(inputLocation == mv::Tensor::MemoryLocation::DDR &&
//                          outputLocation == mv::Tensor::MemoryLocation::OUTPUT))

            if (inputLocation != outputLocation)
            {
                //TODO:: QUant params inherited for concat
                //TODO:: PRONE TO ERRORS! correlate with Class Direction
                mv::QuantizationParams inQuantParams = inputTensor->getQuantParams();
                const std::string directionString = inputLocation.toString() + "2" + outputLocation.toString();
                auto compensatorOutput = om.dMATask(opIt->getName() + "_copy" + std::to_string(ctr),
                                                inputTensor,
                                                dmaDirectionStrings[directionString],
                                                0);

                //NOTE: When the dilated convolution is streamed, the dmas could be placed between
                //the dputtask and the concat which is designed for streaming so in that cases we
                //need to check if the next concat of the streaming has the attributes
                auto sinkOp = mv::findSinkLayers(dm, opIt->getOutputTensor(0))[0];
                if (opIt->hasAttr("dilatedWidthConcat") && opIt->get<bool>("dilatedWidthConcat"))
                {
                    std::size_t slot = 0;
                    for (std::size_t inputConcatTensorIdx = 0; inputConcatTensorIdx < opIt->getInputTensor().size();
                            inputConcatTensorIdx++)
                        if (opIt->getInputTensor()[inputConcatTensorIdx]->getName() == inputTensor->getName())
                            slot = inputConcatTensorIdx;
                    //NOTE: only the tensor which goes to ddr, the dst should have the dilated strides
                    compensatorOutput->set<bool>("dilatedWidthConcat", true);
                    compensatorOutput->set<unsigned>("dilationFactor",
                                                        opIt->get<unsigned>("dilationFactor"));
                    compensatorOutput->set<std::size_t>("inputConcatTensorIdx", slot);
                    compensatorOutput->set<std::size_t>("lineofConcatHeight",
                                                opIt->get<std::size_t>("lineofConcatHeight"));
                }
                else if (sinkOp->hasAttr("dilatedWidthConcat") && sinkOp->get<bool>("dilatedWidthConcat"))
                {
                    //NOTE: they are the streaming operations SO they will all have same coordinates
                    auto subConvOp = om.getSourceOp(opIt->getInputTensor()[0]);
                    //might be a "crop" op inbetween
                    while (subConvOp->isImplicit())
                        subConvOp = om.getSourceOp(subConvOp->getInputTensor()[0]);

                    std::size_t slot = subConvOp->get<std::vector<std::size_t>>("subConvsCoordinates")[1];
                    //NOTE: only the tensor which goes to ddr, the dst should have the dilated strides
                    compensatorOutput->set<bool>("dilatedWidthConcat", true);
                    compensatorOutput->set<unsigned>("dilationFactor",
                                                        sinkOp->get<unsigned>("dilationFactor"));
                    compensatorOutput->set<std::size_t>("inputConcatTensorIdx", slot);
                    compensatorOutput->set<std::size_t>("lineofConcatHeight",
                                                subConvOp->get<std::vector<std::size_t>>("subConvsCoordinates")[0]);

                    auto previousOp = om.getSourceOp(inputTensor);
                    if (previousOp->hasAttr("streamKId"))
                    {
                        auto streamKId = previousOp->get<unsigned>("streamKId");
                        auto symmetrical_first_dimensionK = previousOp->get<std::size_t>("symmetrical_first_dimensionK");
                        compensatorOutput->set<std::size_t>("symmetrical_first_dimensionK",
                                                            symmetrical_first_dimensionK);
                        compensatorOutput->set<unsigned>("streamKId", streamKId);
                    }
                    else if (previousOp->hasAttr("streamHId"))
                    {
                        auto streamHId = previousOp->get<unsigned>("streamHId");
                        auto symmetrical_first_dimensionH = previousOp->get<std::size_t>("symmetrical_first_dimensionH");
                        compensatorOutput->set<std::size_t>("symmetrical_first_dimensionH",
                                                            symmetrical_first_dimensionH);
                        compensatorOutput->set<unsigned>("streamHId", streamHId);
                    }

                }
                // For pipelining, pass strategy decision to overlap dpu, dma tasks
                if(opIt->hasAttr("schedule_for_dpu_dma_overlap") ||
                        sinkOp->hasAttr("schedule_for_dpu_dma_overlap"))
                {
                    unsigned pipelineId = 0;
                    if(opIt->hasAttr("schedule_for_dpu_dma_overlap"))
                        pipelineId = opIt->get<unsigned>("schedule_for_dpu_dma_overlap");
                    else
                        pipelineId = sinkOp->get<unsigned>("schedule_for_dpu_dma_overlap");
                    
                    om.getSourceOp(compensatorOutput)->set<unsigned>("schedule_for_dpu_dma_overlap", pipelineId);
                }

                if (compensatorOutput->hasAttr("quantParams"))
                {
                    if (inQuantParams.hasAttr("shift") && inQuantParams.hasAttr("mult"))
                    {
                        compensatorOutput->get<mv::QuantizationParams>("quantParams").quantize(inQuantParams.getShift(), inQuantParams.getMult());
                    }
                }

                compensatorOutput->set<mv::Tensor::MemoryLocation>("Location", outputLocation);
                auto sinkIdx = sourceFlow->get<std::size_t>("sinkInput");

                pass.log(mv::Logger::MessageType::Debug, "Adding new DMA OP: # " + compensatorOutput->getName() +
                                                            " # after tensor: # " + inputTensor->getName() + " #");
                om.getSourceOp(compensatorOutput)->set<unsigned>("opId", opIt->get<unsigned>("opId"));

                flowsToRemove.push_back(sourceFlow);
                compensationOutputs.push_back(compensatorOutput);
                sinkIndexes.push_back(sinkIdx);

                ctr++;
            }
        }
        for (std::size_t flowIdx = 0; flowIdx < flowsToRemove.size() ; flowIdx++)
        {
            pass.log(mv::Logger::MessageType::Debug,"Setting # " + compensationOutputs[flowIdx]->getName() +
                                                    " # as input at slotIdx: " + std::to_string(sinkIndexes[flowIdx]));
            opIt->setInputTensor(compensationOutputs[flowIdx],sinkIndexes[flowIdx], false);
            om.undefineFlow(flowsToRemove[flowIdx]);
            om.defineFlow(compensationOutputs[flowIdx], opIt, sinkIndexes[flowIdx]);
        }

        opIt->get<mv::ImplicitFlow>("ImplicitFlow").resolve();

    }
    else if (implicitFlow.getCompensationDirection() == mv::ImplicitFlow::OUTPUT_IN_INPUT)
    {
        //TODO:: REVIEW :: normally if the ImplicitFlow trait of the tensor is to compensate on output
        // it should not have multiple inputs;

        if(inputTensors.size() > 1)
            throw mv::AttributeError("resolevImplicitOperationsFcn", " tensor " + opIt->getName() +
                                        " of type " + opIt->getOpType() +
                                        " has multiple inputs but has Implicit Compensation set to OUTPUT");

        auto inputTensor = inputTensors[0];
        auto inputLocation = inputTensor->get<mv::Tensor::MemoryLocation>("Location");
        pass.log(mv::Logger::MessageType::Debug, "Input tensor " + inputTensor->getName() + " location " + inputLocation.toString());
        pass.log(mv::Logger::MessageType::Debug, "Output tensor " + outputTensor->getName() + " location " + outputLocation.toString());

        if(inputLocation != outputLocation)
        {

            const std::string directionString = inputLocation.toString() + "2" + outputLocation.toString();

            std::vector<mv::Data::OpListIterator> opsToLink;
            std::vector<std::size_t> inputSlots;
            auto sourceFlowStart = opIt.leftmostOutput();

            for (mv::Data::FlowSiblingIterator sinkFlow(sourceFlowStart); sinkFlow != om.flowEnd(); ++sinkFlow)
            {
                opsToLink.push_back(sinkFlow.sink());
                inputSlots.push_back(sinkFlow->get<std::size_t>("sinkInput"));
                flowsToRemove.push_back(sinkFlow);
            }

            mv::QuantizationParams outQuantParams = outputTensor->getQuantParams();
            auto compensatorOutput = om.dMATask(opIt->getName() + "_copy" + std::to_string(ctr),
                                                    outputTensor,
                                                    dmaDirectionStrings[directionString],
                                                    0);

            if (compensatorOutput->hasAttr("quantParams"))
            {
                if (outQuantParams.hasAttr("shift") && outQuantParams.hasAttr("mult"))
                {
                    compensatorOutput->get<mv::QuantizationParams>("quantParams").quantize(outQuantParams.getShift(), outQuantParams.getMult());
                }
            }

            // For a slice with "force_slice_in_DDR" attribute DMA is injected just to have the tensor go through DDR and then back to CMX.
            // In such case "broadcast" attribute should be set in the DMA input tensor in case it was in input to slice operation to have
            // an indication for DMA task that whole tensor is present in each cluster and have single DMA task generated by runtime model
            if (opType == "Slice" && opIt->hasAttr("force_slice_in_DDR") && opIt->get<bool>("force_slice_in_DDR") &&
                inputTensor->hasAttr("broadcasted") && inputTensor->get<bool>("broadcasted"))
            {
                outputTensor->set<bool>("broadcasted", true);
            }

            // For a slice with "force_slice_in_DDR" attribute DMA is injected just to have the tensor go through DDR and then back to CMX.
            // In such case "broadcast" attribute should be set in the DMA input tensor in case it was in input to slice operation to have
            // an indication for DMA task that whole tensor is present in each cluster and have single DMA task generated by runtime model
            if (opType == "Slice" && opIt->hasAttr("force_slice_in_DDR") && opIt->get<bool>("force_slice_in_DDR") &&
                inputTensor->hasAttr("broadcasted") && inputTensor->get<bool>("broadcasted"))
            {
                outputTensor->set<bool>("broadcasted", true);
            }

            pass.log(mv::Logger::MessageType::Debug,"Adding new DMA OP: # " + compensatorOutput->getName() +
                                                        " as output to # " + opIt->getName());
            om.getSourceOp(compensatorOutput)->set<unsigned>("opId", opIt->get<unsigned>("opId"));

            // TODO: NOTE: I should be using the relocate function.... but that may fail in case the "output" of slice is forced.
            //Technically, for compensation I should not be adding "new dma" to the slice "outputTensor" but rather
            //but a new tensor as the Slice output and DMA between them. But the API is not really friendly for this
            //case so the "forced" attribute will be inherited by the output of the DMA itself....

            compensatorOutput->set<mv::Tensor::MemoryLocation>("Location", outputLocation);
            if(outputTensor->get<mv::Tensor::MemoryLocation>("Location").isForced())
                compensatorOutput->get<mv::Tensor::MemoryLocation>("Location").force();

            outputTensor->set<mv::Tensor::MemoryLocation>("Location", inputLocation);

            for (std::size_t flowIdx = 0; flowIdx < flowsToRemove.size(); flowIdx++)
            {
                om.undefineFlow(flowsToRemove[flowIdx]);
            }
            for(std::size_t op = 0 ; op < opsToLink.size(); ++op)
            {
                pass.log(mv::Logger::MessageType::Debug," Setting # " + compensatorOutput->getName() +
                                                            "# as input to: # " + opsToLink[op]->getName() +
                                                            "# at slotIdx: " + std::to_string(inputSlots[op]));

                opsToLink[op]->setInputTensor(compensatorOutput, inputSlots[op], false);
                om.defineFlow(compensatorOutput,opsToLink[op], inputSlots[op]);
            }
        }
        opIt->get<mv::ImplicitFlow>("ImplicitFlow").resolve();

        // DPUs have no way of resolving input strides implicitly
        // hence they need to be provided the sliced compact tensor directly
        // Exception from this rule can be considered cases where sridimg is resolved
        // as part of activation sparsity logic; currently only DilatedConv uses sparsity
        // so it satisfies the stides also.
        if (outputLocation == mv::Tensor::MemoryLocation::NNCMX && isStridingOp(opIt))
        {
            for (auto tensor : opIt->getOutputTensor())
            {
                std::vector<mv::Data::FlowListIterator> removableFlows;
                std::vector<std::size_t> inSlots;
                std::vector<mv::Data::OpListIterator> flowSinks;
                auto const& flows = tensor->get<std::set<std::string>>("flows");
                for(auto const& flowStr : flows)
                {
                    auto flow = dm.getDataFlow(flowStr);
                    auto sinkOp = flow.sink();
                    if (sinkOp->getOpType() == "DPUTask" &&
                        !(sinkOp->hasAttr("activationSparsityCompilerSolvingForDilatedConv") &&
                        sinkOp->get<bool>("activationSparsityCompilerSolvingForDilatedConv")))
                    {
                        removableFlows.push_back(flow);
                        flowSinks.push_back(sinkOp);
                        inSlots.push_back(flow->get<std::size_t>("sinkInput"));
                    }
                }
                if (!removableFlows.empty()) {
                    auto dmaTaskOut = mv::insertDMAReplacementRemoveFlows(om, opIt, tensor,
                        mv::DmaDirection(mv::DmaDirectionEnum::NNCMX2DDR),
                        0, removableFlows, inSlots, flowSinks,
                        tensor->getName() + "_unstrided");
                    dmaTaskOut->set<mv::Tensor::MemoryLocation>("Location", mv::Tensor::MemoryLocation::DDR);
                }
            }
        }

        // In cases where an OUTPUT_IN_INPUT implicit op goes into an
        // ImplicitConcat op, there needs to be a DMA to remove
        // extra strides and make the tensor compact.
        // If input and output locations differ, that is taken care of. However,
        // when the locations are identical, there need to be extra DMAs added to
        // solve this case.
        std::vector<mv::Data::FlowListIterator> flows;
        for (const auto& flowName: outputTensor->getFlowNames())
        {
            auto flow = om.getDataFlow(flowName);
            if (flow.sink()->getOpType() == "ImplicitConcat")
            {
                flows.push_back(flow);
            }
        }

        if (inputLocation == mv::Tensor::MemoryLocation::DDR && !flows.empty()) {
            moveTensorToSameMemLoc(om, opIt, flows, mv::Tensor::MemoryLocation::DDR, mv::Tensor::MemoryLocation::NNCMX);
        }
    }
}

void resolveImplicitOperationsFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&)
{

    MV_PROFILED_FUNCTION(MV_PROFILE_PASS)
    mv::OpModel om(model);

    // Propagate throught implicit layers and set their output tensor location to OUTPUT
    propagateImplicitOpsFromOutput(*om.getOutput(), om);
    resolveAlign(om);

    for( auto opIt = om.opBegin(); opIt != om.opEnd(); ++ opIt)
    {
        resolveImplicitOperationsOp(opIt, pass, model);
    }

    // Resolve the situation when the concat writes to more than one concat in DDR
    resolveImplicitConcats(om);
}

//void ensureNoOddDMAsBetweenDDROutputFcn(const mv::pass::PassEntry& , mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&)
//{

//    mv::OpModel om(model);
//    bool changedLocation;
//    for( auto opIt = om.opBegin(); opIt != om.opEnd(); ++ opIt)
//    {
//        mv::Data::TensorIterator outputTensor;
//        if (opIt->getOpType() != "Output")
//        {
//            outputTensor = opIt->getOutputTensor(0);
//            auto outputLocation  = outputTensor->get<mv::Tensor::MemoryLocation>("Location");
//            if (outputLocation == mv::Tensor::MemoryLocation::OUTPUT)
//            {
//                for (auto input : opIt->getInputTensor())
//                {
//                    auto previousOp = om.getSourceOp(input);
//                    if (previousOp->isImplicit())
//                        for (auto inputTensor : previousOp->getInputTensor())
//                        {
//                            auto parentOp = om.getSourceOp(inputTensor);
//                            if (parentOp->getOpType() == "DMATask" &&
//                                    parentOp->hasAttr("direction") &&
//                                    parentOp->get<mv::DmaDirection>("direction") ==
//                                    mv::DmaDirectionEnum::NNCMX2DDR)
//                            {
//                                changedLocation = true;
//                                parentOp->getOutputTensor()[0]->set<mv::Tensor::MemoryLocation>("Location",
//                                                    mv::Tensor::MemoryLocation::OUTPUT);
//                            }
//                        }
//                }
//            }
//        }
//    }
//    if (changedLocation)
//    {
//        for( auto opIt = om.opBegin(); opIt != om.opEnd(); ++ opIt)
//        {
//            if (opIt->getOpType() == "Output")
//            {
//                auto previousOp = om.getSourceOp(opIt->getInputTensor()[0]);
//                for (auto inp : previousOp->getInputTensor())
//                    inp->set<mv::Tensor::MemoryLocation>("Location",
//                                                         mv::Tensor::MemoryLocation::OUTPUT);
//            }
//        }
//    }

//}
