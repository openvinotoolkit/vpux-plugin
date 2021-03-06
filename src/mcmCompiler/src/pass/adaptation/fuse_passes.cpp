//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/pass/pass_registry.hpp"
#include "include/mcm/op_model.hpp"
#include "include/mcm/computation/model/data_model.hpp"
#include "include/mcm/tensor/math.hpp"
#include "include/mcm/utils/custom_strings.hpp"
#include "include/mcm/utils/warning_manager.hpp"
#include "include/mcm/utils/custom_math.hpp"
#include "include/mcm/pass/pass_utils.hpp"
#include "include/mcm/target/kmb/pwl/pwl_dyn_fit.hpp"
#include <functional>
#include <limits.h>

enum TableSource { None = 0, TargetDescriptor, Generation };

typedef void fuse_function(mv::Data::OpListIterator& opIt, mv::Data::OpListIterator& parentIt, const std::string& opType,
                         mv::OpModel& om, mv::TargetDescriptor& td);

void fuseBiasFcn(mv::Data::OpListIterator& opIt, mv::ComputationModel& model, const std::string& opType, mv::TargetDescriptor& td);
template <fuse_function> void fusePPEBaseFcn(mv::Data::OpListIterator& opIt, mv::ComputationModel& model, const std::string& opType, mv::TargetDescriptor& td);
void fuseMinimumFcn(mv::Data::OpListIterator& opIt, mv::ComputationModel& model, const std::string& opType, mv::TargetDescriptor& td);
void fuseMaximumFcn(mv::Data::OpListIterator& opIt, mv::ComputationModel& model, const std::string& opType, mv::TargetDescriptor& td);
void fuseEltwiseFcn(mv::Data::OpListIterator& opIt, mv::ComputationModel& model, const std::string& opType, mv::TargetDescriptor& td);
void fuseScaleFcn(mv::Data::OpListIterator& opIt, mv::ComputationModel& model, const std::string& opType, mv::TargetDescriptor& td);
void fuseBatchNormFcn(mv::Data::OpListIterator& opIt, mv::ComputationModel& model, const std::string& opType, mv::TargetDescriptor& td);

bool optimisableWithPWL(mv::Data::OpListIterator& op, mv::TargetDescriptor& td, TableSource& ts);

static void fusePostOpsFcn(const mv::pass::PassEntry&, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&,
                           mv::Element&);

/* Fuse funcs added to base template */
void fuse_custom_pwl(mv::Data::OpListIterator& opIt, mv::Data::OpListIterator& parentIt, const std::string& opType,
                         mv::OpModel& om, mv::TargetDescriptor& td);
void fuse_usual_ppe(mv::Data::OpListIterator& opIt, mv::Data::OpListIterator& parentIt, const std::string& opType,
                         mv::OpModel& om, mv::TargetDescriptor& td);

namespace mv
{
    namespace pass
    {

        MV_REGISTER_PASS(FusePostOps)
        .setFunc(fusePostOpsFcn)
        .setDescription(
            "Fuses all the ops that will be converted to PPE Tasks and can be handled through hardware. "
            "Scale, Batch Norm from My-X\n"
        );
    }
}

/* Function determines if an optimisation exists with Custom PWL table */
bool optimisableWithPWL(mv::Data::OpListIterator& op, mv::TargetDescriptor& td, TableSource& ts) {
    const std::vector<std::string> generatable_activations = {"Mish"};

    auto activation = op->getOpType();
    auto d_type = op->getInputTensor(0)->getDType();

    auto PWLTableMap = td.generalTargetConfigs().pwlTables;

    /* Check if custom PWL table is specified in target descriptor */
    mv::PWLTableType pwl_t;
    pwl_t.activation = activation;
    pwl_t.dtype = d_type;
    if (PWLTableMap.find(pwl_t) != PWLTableMap.end()) {
        ts = TableSource::TargetDescriptor;
        return true;
    }

    /* Check if custom PWL can be generated for activation and datatype */
    if (d_type == mv::DType("UInt8") && std::find(generatable_activations.begin(), generatable_activations.end(), activation) != generatable_activations.end()) {
        ts = TableSource::Generation;
        return true;
    }

    ts = TableSource::None;
    return false;
}

