//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/pass/pass_registry.hpp"
#include "include/mcm/computation/model/control_model.hpp"
#include "include/mcm/computation/model/data_model.hpp"
#include "include/mcm/op_model.hpp"
#include "include/mcm/utils/data_generator.hpp"
#include "include/mcm/tensor/quantization_params.hpp"
#include "include/mcm/utils/custom_strings.hpp"
#include "include/mcm/utils/custom_math.hpp"
#include "include/mcm/tensor/shape.hpp"
#include "include/mcm/pass/pass_utils.hpp"
#include "include/mcm/target/kmb/ppe_task.hpp"
#include "include/mcm/target/kmb/pwl/pwl_dyn_fit.hpp"
#include "include/mcm/target/kmb/pwl/ref_functions.hpp"
#include <cmath>
#include <math.h>

static const std::size_t WT_ELEMENTS_PER_CHANNEL = 4;
static const std::size_t ALU_HALT_OPCODE = 6;
static const std::size_t ALU_LOAD = 2;
static const std::size_t MAX_CLUSTERS = 4;
static void generateWeightsTablesFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);
static void generateAndPopulateInstructionListTablesFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);
static void populateWeightsTablesPointersFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);
static void populateWeightsTablesQuantizationFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);
static void removeBiasTensorsFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);
static void populateStorageElementPointersFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);

// 8bit mult mask
static const uint32_t PRELU_MULT_MASK = 0x000000FF;
// 6bit shift mask
static const uint32_t PRELU_SHIFT_MASK = 0x00003F00;
static const uint32_t PRELU_SHIFT_SHIFT = 8;
// round mode mask
static const uint32_t ROUND_MODE_MASK = 0x0000C000;
static const uint32_t ROUND_MODE_SHIFT = 14;
// scale mask
static const uint32_t SCALE_MODE_MASK = 0xFFFF0000;
static const uint32_t SCALE_MODE_SHIFT = 16;

namespace mv
{
    namespace pass
    {
        MV_REGISTER_PASS(GenerateWeightsTables)
        .setFunc(generateWeightsTablesFcn)
        .setDescription(
            "Generates weights tables for the Tasks that need them"
        );
        MV_REGISTER_PASS(GenerateAndPopulateInstructionListTables)
        .setFunc(generateAndPopulateInstructionListTablesFcn)
        .setDescription(
			"Generates and populates instruction list tables for the custom PWL that need them"
		);
        MV_REGISTER_PASS(PopulateWeightsTablesPointers)
        .setFunc(populateWeightsTablesPointersFcn)
        .setDescription(
            "Populate WeightsTables"
        );
        MV_REGISTER_PASS(PopulateWeightsTablesQuantization)
        .setFunc(populateWeightsTablesQuantizationFcn)
        .setDescription(
            "Populate WeightsTables"
        );
        MV_REGISTER_PASS(RemoveBiasTensors)
        .setFunc(removeBiasTensorsFcn)
        .setDescription(
            "remove bias tensors after been adding to all weight tables"
        );
        MV_REGISTER_PASS(PopulateStorageElementPointers)
        .setFunc(populateStorageElementPointersFcn)
        .setDescription(
            "Populate storage element maps for activations"
        );
    }
}

void populatePointerMultiCluster(mv::Data::TensorIterator weightsTableData, mv::Data::OpListIterator dpuTaskOp, const std::vector<int64_t>& increments, long int offset, std::size_t addingIndex, mv::ComputationModel &model, mv::Data::TensorIterator tensor)
{
    //std::cout << "Populating " << std::to_string(addingIndex) << " pointer of weights table for op " << dpuTaskOp->getName() << std::endl;
    if (dpuTaskOp->get<std::string>("splitStrategy") != "SplitOverK")
    {
        for (size_t i = 0, k = 0; i < weightsTableData->size(); i+=WT_ELEMENTS_PER_CHANNEL, ++k)
            weightsTableData->at(i + addingIndex) = offset + increments[k];
    }
    else
    {
        // Saving a backup copy of the offset
        long int new_offset = offset;

        unsigned numClusters =  model.getGlobalConfigParams()->get<int>("Number_of_Clusters");
        std::size_t sizeToIterate = 0;
        std::size_t totalSizeToIterate = 0;
        std::size_t k = 0;
        std::vector<std::size_t> fusedClusterOffsets;
        if (tensor->hasAttr("fusedClusterOffsets"))
            fusedClusterOffsets = tensor->get<std::vector<std::size_t>>("fusedClusterOffsets");
        bool isSparse = tensor->isSparse();
        for (unsigned i = 0; i < numClusters; i++)
        {
            // Resetting offset at the beginning of the cluster
            offset = new_offset;
            if (fusedClusterOffsets.size() > i)
                offset += fusedClusterOffsets.at(i);
            // Resetting k index only when weights are not sparse
            if(!isSparse)
                k = 0;

            // Filling cluster and subtensors
            for (size_t j = 0; j < weightsTableData->getSubTensor(i).size(); j+=WT_ELEMENTS_PER_CHANNEL)
            {
                weightsTableData->getSubTensor(i).at(j + addingIndex) = offset + increments[k];
                weightsTableData->at(j + addingIndex + totalSizeToIterate) = offset + increments[k++];
            }
            // Preparing for next iteration
            sizeToIterate = tensor->getSubTensor(i).getShape()[mv::KERNEL_OUTPUT_CHANNELS] * WT_ELEMENTS_PER_CHANNEL;
            totalSizeToIterate += sizeToIterate;
        }
    }
    return;
}

void populateWeightsTablesDataPointers(mv::Data::TensorIterator weightsTableData, mv::Data::OpListIterator dpuTaskOp, mv::ComputationModel& model)
{
    auto taskOp = dpuTaskOp->get<std::string>("taskOp");

    // Max pooling does not need DataPointer
    // Eltwise doesn't have weights table at all
    if(taskOp == "Conv" || taskOp == "ChannelMajorConvolution" ||
       taskOp == "DepthwiseConv")
    {
        auto weights = dpuTaskOp->getInputTensor(1);

        // NOTE: Output channels taken from the weights, hence already padded
        auto outputChannels = weights->getShape()[mv::KERNEL_OUTPUT_CHANNELS];
        auto strategy = dpuTaskOp->get<std::string>("splitStrategy");

        long int offset = weights->getAddress();
        std::vector<int64_t> increments;

        if (dpuTaskOp->hasAttr("fusedConstantIndex") && weights->hasAttr("fusedOffset"))
        {
            auto fusedConstantIndex = dpuTaskOp->get<size_t>("fusedConstantIndex");
            auto fusedConstant = dpuTaskOp->getInputTensor(fusedConstantIndex);
            offset = fusedConstant->getAddress();
            if(strategy != "SplitOverK")
                offset += weights->get<std::size_t>("fusedOffset");
        }

        if(weights->isSparse())
        {
            if(strategy != "SplitOverK")
                increments = weights->getKernelDataOffsets();
            else
            {
                unsigned numClusters =  model.getGlobalConfigParams()->get<int>("Number_of_Clusters");
                for(std::size_t i = 0; i < numClusters; ++i)
                {
                    auto kernelOffsets = weights->getSubTensor(i).getKernelDataOffsets();
                    increments.insert(increments.end(), kernelOffsets.begin(), kernelOffsets.end());
                }
            }
        }
        else
        {
            long int increment = weights->getShape()[mv::KERNEL_WEIGHT_SETS] * (weights->getDType().getSizeInBits() / 8);
            increments = std::vector<int64_t>(outputChannels, 0);
            for(unsigned i = 1; i < outputChannels; ++i)
                increments[i] = increments[i-1] + increment;
        }
        populatePointerMultiCluster(weightsTableData, dpuTaskOp, increments, offset, 0, model, weights);
    }
}



void populateWeightsTablesSparsityPointers(mv::Data::TensorIterator weightsTableData, mv::Data::OpListIterator dpuTaskOp, mv::ComputationModel& model, mv::TargetDescriptor& td)
{
    mv::DataModel dm(model);

    auto taskOp = dpuTaskOp->get<std::string>("taskOp");
    auto strategy = dpuTaskOp->get<std::string>("splitStrategy");
    if(taskOp == "Conv")
    {
        auto weights = dpuTaskOp->getInputTensor(1);

        // NOTE: Output channels taken from the weights, hence already padded
        auto outputChannels = weights->getShape()[mv::KERNEL_OUTPUT_CHANNELS];
        if(weights->isSparse())
        {
            auto weightsSparsityMap = dm.getTensor(weights->getSparsityMap()->getName());
            long int offset = weightsSparsityMap->getAddress();
            if (strategy == "SplitOverK" && dpuTaskOp->hasAttr("fusedConstantIndex"))
            {
                auto fusedConstantIndex = dpuTaskOp->get<size_t>("fusedConstantIndex");
                auto fusedConstant = dpuTaskOp->getInputTensor(fusedConstantIndex);
                offset = fusedConstant->getAddress();
            }
            auto sparsityMapSizeInWords = weightsSparsityMap->getShape().totalSize();
            auto sparsityMapSizeInBytes = sparsityMapSizeInWords * weightsSparsityMap->getDType().getSizeInBits() / 8;
            long int increment = sparsityMapSizeInBytes / outputChannels;

            std::vector<int64_t> increments = std::vector<int64_t>(outputChannels, 0);
            for(unsigned i = 1; i < outputChannels; ++i)
                increments[i] = increments[i-1] + increment;

            populatePointerMultiCluster(weightsTableData, dpuTaskOp, increments, offset, 1, model, weightsSparsityMap);
        }
        else
        {
            // Dense ZMajor Convolution case
            // Not using the generic function because it's a super simple case
            int64_t offset = 0xFFFFFF; // NOTE: Implementation defined
            for (size_t i = 0; i < weightsTableData->size(); i+=WT_ELEMENTS_PER_CHANNEL)
                weightsTableData->at(i+1) = offset;
            
            if (strategy == "SplitOverK")
            {
                // also fill sub tensors
                for (unsigned i = 0; i < weightsTableData->numSubTensors(); i++)
                    for (size_t j = 0; j < weightsTableData->getSubTensor(i).size(); j+=WT_ELEMENTS_PER_CHANNEL)
                        weightsTableData->getSubTensor(i).at(j+1) = offset;
            }
        }
    }
    else if(taskOp == "DepthwiseConv"  ||
            (taskOp == "ChannelMajorConvolution" && td.getTarget() != mv::Target::ma3720) ||
            taskOp == "MaxPool")
    {
        // We have fake sparsity here! Yuppi!
        auto activationWindow = dpuTaskOp->getInputTensor(dpuTaskOp->get<std::size_t>("fakeSparsityIndex"));
        auto activationWindowSizeInWords = activationWindow->getShape().totalSize();
        auto activationWindowSizeInBytes = activationWindowSizeInWords * activationWindow->getDType().getSizeInBits() / 8;

        auto outputChannels = dpuTaskOp->getOutputTensor(0)->getShape()[mv::IO_CHANNEL_DIMENSION];

        auto paddedOutputChannels = outputChannels;
        auto globalParams = model.getGlobalConfigParams();
        int pad = globalParams->hasAttr("VPUX30XXChannelPadding") ? globalParams->get<int>("VPUX30XXChannelPadding") : 16;

        paddedOutputChannels = mv::round_up(outputChannels, pad);
        auto increment = activationWindowSizeInBytes / paddedOutputChannels;

        long int offset = activationWindow->getAddress();
        if (strategy == "SplitOverK" && dpuTaskOp->hasAttr("fusedConstantIndex"))
        {
            auto fusedConstantIndex = dpuTaskOp->get<size_t>("fusedConstantIndex");
            auto fusedConstant = dpuTaskOp->getInputTensor(fusedConstantIndex);
            offset = fusedConstant->getAddress();
        }

        std::vector<int64_t> increments = std::vector<int64_t>(paddedOutputChannels, 0);
        for(unsigned i = 1; i < paddedOutputChannels; ++i)
            increments[i] = increments[i-1] + increment;

        populatePointerMultiCluster(weightsTableData, dpuTaskOp, increments, offset, 1, model, activationWindow);
    }
}
#include "mcm/utils/custom_math.hpp"