void fusePostOpsFcn(const mv::pass::PassEntry&, mv::ComputationModel& model, mv::TargetDescriptor& td, mv::Element&,
                    mv::Element&) {
    mv::OpModel om(model);
    std::shared_ptr<mv::Element> globalParams = model.getGlobalConfigParams();
    bool PWLUsage = globalParams->hasAttr("PWLUsage") ? globalParams->get<bool>("PWLUsage") : false;
    std::unordered_map<std::string, std::pair<uint32_t,
                       std::function<void(mv::Data::OpListIterator&, mv::ComputationModel&, const std::string&, mv::TargetDescriptor&)>>>
            fuseTaskMap =   {{"Bias",       {0, fuseBiasFcn}},
                            {"Sigmoid",     {1, fusePPEBaseFcn<fuse_usual_ppe>}},
                            {"Tanh",        {2, fusePPEBaseFcn<fuse_usual_ppe>}},
                            {"Relu",        {3, fusePPEBaseFcn<fuse_usual_ppe>}},
                            {"Prelu",       {4, fusePPEBaseFcn<fuse_usual_ppe>}},
                            {"LeakyRelu",   {5, fusePPEBaseFcn<fuse_usual_ppe>}},
                            {"Mish",        {6, fusePPEBaseFcn<fuse_usual_ppe>}},
                            {"Minimum",     {7, fuseMinimumFcn}},
                            {"Maximum",     {8, fuseMaximumFcn}}};

    /* ToDo: In case more activations will have this handling available, design should be generalized with lists/maps */
    bool PPELReluAccuracy = checkPPEAccuracy(model);

    if (PPELReluAccuracy) {
        /* Fuse Bias ops first */
        std::vector<mv::Data::OpListIterator> biasOperations = om.getOps("Bias");

        for (auto bias : biasOperations)
            fuseTaskMap["Bias"].second(bias, model, "Bias", td);

        provideAccuracyinPPEs(model);

        /* Bias ops already fused */
        fuseTaskMap.erase("Bias");

        /* LeakyRelu ops already fused */
        fuseTaskMap.erase("LeakyRelu");
    }

    /* Collect all fusable op types from fuseTaskMap */
    std::vector<std::string> fuse_types;
    std::vector<uint32_t> fuse_priority;
    std::vector<std::size_t> fuse_order;
    std::size_t count = 0;
    for(auto map_it = fuseTaskMap.begin(); map_it != fuseTaskMap.end(); ++map_it)
    {
        /* Get fusable op type */
        fuse_types.push_back(map_it->first);

        /* Get fusable type priority */
        fuse_priority.push_back(map_it->second.first);

        /* Init order vector */
        fuse_order.push_back(count);
        ++count;
    }

    /* Get fusing order based on priority vector */
    std::sort(fuse_order.begin(), fuse_order.end(), [&](std::size_t a, std::size_t b) { return fuse_priority[a] < fuse_priority[b];});

    std::unordered_map<std::string, std::vector<mv::Data::OpListIterator>> operationsOfType =
            om.getOpsOfTypes(fuse_types);

    // NOTE: Iterate the fusing order vector to respect priorities set in fuseTaskMap
    for (auto order_it = fuse_order.begin(); order_it != fuse_order.end(); order_it++)
    {
        /* Get fuse type at current step */
        auto type = fuse_types[*order_it];

        /* Get fusing function for current type */
        auto fuseFunctor = (fuseTaskMap.at(type)).second;

        for (auto opIt = operationsOfType[type].begin(); opIt != operationsOfType[type].end(); ++opIt) {
            TableSource ts = TableSource::None;

            bool override_pwl = (type == "LeakyRelu" && PPELReluAccuracy);

            if (!override_pwl && PWLUsage && optimisableWithPWL(*opIt, td, ts)) {
                (*opIt)->set<int>("PWLSource", static_cast<int>(ts));
                fusePPEBaseFcn<fuse_custom_pwl>(*opIt, model, type, td);
            }
            else
            {
                fuseFunctor(*opIt, model, type, td);
            }
        }

    }
}

mv::Data::OpListIterator linkNewOperationsFuse(mv::Data::OpListIterator parentOpIt, mv::Data::TensorIterator sourceTensor, mv::OpModel & om, mv::Data::OpListIterator opIt)
{
    //Important: do not change the order of this ops
    std::vector<mv::Data::OpListIterator> opsToLink;
    std::vector<std::size_t> inputSlots;
    for (mv::Data::FlowSiblingIterator sinkFlow(opIt.leftmostOutput()); sinkFlow != om.flowEnd(); ++sinkFlow)
    {
        opsToLink.push_back(sinkFlow.sink());
        inputSlots.push_back(sinkFlow->get<std::size_t>("sinkInput"));
    }

    auto paramOp = opIt.leftmostParent();
    while(paramOp != om.opEnd())
    {
        if (paramOp->getOpType() == "Constant" || paramOp->getOpType() == "ConstantInt" || paramOp->getOpType() == "ConstantDataElement")
        {
            auto backUp = paramOp;
            ++paramOp;
            om.removeOp(backUp);
        }
        else
            ++paramOp;
    }

    om.removeOp(opIt);
    opIt = parentOpIt;

    for (unsigned j = 0; j < opsToLink.size(); ++j)
    {
        opsToLink[j]->setInputTensor(sourceTensor, inputSlots[j], false);
        om.defineFlow(sourceTensor, opsToLink[j], inputSlots[j]);
    }

    return opIt;
}

void fuseBiasFcn(mv::Data::OpListIterator& opIt, mv::ComputationModel& model, const std::string& /*opType*/, mv::TargetDescriptor&) {
    using namespace mv;
    mv::OpModel om(model);
    mv::DataModel dm(model);
    std::vector<mv::Data::TensorIterator> sourceTensors;

    // Check for op patterns between bias & DPUTask to fuse into
    mv::Data::OpListIterator lastIt;
    // Op patterns from asymmetric stride Conv replacement in splitOperationSlicingV2()
    const std::vector<std::vector<std::string>> patterns = {
        {"Concat"},
        {"Slice","Identity","Concat"},
        {"Identity","Concat"},
    };
    for (auto& pattern : patterns)
    {
        if (mv::matchPattern(pattern, om.getSourceOp(opIt->getInputTensor(mv::IO_TENSOR_INPUT)), lastIt, model))
        {
            sourceTensors = lastIt->getInputTensor();
            break;
        }
    }

    // If pattern not found, use original bias op
    if (sourceTensors.empty())
        sourceTensors.push_back(opIt->getInputTensor(mv::IO_TENSOR_INPUT));

    for(auto& sourceTensor: sourceTensors)
    {
        auto parentOpIt = om.getSourceOp(sourceTensor);
        if (parentOpIt->getOpType() == "Conv" ||
            parentOpIt->getOpType() == "FullyConnected" ||
            parentOpIt->getOpType() == "DepthwiseConv" ||
            parentOpIt->getOpType() == "Deconv" ||
            parentOpIt->getOpType() == "MaxPool")
        {
            auto bias = *opIt->getInputTensor(1);
            if (parentOpIt->hasAttr("bias"))
            {
                auto biasTensor = model.getTensor(parentOpIt->get<std::string>("bias"));
                biasTensor->add(bias);
            }
            else
            {
                std::string biasTensorName = mv::createBiasName(parentOpIt->getName());
                mv::Data::TensorIterator biasTensor;
                if (bias.hasAttr("quantParams"))
                    biasTensor = dm.defineTensor(mv::Tensor(biasTensorName, bias.getShape(), bias.getDType(), bias.getOrder(), bias.getData(), bias.get<mv::QuantizationParams>("quantParams")) );
                else
                    biasTensor = dm.defineTensor(mv::Tensor(biasTensorName, bias.getShape(), bias.getDType(), bias.getOrder(), bias.getData()) );
                om.addAttr(parentOpIt, "bias", biasTensor->getName());
            }
        }
    }

    auto biasOutputMemoryLocation = opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT)->get<mv::Tensor::MemoryLocation>("Location");
    auto parentOpIt = om.getSourceOp(opIt->getInputTensor(mv::IO_TENSOR_INPUT));
    auto sourceTensor = parentOpIt->getOutputTensor(mv::IO_TENSOR_OUTPUT);
    sourceTensor->setDType(opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT)->getDType());
    sourceTensor->setQuantParams(opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT)->getQuantParams());
    opIt = linkNewOperationsFuse(parentOpIt, sourceTensor, om, opIt);
    if (biasOutputMemoryLocation.isForced())
    {
        opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT)->set<mv::Tensor::MemoryLocation>("Location", biasOutputMemoryLocation);
    }
}