void populateWeightsTablesActivationAndBias(mv::Data::TensorIterator weightsTableData, mv::Data::OpListIterator dpuTaskOp, mv::ComputationModel& model,
        mv::TargetDescriptor& td)
{
    mv::DataModel dm(model);
    mv::QuantizationParams quantParams = {{},{},{},{}};
    auto output = dpuTaskOp->getOutputTensor(0);
    auto outputChannels = output->getShape()[mv::IO_CHANNEL_DIMENSION];
    auto paddedOutputChannels = outputChannels;
    auto globalParams = model.getGlobalConfigParams();
    int pad = globalParams->hasAttr("VPUX30XXChannelPadding") ? globalParams->get<int>("VPUX30XXChannelPadding") : 16;

    paddedOutputChannels = mv::round_up(outputChannels, pad);

    std::vector<int32_t> mScaled(paddedOutputChannels, 0);
    std::vector<int32_t> mShift(paddedOutputChannels, 0);
    if(output->hasAttr("quantParams"))
    {
        if (dpuTaskOp->hasAttr("pwlQuantParams"))
            quantParams = dpuTaskOp->get<mv::QuantizationParams>("pwlQuantParams");
        else
            quantParams = dpuTaskOp->getOutputTensor(0)->get<mv::QuantizationParams>("quantParams");
        if (!quantParams.isEmpty())
        {
            auto mult = quantParams.getMult();
            auto shift = quantParams.getShift();
            if (output->hasAttr("preAdjustedShift") && output->hasAttr("preAdjustedMult"))
            {
                shift = output->get<std::vector<unsigned>>("preAdjustedShift");
                mult = output->get<std::vector<unsigned>>("preAdjustedMult");
            }
            std::transform(mScaled.begin(), mScaled.end(), mult.begin(), mScaled.begin(), std::plus<int32_t>());
            std::transform(mShift.begin(), mShift.end(), shift.begin(), mShift.begin(), std::plus<int32_t>());
            for (size_t idx = outputChannels; idx < paddedOutputChannels; idx++)
            {
                mScaled[idx] = mScaled[0];
                mShift[idx] = mShift[0];
            }
        }
    }
    std::vector<mv::DataElement> biasData;
    bool hasBias = dpuTaskOp->hasAttr("bias");
    bool hasPPETask = dpuTaskOp->hasAttr("PPETask");

    mv::Data::TensorIterator bias;
    if (hasBias)
    {
        bias = dm.getTensor(dpuTaskOp->get<std::string>("bias"));
        biasData = bias->getData(); //Bias has the type Int32 in both cases above
    }

    bool floatScaleTable = false;
    if (dpuTaskOp->hasAttr("floatScale"))
        floatScaleTable = true;

    if (floatScaleTable)
    {
        auto mScale = dpuTaskOp->get<std::vector<float>>("floatScale");
        for (size_t i = 0; i < weightsTableData->size(); i+=WT_ELEMENTS_PER_CHANNEL)
        {
            weightsTableData->at(i+2) = static_cast<int64_t>(mv::float_as_int(mScale[i/WT_ELEMENTS_PER_CHANNEL]));
            if (hasBias)
                weightsTableData->at(i+3) = static_cast<int64_t>(mv::float_as_int(biasData[i/WT_ELEMENTS_PER_CHANNEL]));
        }
    }
    else
    {
        unsigned round_mode = 1;
        std::vector<int32_t> round32(outputChannels, round_mode);
        std::vector<int32_t> reluMultData(outputChannels, 0);
        if (hasPPETask && td.getTarget() != mv::Target::ma3720)
        {
            auto ppeFF = dpuTaskOp->get<mv::PPETask>("PPETask").getFixedFunction();
            auto& ppeLayers = ppeFF.getLayers();
            auto isLPRelu = std::find(ppeLayers.begin(), ppeLayers.end(), mv::PPELayerTypeEnum::PPELayerType_LPRELU) != ppeLayers.end();
            if (isLPRelu)
            {
                // TODO
                // check why no zero size when multipliers is defined as const std::vector<int32_t>&
                std::vector<int32_t> multipliers = dpuTaskOp->get<mv::PPETask>( "PPETask" ).getFixedFunction().getLReluMults();
                if (multipliers.size() == 1)
                {
                    std::fill(reluMultData.begin(), reluMultData.end(), multipliers[0]);
                }
                else if (multipliers.size() == outputChannels)
                {
                    reluMultData = multipliers;
                }
                else
                {
                    throw std::runtime_error("The number of slopes does not match with the number of output channels");
                }
            }
        }

        for (size_t i = 0; i < weightsTableData->size(); i+=WT_ELEMENTS_PER_CHANNEL)
        {
            weightsTableData->at(i+2) = static_cast<int64_t>(
                ((mScaled[i/WT_ELEMENTS_PER_CHANNEL] << SCALE_MODE_SHIFT) & SCALE_MODE_MASK) |
                ((round32[i/WT_ELEMENTS_PER_CHANNEL] << ROUND_MODE_SHIFT) & ROUND_MODE_MASK) |
                ((mShift[i/WT_ELEMENTS_PER_CHANNEL] << PRELU_SHIFT_SHIFT) & PRELU_SHIFT_MASK) |
                (reluMultData[i/WT_ELEMENTS_PER_CHANNEL] & PRELU_MULT_MASK));
            if (hasBias)
                weightsTableData->at(i+3) = biasData[i/WT_ELEMENTS_PER_CHANNEL];
        }
    }
}
static void populateWeightsTablesQuantizationFcn(const mv::pass::PassEntry& , mv::ComputationModel& model, mv::TargetDescriptor& td, mv::Element&, mv::Element&)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_PASS)
    mv::OpModel om(model);
    for(auto dpuTaskOp = om.opBegin(); dpuTaskOp != om.opEnd(); ++dpuTaskOp)
    {
        if(dpuTaskOp->getOpType() == "DPUTask")
        {
            auto taskOp = dpuTaskOp->get<std::string>("taskOp");
            if(taskOp == "Conv" ||
               taskOp == "ChannelMajorConvolution" ||
               taskOp == "MaxPool" ||
               taskOp == "DepthwiseConv")
            {
                auto weightsTable = dpuTaskOp->getInputTensor(dpuTaskOp->get<std::size_t>("weightsTableIndex"));
                populateWeightsTablesActivationAndBias(weightsTable, dpuTaskOp, model, td);
            }
        }
    }
}
static void removeBiasTensorsFcn(const mv::pass::PassEntry& , mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_PASS)
    mv::OpModel om(model);
    mv::DataModel dm(model);
    std::set<std::string> biasNamesToRemove;
    for(auto dpuTaskOp = om.opBegin(); dpuTaskOp != om.opEnd(); ++dpuTaskOp)
    {
        if(dpuTaskOp->getOpType() == "DPUTask")
        {
            auto taskOp = dpuTaskOp->get<std::string>("taskOp");
            if(taskOp == "Conv" ||
               taskOp == "ChannelMajorConvolution" ||
               taskOp == "MaxPool" ||
               taskOp == "DepthwiseConv")
            {
                bool hasBias = dpuTaskOp->hasAttr("bias");
                if (hasBias)
                {
                    auto bias = dm.getTensor(dpuTaskOp->get<std::string>("bias"));
                    biasNamesToRemove.insert(bias->getName());
                    dpuTaskOp->erase("bias");
                }
            }
        }
    }
    for(auto biasName = biasNamesToRemove.begin(); biasName != biasNamesToRemove.end(); ++biasName)
    {
        auto bias = dm.getTensor(*biasName);
        dm.undefineTensor(bias);
    }
}