void fuseScaleFcn(mv::Data::OpListIterator& opIt, mv::ComputationModel& model, const std::string& /*opType*/, mv::TargetDescriptor&) {
    mv::OpModel om(model);
    auto parentOpIt = om.getSourceOp(opIt->getInputTensor(0));
    auto scaleOutputMemoryLocation = opIt->getOutputTensor(0)->get<mv::Tensor::MemoryLocation>("Location");
    if (parentOpIt->getOpType() == "Conv")
    {
        auto scale = *opIt->getInputTensor(1);
        parentOpIt->getInputTensor(1)->multiply(scale);
        if (parentOpIt->hasAttr("bias"))
        {
            auto biasTensor = model.getTensor(parentOpIt->get<std::string>("bias"));
            biasTensor->multiply(scale);
        }
        auto sourceTensor = parentOpIt->getOutputTensor(0);
        sourceTensor->setDType(opIt->getOutputTensor(0)->getDType());
        sourceTensor->setQuantParams(opIt->getOutputTensor(0)->getQuantParams());
        opIt = linkNewOperationsFuse(parentOpIt, sourceTensor, om, opIt);
        if (scaleOutputMemoryLocation.isForced())
            opIt->getOutputTensor(0)->set<mv::Tensor::MemoryLocation>("Location", scaleOutputMemoryLocation);
    }
}

/// Refer to src/mcmCompiler/src/pass/kmb_adaptation/compute_output_dtype.cpp: decideOutputDataType()
bool floatOutput(mv::Data::OpListIterator& op, mv::ComputationModel& model) {
    mv::OpModel om(model);
    auto returnedParams = model.getGlobalConfigParams();

    auto perTensorScale = [](const mv::Data::TensorIterator& tensor) {
        const auto& channelScale = tensor->get<mv::QuantizationParams>("quantParams").getScale();
        return std::all_of(channelScale.begin(), channelScale.end(), [&](double x) {
            return std::abs(x - channelScale[0]) <= 0.01f;
        });
    };

    bool floatOutput = op->getOutputTensor(mv::IO_TENSOR_OUTPUT)->isFloatingPointType();
    auto opType = op->getOpType();

    if (returnedParams->hasAttr("PredictionOfQuantizationOutput") &&
        returnedParams->get<bool>("PredictionOfQuantizationOutput")) {
        floatOutput = false;
    } else {
        if (op->isHardwarizable()) {
            bool inputQuantized = true;
            /// check conv with fp16 weights and also no quan params
            /// need do with fp16 conv
            for (size_t i = 0; i < (opType == "Eltwise" || opType == "Conv" ? 2 : 1); ++i) {
                inputQuantized &= !op->getInputTensor(i)->isFloatingPointType() &&
                                  !op->getInputTensor(i)->getQuantParams().isEmpty();
            }
            bool outputQuantized = !floatOutput &&
                                   !op->getOutputTensor(mv::IO_TENSOR_OUTPUT)->getQuantParams().isEmpty();

            if (!inputQuantized && !outputQuantized) {
                if (returnedParams->hasAttr("FloatOutput") && returnedParams->get<bool>("FloatOutput")) {
                    // This will ensure that the constants are properly dequantized and tensors are set
                    // to fp16 for cases such as u8 with neutral quantParams
                    floatOutput = true;
                }
            } else if (inputQuantized && !outputQuantized) {
                if (returnedParams->hasAttr("Int32Output") && returnedParams->get<bool>("Int32Output")) {
                    floatOutput = false;
                }
                // NOTE: HW limitation, in mixed mode the grids of the MPEs are conflicting between
                // each other, which leads to 1x1 workloads, so we will do an explicit conversion
                // in different cases
                if (returnedParams->hasAttr("FloatOutput") && returnedParams->get<bool>("FloatOutput")) {
                    if (op->getOutputTensor(mv::IO_TENSOR_OUTPUT)->getShape()[mv::IO_WIDTH_DIMENSION] == 1 &&
                        op->getOutputTensor(mv::IO_TENSOR_OUTPUT)->getShape()[mv::IO_HEIGHT_DIMENSION] == 1) {
                        floatOutput = true;
                    } else {
                        if (perTensorScale(op->getInputTensor(mv::IO_TENSOR_OUTPUT))) {
                            floatOutput = true;
                        }
                    }
                }
            } else if (!inputQuantized && outputQuantized) {
                if (returnedParams->hasAttr("FloatOutput") && returnedParams->get<bool>("FloatOutput")) {
                    if (perTensorScale(op->getInputTensor(mv::IO_TENSOR_INPUT))) {
                        floatOutput = true;
                    }
                }
            }
        }
    }

    return floatOutput;
}

/**
 * @brief fuse PPE function to parent op
 */
template <fuse_function fuse>
void fusePPEBaseFcn(mv::Data::OpListIterator& opIt, mv::ComputationModel& model, const std::string& opType, mv::TargetDescriptor& td) {
    mv::OpModel om(model);
    mv::DataModel dm(model);
    auto ppeOutputMemoryLocation = opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT)->get<mv::Tensor::MemoryLocation>("Location");
    auto parentOp = om.getSourceOp(opIt->getInputTensor(mv::IO_TENSOR_INPUT));

    std::vector<mv::Data::OpListIterator> fusableParents;
    std::vector<mv::Data::OpListIterator> quantParamsPathOps;
    auto isActivationAgnostic = [](mv::Data::OpListIterator &op)
        {return op->isImplicit() || op->getOpType() == "Concat" || op->getOpType() == "Identity" || op->getOpType() == "StridedSlice";};

    quantParamsPathOps.push_back(parentOp);
    if (!isActivationAgnostic(parentOp))
        fusableParents.push_back(parentOp);
    else {
        std::queue<mv::Data::OpListIterator> op_itr_bfs;
        op_itr_bfs.push(parentOp);
        // BFS the implicit ops subtree to find all fusable parents
        while (!op_itr_bfs.empty()) {
            auto current_op_itr = op_itr_bfs.front();
            for(auto parentIt = current_op_itr.leftmostParent();
                parentIt != om.opEnd(); ++parentIt) {
                mv::Data::OpListIterator parentOpIt = parentIt;
                quantParamsPathOps.push_back(parentIt);
                if (isActivationAgnostic(parentOpIt)) {
                    op_itr_bfs.push(parentIt);
                } else {
                    fusableParents.push_back(parentIt);
                }
            }
            op_itr_bfs.pop();
        }
    }

    /// Disable last sigmoid fusion when fp16 precision
    if (opType == "Sigmoid") {
        auto nextOps = mv::findSinkLayers(dm, opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT));
        for (auto& nextOp : nextOps) {
            if (nextOp->getOpType() == "Output") {
                for (auto& parent : fusableParents) {
                    if (floatOutput(parent, model)) {
                        return;
                    }
                }
            }
        }
    }

    // Disable Prelu / leakyRelu fusion if slopes are per-channel or negative
    if (opType == "Prelu" && (opIt->getInputTensor(mv::IO_TENSOR_WEIGHTS_SET)->getData().size() != 1 ||
                              opIt->getInputTensor(mv::IO_TENSOR_WEIGHTS_SET)->getDoubleData()[0] < 0)) {
        return;
    }
    if (opType == "LeakyRelu" && opIt->get<double>("alpha") < 0) {
        return;
    }

    // Check number of children of each fusable parent;
    // normally if all children are the same opType,
    // one could attempt to fuse all siblings.
    // Have this as a future optimization, for now just mark it as
    // software executed.
    if ((!parentOp->isHardwarizable() && parentOp.childrenSize() > 1) ||
        (std::find_if(fusableParents.begin(), fusableParents.end(),
            [] (mv::Data::OpListIterator &op) {return op.childrenSize() > 1 || !op->isHardwarizable();})
        != fusableParents.end()))
    {
        /// Insert maxpool to fuse relu
        if(opType == "Relu" && (parentOp->getOpType() != "Input"))
        {
            auto childOpIts = findSinkLayers(dm, opIt->getInputTensor(mv::IO_TENSOR_INPUT));
            auto maxpool = om.maxPool("identity_maxpool_" + opIt->getName(), opIt->getInputTensor(mv::IO_TENSOR_INPUT), {1, 1}, {1, 1}, {0, 0, 0, 0});
            auto maxpoolOp = om.getSourceOp(maxpool);
            maxpoolOp->set<unsigned>("opId", opIt->get<unsigned>("opId"));
            std::vector<std::string> postOpTypes;
            postOpTypes.push_back(opType);
            maxpoolOp->set<std::vector<std::string>>("postOpTypes", postOpTypes);

            maxpool->setDType(opIt->getOutputTensor(0)->getDType());
            maxpool->setQuantParams(opIt->getOutputTensor(0)->getQuantParams());
            opIt = linkNewOperationsFuse(maxpoolOp, maxpool, om, opIt);
            if (ppeOutputMemoryLocation.isForced())
                opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT)->set<mv::Tensor::MemoryLocation>("Location", ppeOutputMemoryLocation);
        }
        else
        {
            opIt->set<bool>("softwareExecuted", true);
        }

        return;
    }

    // Proceed with fusign postOp into each parentOp
    for (auto parentIt : fusableParents) {
        //Call fuse function
        fuse(opIt, parentIt, opType, om, td);
    }

	// Propagate quantParams up path to fuseableParents
    for (auto& it : quantParamsPathOps)
    {
        auto sourceTensor = it->getOutputTensor(mv::IO_TENSOR_OUTPUT);
        sourceTensor->setDType(opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT)->getDType());
        sourceTensor->setQuantParams(opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT)->getQuantParams());
    }

    // Link direct postOp parent with postOp consumers
    auto sourceTensor = parentOp->getOutputTensor(mv::IO_TENSOR_OUTPUT);
    opIt = linkNewOperationsFuse(parentOp, sourceTensor, om, opIt);
    if (ppeOutputMemoryLocation.isForced())
        opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT)->set<mv::Tensor::MemoryLocation>("Location", ppeOutputMemoryLocation);
}