static void populateWeightsTablesPointersFcn(const mv::pass::PassEntry& , mv::ComputationModel& model, mv::TargetDescriptor& td, mv::Element&, mv::Element&)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_PASS)
    mv::OpModel om(model);
    for(auto dpuTaskOp = om.opBegin(); dpuTaskOp != om.opEnd(); ++dpuTaskOp)
    {
        if(dpuTaskOp->getOpType() == "DPUTask")
        {
            auto taskOp = dpuTaskOp->get<std::string>("taskOp");
            if(taskOp == "Conv" ||
               taskOp == "ChannelMajorConvolution" ||
               taskOp == "MaxPool" ||
               taskOp == "DepthwiseConv")
            {
                auto weightsTable = dpuTaskOp->getInputTensor(dpuTaskOp->get<std::size_t>("weightsTableIndex"));
                populateWeightsTablesDataPointers(weightsTable, dpuTaskOp, model);
                populateWeightsTablesSparsityPointers(weightsTable, dpuTaskOp, model, td);
            }
        }
    }
}

void populateActivationStorageElementMap(
    mv::Data::OpListIterator op,
    mv::ComputationModel& model)
{
    auto inputTensorIdx = 0;

    for (auto tidx : op->get<std::vector<std::size_t>>("storageElementIndex"))
    {
        auto storageElementTable = op->getInputTensor(tidx);
        std::vector<int64_t> table_offsets(storageElementTable->getShape().totalSize(), 0);

        std::vector<unsigned short> basePtrs(MAX_CLUSTERS, 0);
        if (op->getInputTensor(inputTensorIdx)->hasAttr("base_ptrs"))
            basePtrs = op->getInputTensor(inputTensorIdx)->get<std::vector<unsigned short>>("base_ptrs");

        if (std::find(activationSegmentableStrategies.cbegin(), activationSegmentableStrategies.cend(),
            op->get<std::string>("splitStrategy")) == activationSegmentableStrategies.cend()) {
                const auto increment =
                     op->getInputTensor(inputTensorIdx)->getShape()[mv::IO_CHANNEL_DIMENSION] *
                     op->getInputTensor(inputTensorIdx)->getDType().getSizeInBytes();
                for (size_t i = 0; i < table_offsets.size(); ++i)
                    table_offsets[i] =
                        ((i * increment) << SHIFT_FOR_STORAGE_ELEMENT) + basePtrs[0];
        }
        else
        {
            const auto numClusters = model.getGlobalConfigParams()->get<int>("Number_of_Clusters");
            auto running_index = 0;

            for (int cl = 0; cl < numClusters; cl++) {
                const auto increment =
                     op->getInputTensor(inputTensorIdx)->getSubTensor(cl).getShape()[mv::IO_CHANNEL_DIMENSION] *
                     op->getInputTensor(inputTensorIdx)->getSubTensor(cl).getDType().getSizeInBytes();
                const auto clTotalSize =
                    storageElementTable->getSubTensor(cl).getShape().totalSize();
                for (size_t i = 0; i < clTotalSize; ++i)
                    table_offsets[running_index + i] =
                        ((i * increment) << SHIFT_FOR_STORAGE_ELEMENT) + basePtrs[cl];
                running_index += clTotalSize;
            }
        }
        storageElementTable->populate(table_offsets, mv::Order("NHWC"));
        inputTensorIdx++;
    }
}

// Sub function to generate storage element pointer for dilated convolution