void fuseEltwiseFcn(mv::Data::OpListIterator& opIt1, mv::ComputationModel& model, const std::string& /*opType*/, mv::TargetDescriptor& td) {
    std::unordered_map<std::string, std::function<void(mv::Data::OpListIterator&, mv::ComputationModel&, std::string&, mv::TargetDescriptor& td)>>
            fuseEltwiseMap = {{"Minimum", fuseMinimumFcn}, {"Maximum", fuseMaximumFcn}, {"Power", fusePPEBaseFcn<fuse_usual_ppe>}};

    auto eltwiseType = opIt1->get<std::string>("eltwiseType");
    auto functor = fuseEltwiseMap.find(eltwiseType);
    if (functor != fuseEltwiseMap.end())
        functor->second(opIt1, model, eltwiseType, td);
}

void fuseMinimumFcn(mv::Data::OpListIterator& opIt, mv::ComputationModel& model, const std::string& /*opType*/, mv::TargetDescriptor&) {
    mv::OpModel om(model);

    auto minimumOutputMemoryLocation = opIt->getOutputTensor(0)->get<mv::Tensor::MemoryLocation>("Location");
    auto parentOpIt = om.getSourceOp(opIt->getInputTensor(0));

    double minimumValue = opIt->get<double>("minimum");
    auto sourceTensor = parentOpIt->getOutputTensor(0);
    parentOpIt->set<double>("Minimum", minimumValue);

    sourceTensor->setDType(opIt->getOutputTensor(0)->getDType());
    sourceTensor->setQuantParams(opIt->getOutputTensor(0)->getQuantParams());
    opIt = linkNewOperationsFuse(parentOpIt, sourceTensor, om, opIt);
    if (minimumOutputMemoryLocation.isForced())
        opIt->getOutputTensor(0)->set<mv::Tensor::MemoryLocation>("Location", minimumOutputMemoryLocation);
}

void fuseMaximumFcn(mv::Data::OpListIterator& opIt, mv::ComputationModel& model, const std::string& /*opType*/, mv::TargetDescriptor&) {
    mv::OpModel om(model);

    auto maximumOutputMemoryLocation = opIt->getOutputTensor(0)->get<mv::Tensor::MemoryLocation>("Location");
    auto parentOpIt = om.getSourceOp(opIt->getInputTensor(0));

    double maximumValue = opIt->get<double>("maximum");
    auto sourceTensor = parentOpIt->getOutputTensor(0);
    parentOpIt->set<double>("Maximum", maximumValue);

    sourceTensor->setDType(opIt->getOutputTensor(0)->getDType());
    sourceTensor->setQuantParams(opIt->getOutputTensor(0)->getQuantParams());
    opIt = linkNewOperationsFuse(parentOpIt, sourceTensor, om, opIt);
    if (maximumOutputMemoryLocation.isForced())
        opIt->getOutputTensor(0)->set<mv::Tensor::MemoryLocation>("Location", maximumOutputMemoryLocation);
}