void populateActivationStorageElementMapForDilatedConvolution(mv::Data::OpListIterator dpuTaskOp, mv::ComputationModel&)
{
    auto input = dpuTaskOp->getInputTensor(0);
    auto subConvIndex = dpuTaskOp->get<unsigned>("subConvIndex");
    if ((dpuTaskOp->get<std::vector<std::size_t>>("storageElementIndex")).empty())
        throw mv::RuntimeError("populateActivationStorageElementMapForDilatedConvolution" , dpuTaskOp->getName() + " has no storageElementIndex attr to populate");

    auto activationStorageElement = dpuTaskOp->getInputTensor(dpuTaskOp->
                                            get<std::vector<std::size_t>>("storageElementIndex")[0]);
    auto dilationFactor = dpuTaskOp->get<unsigned>("originalDilationFactor");
    auto originalWidth = dpuTaskOp->get<mv::Shape>("originalShape")[mv::IO_WIDTH_DIMENSION];
    auto inputChannels = input->getShape()[mv::IO_CHANNEL_DIMENSION];
    auto width = activationStorageElement->getShape()[mv::IO_WIDTH_DIMENSION];
    auto height = activationStorageElement->getShape()[mv::IO_HEIGHT_DIMENSION];

    std::vector<int64_t> unpopulated_offsets(width*height, 0);
    unsigned subConvRowIdx = subConvIndex/dilationFactor;
    unsigned subConvColIdx = subConvIndex%dilationFactor;
    long int increment = inputChannels * (input->getDType().getSizeInBits() / 8) ;

    long int subConvElementIncrement = increment * dilationFactor;
    long int subConvRowIncrement = increment * originalWidth * dilationFactor;
    long int subConvOffset = increment * subConvColIdx + subConvRowIdx*originalWidth*increment;

    std::vector<unsigned short> basePtrs(MAX_CLUSTERS, 0);
    if (input->hasAttr("base_ptrs"))
        basePtrs = input->get<std::vector<unsigned short>>("base_ptrs");

    unsigned i = 0;
    unsigned rowOffset = subConvOffset;
    for(unsigned h = 0; h < height; ++h)
    {
        for(unsigned w = 0; w < width; ++w)
        {
            unpopulated_offsets[i++] = ((rowOffset + w * subConvElementIncrement ) << SHIFT_FOR_STORAGE_ELEMENT) + basePtrs[0];
        }
        rowOffset += subConvRowIncrement;
    }
    activationStorageElement->populate(unpopulated_offsets, mv::Order("NHWC"));
}

// Sub function to generate storage element pointer for InterpNN

void populateActivationStorageElementMapForInterpNN(mv::Data::OpListIterator dpuTaskOp, mv::ComputationModel&)
{
    auto input = dpuTaskOp->getInputTensor(0);
    auto activationStorageElement = dpuTaskOp->getInputTensor(dpuTaskOp->
                                            get<std::vector<std::size_t>>("storageElementIndex")[0]);

    auto width = activationStorageElement->getShape()[mv::IO_WIDTH_DIMENSION];
    auto height = activationStorageElement->getShape()[mv::IO_HEIGHT_DIMENSION];
    std::vector<int64_t> unpopulated_offsets(width*height, 0);

    auto inputChannels = input->getShape()[mv::IO_CHANNEL_DIMENSION];
    long int channelIncrement = inputChannels * (input->getDType().getSizeInBits() / 8) ;

    auto originalShape = activationStorageElement->get<mv::Shape>("originalShape");
    auto old_width = originalShape[mv::IO_WIDTH_DIMENSION];
    auto old_height = originalShape[mv::IO_HEIGHT_DIMENSION];
    auto new_width = activationStorageElement->getShape()[mv::IO_WIDTH_DIMENSION];
    auto new_height = activationStorageElement->getShape()[mv::IO_HEIGHT_DIMENSION];
    auto scale_factor_width = int(new_width/old_width);
    auto scale_factor_height = int(new_height/old_height);
    std::size_t padding_col = 0;
    std::size_t padding_row = 0;
    bool padding_width = false;
    bool padding_height = false;
    if (new_width%old_width != 0)
    {
        padding_width = true;
        padding_col = (new_width%old_width);
    }
    if (new_height%old_height != 0)
    {
        padding_height = true;
        padding_row = (new_height%old_height);
    }

    // Populate SEP table
    for (auto row=0; row < int(new_height / scale_factor_height); ++row){
        for (auto col=0; col < int(new_width / scale_factor_width); ++col){
            auto old_offset = (row*old_width + col) * channelIncrement;
            for (auto new_row=0; new_row < scale_factor_height; ++new_row){
                for (auto new_col=0; new_col < scale_factor_width; ++new_col){
                    auto new_offset = row*new_width*scale_factor_height + new_row*new_width + col*scale_factor_width + new_col;
                    unpopulated_offsets[new_offset] = (old_offset << SHIFT_FOR_STORAGE_ELEMENT);
                }
            }
        }
    }

    if (padding_width)
    {
        auto rounded_width = scale_factor_width * old_width;
        for (auto row=0; row < int (new_height / scale_factor_height); ++row){
            for (auto new_row=0; new_row < scale_factor_height; ++new_row){
                for (std::size_t col=0; col < padding_col; ++col){
                    auto new_offset = col + rounded_width + new_width * new_row + new_width * scale_factor_height * row;
                    auto old_offset = new_offset - 1;
                    unpopulated_offsets[new_offset] = unpopulated_offsets[old_offset];
                }
            }
        }
    }

    if (padding_height)
    {
        for (std::size_t row=0; row < padding_row; ++row){
            for (std::size_t col=0; col < new_width; ++col){
                auto new_offset = col + ( (row + new_height - padding_row) * new_width);
                auto old_offset = (new_offset - new_width);
                unpopulated_offsets[new_offset] = unpopulated_offsets[old_offset];
            }
        }
    }
    activationStorageElement->populate(unpopulated_offsets, mv::Order("NHWC"));
    if (activationStorageElement->hasAttr("splitStrategy") &&
        (activationStorageElement->get<std::string>("splitStrategy") == "SplitOverH"))
    {
        activationStorageElement->matchSubTensors();
    }
}

int64_t getSmallestInputAddress(mv::Data::OpListIterator implicitJoin)
{
    auto numberInputs = implicitJoin.inputsSize();
    auto minBaseAddress = implicitJoin->getInputTensor(0)->getAddress();
    for (size_t i=1; i < numberInputs; i++)
    {
        auto address = implicitJoin->getInputTensor(i)->getAddress();
        if (address < minBaseAddress)
            minBaseAddress = address;
    }

    //std::cout << " minBaseAddress " << std::hex << minBaseAddress << std::endl;
    return minBaseAddress;
}