void fuseBatchNormFcn(mv::Data::OpListIterator& opIt, mv::ComputationModel& model, const std::string& /*opType*/, mv::TargetDescriptor&) {
    mv::OpModel om(model);
    auto outputMemoryLocation = opIt->getOutputTensor(0)->get<mv::Tensor::MemoryLocation>("Location");
    auto batchNormName = opIt->getName();
    auto parentOpIt = om.getSourceOp(opIt->getInputTensor(0));
    auto bnMean = *opIt->getInputTensor(1);
    auto bnVar = *opIt->getInputTensor(2);
    auto bnOffset = *opIt->getInputTensor(3);
    auto bnScale = *opIt->getInputTensor(4);
    double bnEps = opIt->get<double>("eps");
    auto scaleParam = mv::math::divide(bnScale, mv::math::sqrt(mv::math::add(bnVar, bnEps)));
    auto offsetParam = mv::math::subtract(bnOffset, mv::math::multiply(bnMean, scaleParam));
    auto offset = om.constantDataElement(batchNormName + "_offset",
                        offsetParam.getData(), offsetParam.getShape(),
                        offsetParam.getDType(), offsetParam.getOrder());

    mv::Data::TensorIterator sourceTensor;

    if (bnMean.getShape().ndims() == 1)
    {
        if (parentOpIt->getOpType() == "Conv")
        {
            parentOpIt->getInputTensor(1)->multiply(scaleParam);
            sourceTensor = parentOpIt->getOutputTensor(0);
        }
        else
        {
            auto scale = om.constantDataElement("", scaleParam.getData(), scaleParam.getShape(), scaleParam.getDType(), scaleParam.getOrder());
            sourceTensor = om.scale("", opIt->getInputTensor(0), scale);
            parentOpIt = om.getSourceOp(sourceTensor);
        }
    }
    else
    {
        auto scale = om.constantDataElement("", scaleParam.getData(), scaleParam.getShape(), scaleParam.getDType(), scaleParam.getOrder());
        sourceTensor = om.eltwise("", {opIt->getInputTensor(0), scale}, "Multiply");
        parentOpIt = om.getSourceOp(sourceTensor);
    }

    if (offsetParam.getShape().ndims() == 1)
        sourceTensor = om.bias("", sourceTensor, offset);
    else
        sourceTensor = om.eltwise("", {sourceTensor, offset}, "Add");
    sourceTensor->setDType(opIt->getOutputTensor(0)->getDType());
    sourceTensor->setQuantParams(opIt->getOutputTensor(0)->getQuantParams());
    opIt = linkNewOperationsFuse(parentOpIt, sourceTensor, om, opIt);
    if (outputMemoryLocation.isForced())
        opIt->getOutputTensor(0)->set<mv::Tensor::MemoryLocation>("Location", outputMemoryLocation);
}

void fuse_usual_ppe(mv::Data::OpListIterator& opIt, mv::Data::OpListIterator& parentIt, const std::string& opType,
                         mv::OpModel& om, mv::TargetDescriptor&) {
    std::vector<std::string> postOpTypes;
    if (parentIt->hasAttr("postOpTypes"))
        postOpTypes = parentIt->get<std::vector<std::string>>("postOpTypes");
    postOpTypes.push_back(opType);
    parentIt->set<std::vector<std::string>>("postOpTypes", postOpTypes);

    if (opType == "LeakyRelu")
    {
        parentIt->set<double>("leakyAlpha", opIt->get<double>("alpha"));
    }
    else if (opType == "Prelu")
    {
        // slope is the second input of PRelu
        auto parentOpIt1 = om.getSourceOp(opIt->getInputTensor(1));
        const std::string& parentOpType = parentOpIt1->getOpType();

        // Constant Operator is only support
        if ((parentOpType == "Constant") || (parentOpType == "ConstantInt") || (parentOpType == "ConstantDataElement"))
        {
            // TODO
            // check how to handle non constant slopes as constant slops are only supported in the current implementation
            throw mv::OpError(parentOpIt1->getLogID(), "Non Const slopes of PReLU are not supported");
        }

        // output densor of the const output tensor
        auto slopeSourceTensor = parentOpIt1->getOutputTensor(mv::IO_TENSOR_OUTPUT);

        std::vector<mv::DataElement> data = slopeSourceTensor->getData();

        std::vector<double> slopes(data.size());
        if (slopeSourceTensor->getDType() != mv::DType("Float16"))
        {
            // slopes are converted into fp16 before fusing passing
            throw mv::OpError(parentOpIt1->getLogID(), "float16 data type is expected");
        }

        // converting slopes into double to use them for quantization param calculation later
        std::transform(data.begin(), data.end(), slopes.begin(),
            [](const int64_t& arg){ return static_cast<double>(mv::fp16_to_fp32(static_cast<uint16_t>(arg))); });

        // for quantization
        parentIt->set<std::vector<double>>( "slopes", slopes );

        // Note
        // Constant is removed in linkNewOperationsFuse

    }
}

void fuse_custom_pwl(mv::Data::OpListIterator& opIt, mv::Data::OpListIterator& parentIt, const std::string& /*opType*/,
                         mv::OpModel& /*om*/, mv::TargetDescriptor& td) {
    std::vector<std::string> postOpTypes;
        if (parentIt->hasAttr("postOpTypes"))
            postOpTypes = parentIt->get<std::vector<std::string>>("postOpTypes");
        postOpTypes.push_back(std::string("FLEXARB"));
        parentIt->set<std::vector<std::string>>("postOpTypes", postOpTypes);

        /* Set custom PWL attributes */
        /* Check if custom PWL table is specified in target descriptor */

        mv::PWLTableType pwl_type;
        pwl_type.activation = opIt->getOpType();
        pwl_type.dtype = opIt->getInputTensor(0)->getDType();
        std::string table_source = "UNSET";

        if (opIt->getOpType() == "LeakyRelu" && opIt->hasAttr("alpha"))
            parentIt->set<double>("leakyAlpha", opIt->get<double>("alpha"));

        TableSource ts = static_cast<TableSource>(opIt->get<int>("PWLSource"));

        if (ts == TableSource::Generation) {
            table_source = "Generation";
        } else if (ts == TableSource::TargetDescriptor) {
            table_source = "TargetDescriptor";

            /* Search for most optimal table using QuantParams */
            auto table_vec = td.generalTargetConfigs().pwlTables.at(pwl_type);

            double min_diff = std::numeric_limits<double>::max();
            std::vector<std::size_t> min_index;

            auto quant_params = opIt->getOutputTensor()[0]->getQuantParams();
            double f_min = quant_params.getMin()[0];
            double f_max = quant_params.getMax()[0];

            /* Find tables that have least elements that are outside of float range */
            /* Tables that have fewer elements outside of the defined interval are favored, to prevent clamping */
            for (std::size_t i = 0; i < table_vec.size(); ++i) {
                double diff = 0;
                auto float_range = table_vec[i].float_range;

                if (f_min < float_range.first) {
                    diff += float_range.first - f_min;
                }
                if (f_max > float_range.second) {
                    diff += f_max - float_range.second;
                }

                if (diff < min_diff) {
                    min_diff = diff;
                    min_index.clear();
                    min_index.push_back(i);
                } else if (std::fabs(diff - min_diff) < std::numeric_limits<double>::epsilon()) {
                    min_index.push_back(i);
                }
            }

            int pwl_table_index = 0;

            /* In case there are tables that contain the whole FQ range */
            if (min_diff == 0) {
                min_diff = std::numeric_limits<double>::max();

                /* Select the most restrictive table in order to get the best precision */
                for (std::size_t i = 0; i < min_index.size(); ++i) {
                    auto float_range = table_vec[min_index[i]].float_range;

                    double diff = (f_min - float_range.first) + (float_range.second - f_max);

                    if (diff < min_diff) {
                        min_diff = diff;
                        pwl_table_index = min_index[i];
                    }
                }
            }

            parentIt->set<int>("PWLIndex", pwl_table_index);
        } else {
            /* Never enter */
            throw mv::AttributeError(parentIt->getLogID(), " has invalid custom PWL table source.");
        }

        parentIt->set<std::string>("PWLSource", table_source);
        parentIt->set<mv::PWLTableType>("PWLType", pwl_type);

}