void populateActivationStorageElementMapForLayerAfterDilatedConvolution(mv::Data::OpListIterator dpuTaskOp, mv::ComputationModel& model)
{
    mv::OpModel om(model);

    auto input = dpuTaskOp->getInputTensor()[0];
    auto parentImplicitOp = om.getSourceOp(input);
    while (parentImplicitOp->getOpType() != "ImplicitJoin")
    {
        parentImplicitOp = om.getSourceOp(parentImplicitOp->getInputTensor()[0]);
    }
    std::size_t numberSubConvs = 0;
    int64_t inputBaseAddress = 0;
    auto activationStorageElement = dpuTaskOp->getInputTensor(dpuTaskOp->get<std::vector<std::size_t>>("storageElementIndex")[0]);
    auto width = activationStorageElement->getShape()[mv::IO_WIDTH_DIMENSION];
    auto height = activationStorageElement->getShape()[mv::IO_HEIGHT_DIMENSION];
    std::vector<int64_t> unpopulated_offsets(width*height, 0);
    auto inputChannels = input->getShape()[mv::IO_CHANNEL_DIMENSION];
    long int increment = inputChannels * (input->getDType().getSizeInBits() / 8) ;

    //NOTE: The code referring to the previous operation if concat
    //is redundant as the final implementation was not to use an adittional operation
    //but resolve the unshuffling of the tensor through the 3D-DMAs, so I am leaving to comments


//    if (parentImplicitOp->getOpType() == "ImplicitJoin")
//    {
    numberSubConvs = parentImplicitOp.inputsSize();
    inputBaseAddress = getSmallestInputAddress(parentImplicitOp);
    //Original DF factor is sqrt() of inputs to ImplicitJoin
    unsigned int originalDilationFactor = std::sqrt(numberSubConvs);

    //for simplicity we pick base address as the smallest of all subconvs output addresses (to avoid negatives)
    unsigned i = 0;
    for(unsigned h = 0; h < height; ++h)
    {
        unsigned subConvRowIdx = (h%originalDilationFactor)*originalDilationFactor;
        for(unsigned w = 0; w < width; ++w)
        {
            //get base address based on subConvIdx
            unsigned subConvIdx = subConvRowIdx + w%originalDilationFactor;
            auto subConvBaseAddressOffset = parentImplicitOp->getInputTensor(subConvIdx)->getAddress() - inputBaseAddress;
            auto subConvWidth = parentImplicitOp->getInputTensor(subConvIdx)->getShape()[mv::IO_WIDTH_DIMENSION];
            //calc offset from start of subconv
            unsigned subConvElementIdx = (h/originalDilationFactor)*subConvWidth + (w/originalDilationFactor);
            unsigned subConvElementOffset = subConvElementIdx * increment;

            unpopulated_offsets[i++] = ((subConvBaseAddressOffset + subConvElementOffset) << SHIFT_FOR_STORAGE_ELEMENT);
            //std::cout << " row " << h << " col " << w << " address "  <<  std::hex << unpopulated_offsets[i-1] << " not shifted " << (subConvBaseAddressOffset + subConvElementOffset) << std::endl;
        }
    }
//    }
//    else if (parentImplicitOp->getOpType() == "DMATask" &&
//             om.getSourceOp(parentImplicitOp->getInputTensor()[0])->getOpType() == "ImplicitConcat" &&
//             om.getSourceOp(parentImplicitOp->getInputTensor()[0])->get<bool>("joinSimulation"))
//    {
//        numberSubConvs = om.getSourceOp(parentImplicitOp->getInputTensor()[0])->get<size_t>("dilationSubConvs");
//        unsigned int originalDilationFactor = std::sqrt(numberSubConvs);
//        unsigned i = 0;
//        unsigned subConvHeight = ceil((double)height / originalDilationFactor); //height of bigger subconvs
//        unsigned subConvWidth = ceil((double)width / originalDilationFactor); //width of bigger subconvs
//        for(unsigned h = 0; h < height; ++h)
//        {
//            for(unsigned w = 0; w < width; ++w)
//            {
//                unsigned totalNumberOfRows=0;
//                unsigned totalNumberOfCols=0;

//                //calc number of rows
//                if((height % originalDilationFactor) == 0 || (h % originalDilationFactor)  < (height % originalDilationFactor))  // all the sub conv to the left are of full width
//                {
//                    totalNumberOfRows = h%originalDilationFactor * subConvHeight;
//                }
//                else
//                {
//                    //add height of subconvRows of full height first and then add remaining of smaller height
//                    totalNumberOfRows = (height % originalDilationFactor) * subConvHeight + (h%originalDilationFactor - height%originalDilationFactor)*(subConvHeight - 1);
//                }
//                totalNumberOfRows += h / originalDilationFactor;
//                //calc number of cols
//                if((width % originalDilationFactor) == 0 || (w % originalDilationFactor)  < (width % originalDilationFactor))  // all the sub conv to the left are of full width
//                {
//                    totalNumberOfCols = w%originalDilationFactor * subConvWidth;
//                }
//                else
//                {
//                    //add width*subConvWidth for of full subConvWidth  + (subConvWidth-1) for the rows of smaller subconvs
//                    totalNumberOfCols = (width % originalDilationFactor) * subConvWidth + (w%originalDilationFactor - width%originalDilationFactor)*(subConvWidth - 1);
//                }
//                totalNumberOfCols += w / originalDilationFactor;
//                unsigned subConvElementIdx = (totalNumberOfCols + totalNumberOfRows*width);

//                unsigned subConvElementOffset = subConvElementIdx * increment;

//                unpopulated_offsets[i++] = (subConvElementOffset << SHIFT_FOR_STORAGE_ELEMENT);
//            }
//        }
//    }

    activationStorageElement->populate(unpopulated_offsets, mv::Order("NHWC"));
}

void createPWLTable(const double& quantOutLow, const double& quantOutHigh,
                    const std::function<double(double)>& refFunction, std::vector<int>& range_vector,
                    std::vector<int>& shift_vector, std::vector<int>& bias_vector) {
    auto pwlfnc = PWLDoubleFit()
                          .setRange(quantOutLow, quantOutHigh)
                          .setFunction(refFunction)
                          .setBitness(8u)
                          .setMaxIntervals(8u)
                          .solve();

    if (pwlfnc.segments().empty()) {
        throw std::runtime_error("populateInstructionListMap: Invalid PWL intervals created");
    }

    for (size_t s = 0; s < pwlfnc.segments().size(); s++) {
        range_vector.push_back(pwlfnc.segments()[s].getRange().begin());
        shift_vector.push_back(pwlfnc.segments()[s].getShift());
        bias_vector.push_back(pwlfnc.segments()[s].getBias());
    }
    // Add the closing interval
    const int closing_interval = static_cast<int>(std::numeric_limits<int8_t>::max());
    range_vector.push_back(closing_interval);

    /*The PWL table currently only works with exactly 8 intervals, so here the table is expanded to 8 intervals if required.
      The table expansion inserts dummy intervals of the max point n times to ensure that there are 9 intervals.*/

    size_t extraIntervals = 9 - range_vector.size();
    size_t lastIntervalShift = shift_vector.back();
    size_t lastIntervalBias = bias_vector.back();

    for (std::size_t i = 0; i < extraIntervals; i++) {
        range_vector.insert(range_vector.end() - 1 - i, closing_interval);
        shift_vector.insert(shift_vector.end() - 1 - i, lastIntervalShift);
        bias_vector.insert(bias_vector.end() - 1 - i, lastIntervalBias);
    }

    // Change the sign of m
    std::for_each(shift_vector.begin(), shift_vector.end(), [](int& i) {
        i *= -1;
    });
}

// NOTE: The whole idea of the pwl is that we are going to use a linear function that represents leaky Relu.
// This comes through the equation and idea of Alessandro
// https://colab.research.google.com/drive/1xTQyJtZiPtMw-r1jUGks-aspbrpuEdKR#scrollTo=biQruEJ7olzD. Idea: We use the
// equation: ((x << m) + b) >> s, and train its variables in order to find a close solution that always satisfies the
// leaky relu. After we generate the instruction list table and we save the values of the registers inside.
// The map of the bits per instruction are described here:
// https://docs.google.com/spreadsheets/d/1RcD1FYGiKCTCRTDsU-J4r_FaQyAQbzMLyRu7WkeNoOY/edit#gid=0.

void populateInstructionListMap(mv::Data::TensorIterator instructionListTable, std::vector<int>& range_vector,
                                std::vector<int>& shift_vector, std::vector<int>& bias_vector) {
    // NOTE : The instruction list has 5 bits of addresses so the biggest count of instructions is 11111 = 27
    // 27 of course will be aligned to 32 and will contain NOPS inside
    auto instructionListShape = instructionListTable->getShape();
    std::vector<uint32_t> template_table(instructionListShape.totalSize(), 0);

    // NOTE: first 2 are hardware reserved areas
    std::size_t ADDR_OF_RESERVED = 6;
    std::size_t ADDR_OF_ADDR_FLEX = 11;
    std::size_t ADDR_OF_FIRST2_BITS = 9;
    std::size_t ADDR_OF_REST_BITS = 16;
    std::size_t ADDR_OF_VALUE = 19;
    std::size_t MASK_FIRST2_BITS = 3;
    std::size_t first2_bits, last3_bits;

    //Populate the instruction list from the table
    std::size_t k = 0;
    for (std::size_t j = 0; j < 32; j++) {
        first2_bits = j & MASK_FIRST2_BITS;
        last3_bits = j >> 2;

        if (j == 15)
            template_table[j] = (ALU_HALT_OPCODE);
        else if (j > 25)
            template_table[j] = (ALU_HALT_OPCODE);
        else {
            if (j < range_vector.size()) {
                template_table[j] = ((range_vector[j] << ADDR_OF_VALUE) | (last3_bits << ADDR_OF_REST_BITS) |
                                     (8 << ADDR_OF_ADDR_FLEX) | (first2_bits << ADDR_OF_FIRST2_BITS) |
                                     (0 << ADDR_OF_RESERVED) | ALU_LOAD);
            } else if (j < range_vector.size() + shift_vector.size() + 1) {
                if (j < 16)
                    template_table[j] = ((shift_vector[j - range_vector.size()] << ADDR_OF_VALUE) |
                                         (last3_bits << ADDR_OF_REST_BITS) | (8 << ADDR_OF_ADDR_FLEX) |
                                         (first2_bits << ADDR_OF_FIRST2_BITS) | (0 << ADDR_OF_RESERVED) | ALU_LOAD);
                else {
                    k = j - 1;
                    first2_bits = k & MASK_FIRST2_BITS;
                    last3_bits = k >> 2;
                    template_table[j] = ((shift_vector[k - range_vector.size()] << ADDR_OF_VALUE) |
                                         (last3_bits << ADDR_OF_REST_BITS) | (8 << ADDR_OF_ADDR_FLEX) |
                                         (first2_bits << ADDR_OF_FIRST2_BITS) | (0 << ADDR_OF_RESERVED) | ALU_LOAD);
                }
            } else if (j < range_vector.size() + shift_vector.size() + bias_vector.size() + 1) {
                k = j - 1;
                first2_bits = k & MASK_FIRST2_BITS;
                last3_bits = k >> 2;
                template_table[j] = ((bias_vector[k - range_vector.size() - shift_vector.size()] << ADDR_OF_VALUE) |
                                     (last3_bits << ADDR_OF_REST_BITS) | (8 << ADDR_OF_ADDR_FLEX) |
                                     (first2_bits << ADDR_OF_FIRST2_BITS) | (0 << ADDR_OF_RESERVED) | ALU_LOAD);
            }
        }
    }

    std::vector<int64_t> template_table_appropriate_type(template_table.begin(), template_table.end());

    instructionListTable->populate(template_table_appropriate_type, mv::Order("NHWC"));
}

static void populateStorageElementPointersFcn(const mv::pass::PassEntry& , mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_PASS)
    mv::OpModel om(model);
    for(auto op : om.getOps("DPUTask"))
    {
        auto taskOp = op->getOpType();
        if (taskOp == "DPUTask")
        {
            if(op->hasAttr("activationSparsityCompilerSolving") &&
                op->get<bool>("activationSparsityCompilerSolving"))
                populateActivationStorageElementMap(op, model);

            // New logic for generating SEP for dilated convolution
            if(op->hasAttr("activationSparsityCompilerSolvingForDilatedConv")
                    && op->get<bool>("activationSparsityCompilerSolvingForDilatedConv"))
            {
                populateActivationStorageElementMapForDilatedConvolution(op, model);
            }

            // New logic for generating SEP for InterpNN
            if(op->hasAttr("activationSparsityCompilerSolvingForInterpNN")
                    && op->get<bool>("activationSparsityCompilerSolvingForInterpNN"))
            {
                populateActivationStorageElementMapForInterpNN(op, model);
            }
        }
    }
}

static void generateWeightsTablesFcn(const mv::pass::PassEntry&, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_PASS)
    mv::OpModel om(model);

    for(auto dpuTaskOp = om.opBegin(); dpuTaskOp != om.opEnd(); ++dpuTaskOp)
    {
        if(dpuTaskOp->getOpType() == "DPUTask")
        {
            auto taskOp = dpuTaskOp->get<std::string>("taskOp");
            if(taskOp == "Conv" ||
               taskOp == "ChannelMajorConvolution" ||
               taskOp == "MaxPool" ||
               taskOp == "DepthwiseConv")
            {
                std::string opName = dpuTaskOp->getName();
                std::string kernelWeightsTableName(mv::createWeightTableName(opName));

                auto outputChannels = dpuTaskOp->getOutputTensor(0)->getShape()[mv::IO_CHANNEL_DIMENSION];
                outputChannels = mv::round_up(dpuTaskOp->getOutputTensor(0)->getShape()[mv::IO_CHANNEL_DIMENSION], 16);

                // per channel layout:
                // 3 -> bias
                // 2 -> mult << 16 | round << 14 |  shift << 8 | prelu
                // 1 -> SP_PTR
                // 0 -> DATA_PTR
                mv::Shape shape({WT_ELEMENTS_PER_CHANNEL, 1, 1, outputChannels});
                std::vector<int64_t> weightsTableData(shape.totalSize(), 0);
                auto weightTable = om.constantInt(kernelWeightsTableName, weightsTableData, shape, mv::DType("Int32"), mv::Order("NHWC"));
                weightTable->set<bool>("weightTable", true);
                om.getSourceOp(weightTable)->set<unsigned>("opId", dpuTaskOp->get<unsigned>("opId"));
                unsigned newSize = dpuTaskOp->addInputTensor(weightTable);
                om.defineFlow(weightTable, dpuTaskOp, newSize - 1);
                dpuTaskOp->set<size_t>("weightsTableIndex", newSize - 1);
            }
        }
    }
}

mv::Data::TensorIterator createInstructionListTable(std::string instructionListTableName, mv::OpModel& om) {
    const std::size_t numberOfInstructions = 25;
    const std::size_t alignedInstructions = mv::round_up(numberOfInstructions, 16);
    mv::Shape shape({alignedInstructions, 1, 1, 1});
    std::vector<int64_t> instructionListTableData(shape.totalSize(), 0);
    auto instructionListTable = om.constantInt(instructionListTableName, instructionListTableData, shape,
                                               mv::DType("Int32"), mv::Order("NHWC"));
    instructionListTable->set<bool>("instructionListTable", true);

    return instructionListTable;
}

static void generateAndPopulateInstructionListTablesFcn(const mv::pass::PassEntry&, mv::ComputationModel& model,
                                                        mv::TargetDescriptor& td, mv::Element&, mv::Element&) {
    MV_PROFILED_FUNCTION(MV_PROFILE_PASS)
    mv::OpModel om(model);

    const std::unordered_map<std::string, std::function<double (double)>> generatable_tables =
    {
        {"Mish", mish}
    };

    // std::unordered_map<std::tuple<

    for (auto dpuTaskOp = om.opBegin(); dpuTaskOp != om.opEnd(); ++dpuTaskOp) {
        if (dpuTaskOp->getOpType() == "DPUTask" && dpuTaskOp->hasAttr("PWLSource")) {
            /* ToDo: Reuse equivalent tables */
            auto instructionListTable =
                    createInstructionListTable(mv::createInstructionListTableName(dpuTaskOp->getName()), om);

            std::vector<int> range_vector;
            std::vector<int> shift_vector;
            std::vector<int> bias_vector;

            auto customPWLSource = dpuTaskOp->get<std::string>("PWLSource");
            auto pwl_type = dpuTaskOp->get<mv::PWLTableType>("PWLType");
            if (customPWLSource == "Generation") {
                auto refFunction = generatable_tables.at(pwl_type.activation);
                auto outQuantParams = dpuTaskOp->getOutputTensor(0)->getQuantParams();
                const auto& quantOutHigh = outQuantParams.getMax();
                const auto& quantOutLow = outQuantParams.getMin();
                if (quantOutHigh.empty()) {
                    throw std::runtime_error(
                            "generateAndPopulateInstructionListTablesFcn: empty output quantization parameters");
                }

                createPWLTable(quantOutLow.at(0), quantOutHigh.at(0), refFunction, range_vector, shift_vector,
                               bias_vector);
            } else if (customPWLSource == "TargetDescriptor") {
                int pwl_table_index = dpuTaskOp->get<int>("PWLIndex");
                auto pwl_table = td.generalTargetConfigs().pwlTables.at(pwl_type)[pwl_table_index];

                range_vector = pwl_table.range;
                shift_vector = pwl_table.shift;
                bias_vector = pwl_table.bias;
            } else {
                /* Never enter */
                throw mv::AttributeError(dpuTaskOp->getLogID(),
                                         " has invalid custom PWL table source: " + customPWLSource);
            }

            populateInstructionListMap(instructionListTable, range_vector, shift_vector, bias_vector);

            dpuTaskOp->set<int>("PWLClampLow", range_vector[0]);
            dpuTaskOp->set<int>("PWLClampHigh", range_vector[range_vector.size() - 1]);

            om.getSourceOp(instructionListTable)->set<unsigned>("opId", dpuTaskOp->get<unsigned>("opId"));
            unsigned newSize = dpuTaskOp->addInputTensor(instructionListTable);
            om.defineFlow(instructionListTable, dpuTaskOp, newSize - 1);
            dpuTaskOp->set<size_t>("instructionListTableIndex", newSize - 1);
        }
    }
}
