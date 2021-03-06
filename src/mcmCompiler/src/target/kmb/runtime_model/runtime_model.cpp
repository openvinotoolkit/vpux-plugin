//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/target/kmb/runtime_model/runtime_model.hpp"
#include "include/mcm/utils/ops_conversion_utils.hpp"
#include "include/mcm/op_model.hpp"
#include "flatbuffers/util.h"
#include "include/mcm/base/exception/argument_error.hpp"
#include "include/mcm/utils/warning_manager.hpp"
#include "include/mcm/utils/custom_math.hpp"
#include "include/mcm/utils/custom_strings.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <unordered_set>
#include <memory>

#include <version.hpp>

#include "schema/graphfile/gf_version.h"

const std::unordered_map<std::string, MVCNN::DType> mv::RuntimeModel::dTypeMapping_ =
{
    {"Float64", MVCNN::DType::DType_FP64},
    {"Float32", MVCNN::DType::DType_FP32},
    {"Float16", MVCNN::DType::DType_FP16},
    {"BFloat16", MVCNN::DType::DType_BFP16},
    {"Float8", MVCNN::DType::DType_FP8},
    {"UInt64", MVCNN::DType::DType_U64},
    {"UInt32", MVCNN::DType::DType_U32},
    {"UInt16", MVCNN::DType::DType_U16},
    {"UInt8", MVCNN::DType::DType_U8},
    {"Int64", MVCNN::DType::DType_I64},
    {"Int32", MVCNN::DType::DType_I32},
    {"Int16", MVCNN::DType::DType_I16},
    {"Int8", MVCNN::DType::DType_I8},
    {"Int4", MVCNN::DType::DType_I4},
    {"Int2", MVCNN::DType::DType_I2},
    {"Int2X", MVCNN::DType::DType_I2X},
    {"Int4X", MVCNN::DType::DType_I4X},
    {"Bin", MVCNN::DType::DType_BIN},
    {"Log", MVCNN::DType::DType_LOG}
};

const std::unordered_map<MVCNN::DType, std::string> mv::RuntimeModel::reverseDTypeMapping_ =
{
    {MVCNN::DType::DType_FP64, "Float64"},
    {MVCNN::DType::DType_FP32, "Float32"},
    {MVCNN::DType::DType_FP16, "Float16"},
    {MVCNN::DType::DType_BFP16, "BFloat16"},
    {MVCNN::DType::DType_FP8, "Float8"},
    {MVCNN::DType::DType_U64, "UInt64"},
    {MVCNN::DType::DType_U32, "UInt32"},
    {MVCNN::DType::DType_U16, "UInt16"},
    {MVCNN::DType::DType_U8, "UInt8"},
    {MVCNN::DType::DType_I64, "Int64"},
    {MVCNN::DType::DType_I32, "Int32"},
    {MVCNN::DType::DType_I16, "Int16"},
    {MVCNN::DType::DType_I8, "Int8"},
    {MVCNN::DType::DType_I4, "Int4"},
    {MVCNN::DType::DType_I2, "Int2"},
    {MVCNN::DType::DType_I2X, "Int2X"},
    {MVCNN::DType::DType_I4X, "Int4X"},
    {MVCNN::DType::DType_BIN, "Bin"},
    {MVCNN::DType::DType_LOG, "Log"}
};

const std::unordered_map<std::string, MVCNN::MemoryLocation> mv::RuntimeModel::memoryLocationMapping_ =
{
    {"ProgrammableInput", MVCNN::MemoryLocation::MemoryLocation_ProgrammableInput},
    {"ProgrammableOutput", MVCNN::MemoryLocation::MemoryLocation_ProgrammableOutput},
    {"ProfilingOutput", MVCNN::MemoryLocation::MemoryLocation_ProfilingOutput},
    {"VPU_DDR_Heap", MVCNN::MemoryLocation::MemoryLocation_VPU_DDR_Heap},
    {"GraphFile", MVCNN::MemoryLocation::MemoryLocation_GraphFile},
    {"VPU_CMX_NN", MVCNN::MemoryLocation::MemoryLocation_VPU_CMX_NN},
    {"VPU_CMX_UPA", MVCNN::MemoryLocation::MemoryLocation_VPU_CMX_UPA},
    {"VPU_DDR_BSS", MVCNN::MemoryLocation::MemoryLocation_VPU_DDR_BSS}
};

const std::unordered_map<std::string, MVCNN::DPULayerType> mv::RuntimeModel::dpuLayerMapping_ =
{
    {"Conv",MVCNN::DPULayerType::DPULayerType_CONV},
    {"DepthwiseConv",MVCNN::DPULayerType::DPULayerType_DWCONV},
    {"MaxPool",MVCNN::DPULayerType::DPULayerType_MAXPOOL},
    {"AveragePool",MVCNN::DPULayerType::DPULayerType_AVEPOOL},
    {"FullyConnected",MVCNN::DPULayerType::DPULayerType_FCL},
    {"Eltwise",MVCNN::DPULayerType::DPULayerType_ELTWISE},
    {"HwConvert",MVCNN::DPULayerType::DPULayerType_ELTWISE},
    {"Identity",MVCNN::DPULayerType::DPULayerType_IDENTITY},
    {"ChannelMajorConvolution",MVCNN::DPULayerType::DPULayerType_CMCONV}
};

const std::map<std::string, int> interpModeMap = {
    {"nearest",      0},
    {"linear",       1},
    {"linear_onnx",  3},
};

const std::map<std::string, int> nearestModeMap = {
    {"round_prefer_floor", 0},
    {"round_prefer_ceil",  1},
    {"floor",              2},
    {"ceil",               3},
    {"simple",             4},
};

const std::map<std::string, int> coordTransformModeMap = {
    {"half_pixel",           0},
    {"pytorch_half_pixel",   1},
    {"asymmetric",           2},
    {"tf_half_pixel_for_nn", 3},
    {"align_corners",        4},
};

const std::unordered_map<mv::PPELayerTypeEnum, MVCNN::PPELayerType, mv::EnumClassHash> mv::RuntimeModel::ppeLayerTypeMapping_ =
{
   {PPELayerType_STORE, MVCNN::PPELayerType::PPELayerType_STORE},
   {PPELayerType_LOAD, MVCNN::PPELayerType::PPELayerType_LOAD},
   {PPELayerType_CLEAR, MVCNN::PPELayerType::PPELayerType_CLEAR},
   {PPELayerType_NOOP, MVCNN::PPELayerType::PPELayerType_NOOP},
   {PPELayerType_HALT, MVCNN::PPELayerType::PPELayerType_HALT},
   {PPELayerType_ADD, MVCNN::PPELayerType::PPELayerType_ADD},
   {PPELayerType_SUB, MVCNN::PPELayerType::PPELayerType_SUB},
   {PPELayerType_MULT, MVCNN::PPELayerType::PPELayerType_MULT},
   {PPELayerType_RELU, MVCNN::PPELayerType::PPELayerType_LRELU},
   {PPELayerType_RELUX, MVCNN::PPELayerType::PPELayerType_LRELUX},
   {PPELayerType_LPRELU, MVCNN::PPELayerType::PPELayerType_LPRELU},
   {PPELayerType_MAXIMUM, MVCNN::PPELayerType::PPELayerType_MAXIMUM},
   {PPELayerType_MINIMUM, MVCNN::PPELayerType::PPELayerType_MINIMUM},
   {PPELayerType_CEIL, MVCNN::PPELayerType::PPELayerType_CEIL},
   {PPELayerType_FLOOR, MVCNN::PPELayerType::PPELayerType_FLOOR},
   {PPELayerType_AND, MVCNN::PPELayerType::PPELayerType_AND},
   {PPELayerType_OR, MVCNN::PPELayerType::PPELayerType_OR},
   {PPELayerType_XOR, MVCNN::PPELayerType::PPELayerType_XOR},
   {PPELayerType_NOT, MVCNN::PPELayerType::PPELayerType_NOT},
   {PPELayerType_ABS, MVCNN::PPELayerType::PPELayerType_ABS},
   {PPELayerType_NEG, MVCNN::PPELayerType::PPELayerType_NEG},
   {PPELayerType_POW, MVCNN::PPELayerType::PPELayerType_POW},
   {PPELayerType_EXP, MVCNN::PPELayerType::PPELayerType_EXP},
   {PPELayerType_SIGMOID, MVCNN::PPELayerType::PPELayerType_SIGMOID},
   {PPELayerType_TANH, MVCNN::PPELayerType::PPELayerType_TANH},
   {PPELayerType_SQRT, MVCNN::PPELayerType::PPELayerType_SQRT},
   {PPELayerType_RSQRT, MVCNN::PPELayerType::PPELayerType_RSQRT},
   {PPELayerType_FLEXARB, MVCNN::PPELayerType::PPELayerType_FLEXARB}
};

template <typename T1, typename T2>
void setIfPresent(T1& fieldToFill, mv::Element& compilationDescriptor, const std::string& key)
{
    if(compilationDescriptor.hasAttr(key))
        fieldToFill = compilationDescriptor.get<T2>(key);
}

mv::RuntimeModel::RuntimeModel(const mv::TargetDescriptor& td)
{
    if (td.getCodecName() == mv::CodecType::HDE)
    {
        auto hdeDef = std::dynamic_pointer_cast<mv::HdeDescriptor>(td.codecDef());
        if (!hdeDef)
            throw std::runtime_error("Failed to get pointer to HDE codec.");

        auto hde = new Hde(hdeDef->bitPerSymbol, hdeDef->maxNumberEncodedSymbols, 0, hdeDef->blockSize, false, hdeDef->bypassMode);

        if (!hde)
            throw std::runtime_error("Memory allocation for HDE codec failed.");

        codec_.reset(hde);
    }
    else
    {
        throw std::runtime_error("Unsupported target selected. Please check target descriptor.");
    }
}

//Note: 16/8 used below depend on the dtype
void mv::RuntimeModel::alignTensor(mv::ComputationModel& cm, std::unique_ptr<MVCNN::TensorReferenceT>& tensorT, mv::Tensor& tensor, const size_t dimension, bool padFinalOutput)
{

    if(dimension == IO_CHANNEL_DIMENSION)
    {
        auto globalConfigParams = cm.getGlobalConfigParams();
        int pad = tensor.computeAppropriatePadding();
        std::vector<std::size_t> dimensions = tensor.getShape();
        auto outputChannelsPadded = mv::round_up(dimensions[mv::IO_CHANNEL_DIMENSION], pad);
        dimensions = {dimensions[mv::IO_WIDTH_DIMENSION], dimensions[mv::IO_HEIGHT_DIMENSION], outputChannelsPadded, dimensions[mv::IO_BATCH_DIMENSION]};
        auto numericStrides = tensor.getOrder().computeByteStrides(mv::Shape(dimensions), tensor.getDType().getSizeInBits() / 8);
        numericStrides.push_back(tensor.getDType().getSizeInBits() / 8);
        std::reverse(dimensions.begin(), dimensions.end());
        std::reverse(numericStrides.begin(), numericStrides.end());
        tensorT->strides = numericStrides; // NOTE: Maybe directly bufferIt->computeStrides() in the future
        if(padFinalOutput)
            tensorT->dimensions = std::vector<uint32_t>(dimensions.begin(), dimensions.end());
    }
    else if (dimension == IO_WIDTH_DIMENSION)
    {
        std::vector<std::size_t> dimensions = tensor.getShape();
        auto widthPadded = mv::round_up(dimensions[mv::IO_WIDTH_DIMENSION], 16);
        dimensions = {widthPadded, dimensions[mv::IO_HEIGHT_DIMENSION],dimensions[mv::IO_CHANNEL_DIMENSION] , dimensions[mv::IO_BATCH_DIMENSION]};
        auto numericStrides = tensor.getOrder().computeByteStrides(mv::Shape(dimensions), tensor.getDType().getSizeInBits() / 8);
        numericStrides.push_back(tensor.getDType().getSizeInBits() / 8);
        std::reverse(dimensions.begin(), dimensions.end());
        std::reverse(numericStrides.begin(), numericStrides.end());
        tensorT->strides = numericStrides;

    }
}

MVCNN::DType mv::RuntimeModel::convertDtype(const mv::DType& dtype)
{
    return dTypeMapping_.at(dtype.toString());
}

mv::DType mv::RuntimeModel::convertDtype(const MVCNN::DType& dtype)
{
    return reverseDTypeMapping_.at(dtype);
}

MVCNN::TargetDevice mv::RuntimeModel::mapTargetDevice(const mv::Target& target)
{
    switch (target)
    {
    case mv::Target::ma2490:
        return MVCNN::TargetDevice::TargetDevice_VPUX30XX;
    case mv::Target::ma3100:
        return MVCNN::TargetDevice::TargetDevice_VPUX311X;
    case mv::Target::ma3720:
        return MVCNN::TargetDevice::TargetDevice_VPUX37XX;
    default:
        return MVCNN::TargetDevice::TargetDevice_NONE;
    }

    return MVCNN::TargetDevice::TargetDevice_NONE;
}

MVCNN::TargetDeviceRevision mv::RuntimeModel::mapTargetDeviceRevision(const std::shared_ptr<mv::Element>& descriptor)
{
    if (!descriptor || !descriptor->hasAttr("DeviceRevision"))
        return MVCNN::TargetDeviceRevision::TargetDeviceRevision_NONE;

    std::string revision = descriptor->get<std::string>("DeviceRevision");

    if (revision == "A0")
        return MVCNN::TargetDeviceRevision::TargetDeviceRevision_A0;
    else if (revision == "B0")
        return MVCNN::TargetDeviceRevision::TargetDeviceRevision_B0;

    return MVCNN::TargetDeviceRevision::TargetDeviceRevision_NONE;
}

MVCNN::MemoryLocation mv::RuntimeModel::convertAllocatorToMemoryLocale(const std::string& allocatorName,
                                                                       const mv::Tensor::MemoryLocation& tensorLocation)
{
    if (tensorLocation == mv::Tensor::MemoryLocation::Location::CSRAM)
    {
        return MVCNN::MemoryLocation::MemoryLocation_VPU_CSRAM;
    }
    else
    {
        // TODO: It's unclear that the allocator is the correct way to
        // find the tensor's memory locale; it might be better to use
        // the MemoryLocation in all cases.
        return memoryLocationMapping_.at(allocatorName);
    }
}

MVCNN::PPELayerType mv::RuntimeModel::convertPPELayerType(PPELayerTypeEnum ppe)
{
    return ppeLayerTypeMapping_.at(ppe);
}


std::unique_ptr<MVCNN::GraphNodeT> mv::RuntimeModel::buildGraphNodeT(mv::ComputationModel &cm, mv::Element&, mv::Data::OpListIterator op)
{
    std::unique_ptr<MVCNN::GraphNodeT> toBuild = std::unique_ptr<MVCNN::GraphNodeT>(new MVCNN::GraphNodeT());

    mv::OpModel opModel(cm);
    toBuild->name = op->getName();
    toBuild->thisID = op->get<unsigned>("opId");

    for (auto nextChildOp = op.leftmostChild(); nextChildOp != opModel.opEnd(); ++nextChildOp)
        toBuild->sinkID.push_back(nextChildOp->get<unsigned>("opId"));

    for (auto nextParentOp = op.leftmostParent(); nextParentOp != opModel.opEnd(); ++nextParentOp)
        toBuild->sourceID.push_back(nextParentOp->get<unsigned>("opId"));

    return toBuild;
}

std::unique_ptr<MVCNN::SourceStructureT> mv::RuntimeModel::buildSourceStructureT(mv::ComputationModel &cm, mv::Element &compilationDescriptor)
{
    std::unique_ptr<MVCNN::SourceStructureT> toBuild = std::unique_ptr<MVCNN::SourceStructureT>(new MVCNN::SourceStructureT());

    mv::OpModel opModel(cm);
    for (auto inputOp : opModel.getNetworkInputs())
        toBuild->first_ID.push_back(inputOp->get<unsigned>("opId"));
    toBuild->nodes = std::vector<std::unique_ptr<MVCNN::GraphNodeT>>(opModel.opsCount());
    unsigned i = 0;

    for(auto opIt = opModel.opBegin(); opIt != opModel.opEnd(); ++opIt)
        toBuild->nodes[i++] = buildGraphNodeT(cm, compilationDescriptor, opIt);

    return toBuild;
}

std::vector<unsigned> mv::RuntimeModel::reduceQuantVector_(std::vector<unsigned> inVec)
{
    if (inVec.size() > 1)
    {
        auto firstVal = inVec[0];
        auto onlyOneValue = true;
        for (size_t i = 1; i < inVec.size(); i++)
            if (firstVal != inVec[i])
                onlyOneValue = false;
        if (onlyOneValue)
        {
            inVec.clear();
            inVec.push_back(firstVal);
        }
    }
    return inVec;
}

bool mv::RuntimeModel::targetEmulator_(mv::Element& compilationDescriptor)
{
    return compilationDescriptor.hasAttr("target_emulator") && compilationDescriptor.get<bool>("target_emulator");
}

static std::uint64_t schemaOrder(const mv::Order& order) {
    const std::string str = order.toString();
    return std::accumulate(str.cbegin(), str.cend(), static_cast<std::uint64_t>(0),
                           [](const std::uint64_t result, const char d) {
                               // start from 1 to avoid ambiguous leading zeros
                               return result * 10 + mv::Shape::getAxis(std::string(1, d)) + 1;
                           });
}

//build tensorReference for Tensors - 1 cluster case
std::unique_ptr<MVCNN::TensorReferenceT> mv::RuntimeModel::buildTensorReferenceT(mv::ComputationModel& model, mv::Element& compilationDescriptor, mv::Data::TensorIterator t, const std::string &allocatorName)
{
    mv::DataModel dm(model);
    mv::OpModel om(model);

    std::unique_ptr<MVCNN::TensorReferenceT> toBuild = std::unique_ptr<MVCNN::TensorReferenceT>(new MVCNN::TensorReferenceT());

    if (targetEmulator_(compilationDescriptor))
    {
        toBuild->name = t->getName();

        toBuild->dimensions = t->getShape();
        std::reverse(toBuild->dimensions.begin(), toBuild->dimensions.end());

        toBuild->data_dtype = convertDtype(t->getDType().toString());
        toBuild->strides = t->computeNumericStrides();
        toBuild->strides.push_back(t->getDType().getSizeInBits() / 8);
        std::reverse(toBuild->strides.begin(), toBuild->strides.end());

        if (t->hasAttr("quantParams"))
        {
            const mv::QuantizationParams quantParams = t->getQuantParams();
            if (quantParams.hasAttr("zeroPoint"))
            {
                const std::vector<int64_t> zeroPoint = quantParams.getZeroPoint();
                toBuild->quant_zero.resize(zeroPoint.size());
                std::copy(zeroPoint.cbegin(), zeroPoint.cend(), toBuild->quant_zero.begin());
            }
            if (quantParams.hasAttr("mult"))
            {
                const std::vector<uint32_t> mult = quantParams.getMult();
                toBuild->quant_mult.resize(mult.size());
                std::copy(mult.cbegin(), mult.cend(), toBuild->quant_mult.begin());
            }
            if (quantParams.hasAttr("shift"))
            {
                const std::vector<uint32_t> shift = quantParams.getShift();
                toBuild->quant_shift.resize(shift.size());
                std::copy(shift.cbegin(), shift.cend(), toBuild->quant_shift.begin());
            }
        }
        toBuild->order = schemaOrder(t->getOrder());
        toBuild->data = std::unique_ptr<MVCNN::IndirectDataReferenceT>(new MVCNN::IndirectDataReferenceT());

        if (t->isPopulated())
        {
            toBuild->locale = MVCNN::MemoryLocation::MemoryLocation_GraphFile;
            toBuild->data->data_index = 0;
            // empty tensors will not have graphFileIndex because they will not be saved in the blob
            unsigned graphfileIndex = t->hasAttr("graphFileIndex")? t->get<unsigned>("graphFileIndex"): 0;
            toBuild->locale_index = std::vector<unsigned int>(1);
            toBuild->locale_index[0] = graphfileIndex;
        }
        return toBuild;
    }

    toBuild->name = t->getName();

    const auto& tensorLocation = t->get<mv::Tensor::MemoryLocation>("Location");

    auto tensorAllocators = t->get<std::set<std::string>>("allocators");

    auto tensorAllocatorName = tensorAllocators.begin();
    if(!allocatorName.empty())
        tensorAllocatorName = tensorAllocators.find(allocatorName);

    if(tensorAllocatorName == tensorAllocators.end()){
        throw mv::ArgumentError(om, "buildTensorReferenceT", "0", "No tensor allocators found");
    }
    auto tensorAllocator = dm.getAllocator(*tensorAllocatorName);
    mv::Data::BufferIterator tensorBufferIt = tensorAllocator.getBuffer(0, t); // 0 is the only stage for now, but this will probably change in the future

    auto underlyingTensor = tensorBufferIt->getData();
    std::vector<uint32_t> dimensions = underlyingTensor->getShape();
    //NOTE: the buffer strides are used only for changing between the normal strides and the buffer strides
    std::vector<float> dilatedStrides(4, 0);
    std::vector<float> bufferStrides(4, 0);

    auto explicitStrides = (om.getSourceOp(t) && om.getSourceOp(t).leftmostChild() &&
                            om.getSourceOp(t).leftmostChild()->hasAttr("explicitStrides") && om.getSourceOp(t).leftmostChild()->get<bool>("explicitStrides"));
    auto masterBuffer = tensorAllocator.getTopMasterBuffer(tensorBufferIt);
    std::vector<float> numericStrides;
    if ((t->hasAttr("leadingOffset") && *tensorAllocatorName == "VPU_CMX_NN" ) ||
            (t->hasAttr("dilatedSlice") && *tensorAllocatorName == "VPU_CMX_NN" ))
        numericStrides = tensorBufferIt->getData()->computeNumericStrides();
    else if (explicitStrides)
        numericStrides = t->computeNumericStrides();
    else
        numericStrides = (*masterBuffer)->getData()->computeNumericStrides();

    if ((t->hasAttr("dilatedWidthConcat") && t->get<bool>("dilatedWidthConcat")) ||
            (t->hasAttr("dilatedSlices3DDMA") && t->get<bool>("dilatedSlices3DDMA")))
    {
        //NOTE: Covered only strides for z-major convolution
        for (std::size_t idx = 0; idx < numericStrides.size(); idx++)
        {
            auto dilationFactor = t->get<unsigned>("dilationFactor");
            if (idx == 0 || idx == 1)
                dilatedStrides[idx] = dilationFactor * numericStrides[idx];
            else
                dilatedStrides[idx] = numericStrides[idx];
        }
        bufferStrides = numericStrides;
        numericStrides = dilatedStrides;
        dilatedStrides = bufferStrides;
    }
    if ((*tensorAllocatorName == "VPU_DDR_Heap" && (t->hasAttr("fusedConcatReshape") && t->get<bool>("fusedConcatReshape"))) ||
        (*tensorAllocatorName == "VPU_CMX_NN" && (t->hasAttr("asymmetricCmxConcat") && t->get<bool>("asymmetricCmxConcat"))))
    {
        for (std::size_t idx = 0; idx < numericStrides.size(); idx++)
        {
            if (idx == mv::IO_HEIGHT_DIMENSION || idx == mv::IO_BATCH_DIMENSION)
                numericStrides[idx] = numericStrides[idx] * t->get<std::size_t>("numberOfConvsForAsymmetricalStride");
        }
    }

    numericStrides.push_back(underlyingTensor->getDType().getSizeInBits() / 8);

    //Because according to graphfile order is given as NCHW, which is exactly the reverse of our shape assumption WHCN
    std::reverse(dimensions.begin(), dimensions.end());
    std::reverse(numericStrides.begin(), numericStrides.end());

    toBuild->dimensions = std::vector<uint32_t>(dimensions.begin(), dimensions.end());
    toBuild->strides = numericStrides; // NOTE: Maybe directly bufferIt->computeStrides() in the future?

    toBuild->data = std::unique_ptr<MVCNN::IndirectDataReferenceT>(new MVCNN::IndirectDataReferenceT());
    if (*tensorAllocatorName == "GraphFile")
    {
        toBuild->data->data_index = 0;
        // empty tensors will not have graphFileIndex because they will not be saved in the blob
        unsigned graphfileIndex = t->hasAttr("graphFileIndex")? t->get<unsigned>("graphFileIndex"): 0;
        toBuild->locale_index = std::vector<unsigned int>(1);
        toBuild->locale_index[0] = graphfileIndex;
    }
    else if(*tensorAllocatorName == "ProgrammableInput" || *tensorAllocatorName == "ProgrammableOutput")
    {
        toBuild->data->data_index = 0;
        toBuild->locale_index = std::vector<unsigned int>(1,0);

        if ((*masterBuffer)->getData()->hasAttr("inputIndex"))
            toBuild->locale_index[0] = (*masterBuffer)->getData()->get<uint8_t>("inputIndex");
        else if ((*masterBuffer)->getData()->hasAttr("outputIndex"))
            toBuild->locale_index[0] = (*masterBuffer)->getData()->get<uint8_t>("outputIndex");

        if (t->hasAttr("dilatedWidthConcat") && t->get<bool>("dilatedWidthConcat"))
        {
            toBuild->data->data_index += dilatedStrides[0] * t->get<std::size_t>("inputConcatTensorIdx");
            toBuild->data->data_index += dilatedStrides[1] * t->get<std::size_t>("lineofConcatHeight");
            if (t->hasAttr("streamKId"))
                //NOTE could use dimensions[1], dim[2] but the last stream can have dim < than the previous
                toBuild->data->data_index += t->get<unsigned>("streamKId") * t->get<std::size_t>("symmetrical_first_dimensionK");
            else if (t->hasAttr("streamHId"))
                toBuild->data->data_index += t->get<unsigned>("streamHId")
                    * numericStrides[3] * t->get<std::size_t>("symmetrical_first_dimensionH");

        }
        else if (t->hasAttr("dilatedSlices3DDMA") &&
                             t->get<bool>("dilatedSlices3DDMA"))
        {
            toBuild->data->data_index += dilatedStrides[0] * t->get<std::size_t>("inputConcatTensorIdx");
            toBuild->data->data_index += dilatedStrides[1] * t->get<std::size_t>("lineofConcatHeight");

            if (t->hasAttr("streamHId"))
            {
                auto strides = tensorBufferIt->getStrides();
                auto tShape = t->getShape();
                //leading offset = (number of lines before) * C * W (C and W are the same for all streams over H)
                auto leading_offset = strides[0]/(tShape[mv::IO_WIDTH_DIMENSION] * tShape[mv::IO_CHANNEL_DIMENSION]);

                //NOTE could use dimensions[1], dim[2] but the last stream can have dim < than the previous
                toBuild->data->data_index += numericStrides[3]
                        *leading_offset;
            }

        }
        else
        {
            auto strides = tensorBufferIt->getStrides();
            auto leading_offset = strides[0];
            if (leading_offset)
                toBuild->data->data_index += leading_offset;
        }

        //NOTE: adjust dilation + concat indexing, when a dilation conv/deconv is not idx = 0 input of concat
        if (t->hasAttr("leftIndexDilation"))
            toBuild->data->data_index += t->get<std::size_t>("leftIndexDilation");
    }
    else
    {
        //NOTE: Sometimes we need to align and use the strides[0], cause when we change axis in concats
        //in the end we need to mutiply the offsets with the dimensions of the last master Buffer, but
        //WATCH OUT!!! not when the axis remain the same...more See allocate_memory_kmb.cpp line 552
        auto strides = tensorBufferIt->getStrides();
        std::size_t leading_offset = strides[0];
        auto temporaryBuffer = tensorBufferIt;
        auto recursiveBuffer = tensorBufferIt;
        if (t->hasAttr("leftIndex"))
        {
            bool sameAxisMasterBuffers = true;
            while (recursiveBuffer->getData()->getName() !=
                   (*tensorAllocator.getTopMasterBuffer(recursiveBuffer))->getData()->getName())
            {
                recursiveBuffer = tensorAllocator.getSimpleMasterBuffer(recursiveBuffer);
                auto newMaster = *tensorAllocator.getSimpleMasterBuffer(recursiveBuffer);
                if (dm.getTensor(recursiveBuffer->getData()->getName())->hasAttr("concatAxis") &&
                        dm.getTensor(newMaster->getData()->getName())->hasAttr("concatAxis") )
                {
                    if (dm.getTensor(recursiveBuffer->getData()->getName())->get<std::string>("concatAxis") !=
                            dm.getTensor(newMaster->getData()->getName())->get<std::string>("concatAxis"))
                    {
                        sameAxisMasterBuffers = false;
                        break;
                    }
                }
            }
            if (sameAxisMasterBuffers)
            {
                leading_offset = t->get<std::size_t>("leftIndex") * toBuild->strides[0];
                while (temporaryBuffer->getData()->getName() != (*masterBuffer)->getData()->getName())
                {
                    temporaryBuffer = temporaryBuffer->getMaster();
                    if (dm.getTensor(temporaryBuffer->getData()->getName())->hasAttr("leftIndex"))
                        leading_offset += dm.getTensor(temporaryBuffer->getData()->getName())->get<std::size_t>("leftIndex") *
                                toBuild->strides[0];
                    if (dm.getTensor(temporaryBuffer->getData()->getName())->hasAttr("indexDecrease") &&
                        dm.getTensor(temporaryBuffer->getData()->getName())->get<bool>("indexDecrease"))
                        leading_offset -= dm.getTensor(temporaryBuffer->getData()->getName())->get<std::size_t>("leftIndexDecrease") *
                                toBuild->strides[0];
                }
            }
            if (t->hasAttr("left_asymmetric_index"))
                leading_offset += t->get<std::size_t>("left_asymmetric_index");
        }
//        if (leading_offset != strides[0])
//        {
//            std::size_t initAddr = 0;
//            if (t->hasAttr("address"))
//                initAddr = t->getAddress();
//            else
//                initAddr = (*masterBuffer)->getOffset();
//            std::cout << "Tensor with name: " + t->getName()  + " and with data index: " + std::to_string(initAddr) +
//                         " has difference in values leading offset of: " + std::to_string(leading_offset) + " and stride[0]: " +
//                         std::to_string(strides[0]) << std::endl;
//        }
        toBuild->locale_index = std::vector<unsigned int>(1,0);

        // This part is for concat
        if(t->hasAttr("address"))
            toBuild->data->data_index = t->getAddress();
        else
        {
            // The storage element pointers offsets generated in populateActivationStorageElementMapForLayerAfterDilatedConvolution()
            // are calculated from the smallest address of the input tenor to the ImplicitUnion operation
            // Here we have to ensure that the data_index of this tensor is the same smallest address otherwise the SEPs
            // offsets will point to the wrong location
            auto parentOp = om.getSourceOp(t);
            if(parentOp->getOpType() == "ImplicitJoin")
            {
                auto numberInputs = parentOp.inputsSize();
                auto minBaseAddress = parentOp->getInputTensor(0)->getAddress();
                for (size_t i=1; i < numberInputs; i++)
                {
                    auto address = parentOp->getInputTensor(i)->getAddress();
                    if (address < minBaseAddress)
                    minBaseAddress = address;
                }
                toBuild->data->data_index = minBaseAddress;
            }
            else
                //toBuild->data->data_index = tensorBufferIt->getOffset();
                toBuild->data->data_index = (*masterBuffer)->getOffset();
        }

        if (t->hasAttr("dilatedWidthConcat") && t->get<bool>("dilatedWidthConcat"))
        {
            toBuild->data->data_index += dilatedStrides[0] * t->get<std::size_t>("inputConcatTensorIdx");
            toBuild->data->data_index += dilatedStrides[1] * t->get<std::size_t>("lineofConcatHeight");
            if (t->hasAttr("streamKId"))
                //NOTE could use dimensions[1], dim[2] but the last stream can have dim < than the previous
                toBuild->data->data_index += t->get<unsigned>("streamKId") * t->get<std::size_t>("symmetrical_first_dimensionK");
            else if (t->hasAttr("streamHId"))
                toBuild->data->data_index += t->get<unsigned>("streamHId")
                    * numericStrides[3] * t->get<std::size_t>("symmetrical_first_dimensionH");

        }
        else if (t->hasAttr("dilatedSlices3DDMA") &&
                             t->get<bool>("dilatedSlices3DDMA"))
        {
            toBuild->data->data_index += dilatedStrides[0] * t->get<std::size_t>("inputConcatTensorIdx");
            toBuild->data->data_index += dilatedStrides[1] * t->get<std::size_t>("lineofConcatHeight");
            if (t->hasAttr("streamHId"))
            {
                auto tShape = t->getShape();
                //leading offset = (number of lines before) * C * W (C and W are the same for all streams over H)
                auto local_leading_offset = leading_offset /
                        (tShape[mv::IO_WIDTH_DIMENSION] * tShape[mv::IO_CHANNEL_DIMENSION]);

                //NOTE could use dimensions[1], dim[2] but the last stream can have dim < than the previous
                toBuild->data->data_index += numericStrides[3]
                        * local_leading_offset;
            }
        }
        else
            toBuild->data->data_index += leading_offset;

        if(t->isSparse())
        {
            toBuild->data->sparsity_index = t->getSparsityMap()->getAddress();
            if(!t->isPopulated())
                toBuild->data->storage_element_index = t->getStorageElement()->getAddress();
            else
                toBuild->data->storage_element_index = 0;
        }

        //NOTE: adjust dilation + concat indexing, when a dilation conv/deconv is not idx = 0 input of concat
        if (t->hasAttr("leftIndexDilation"))
            toBuild->data->data_index += t->get<std::size_t>("leftIndexDilation");

        if (t->hasAttr("sliceAddress"))
            toBuild->data->data_index = t->get<std::size_t>("sliceAddress");
    }

    toBuild->locale = convertAllocatorToMemoryLocale(*tensorAllocatorName, tensorLocation);
    toBuild->data_dtype = convertDtype(t->getDType());

    if (t->hasAttr("base_ptrs"))
        toBuild->base_ptrs = t->get<std::vector<unsigned short>>("base_ptrs");

    // could also be t->hasAttr("quantizationParameters")
    // but in my opinion quantization for a tensor of floats makes very little sense
    // leaving this comment here for future generations
    // future generations say that needs to be serialized even in case of 0s and 1s(z_p, sc)
    if(t->isQuantized())
    {
        auto quantizationParams = t->get<mv::QuantizationParams>("quantParams");

        // acording to the runtime code, Zero point only uses first value of array.
        // mult and shift are only used for eltwise output, not for other outputs.
        // https://github.com/movidius/vpuip_2/blob/develop/system/nn/nce_lib/src/2490/ppe_task.cpp
        auto quantZero = quantizationParams.getZeroPoint();
        if (quantZero.size() > 0)
        {
            toBuild->quant_zero = std::vector<unsigned char>(1, quantZero[0]);
        }
        else
        {
            Logger::log(mv::Logger::MessageType::Info, "RuntimeModel", "no zeropoints are specifed");
        }

        std::vector<unsigned> quantMult = {};
        if (quantizationParams.hasAttr("mult"))
        {
            quantMult = quantizationParams.getMult();

            if (quantMult.size() > 0)
            {
                quantMult = reduceQuantVector_(quantMult);
                toBuild->quant_mult = std::vector<unsigned short int>(1, quantMult[0]);
            }
            else
            {
                Logger::log(mv::Logger::MessageType::Info, "RuntimeModel", "no mults are specified");
            }
        }

        std::vector<unsigned> quantShift;
        if (quantizationParams.hasAttr("shift"))
        {
            quantShift = quantizationParams.getShift();

            if (quantShift.size() > 0)
            {
                quantShift = reduceQuantVector_(quantShift);
                toBuild->quant_shift = std::vector<unsigned char>(1, quantShift[0]);
            }
            else
            {
                Logger::log(mv::Logger::MessageType::Info, "RuntimeModel", "no shifts are specified");
            }
        }
        toBuild->quant_post_shift_right = quantizationParams.getPostShift();
    }

    return toBuild;
}

//update tensorReference for subTensors - multiple clusters
void mv::RuntimeModel::updateTensorReferenceT(mv::ComputationModel& cm, mv::Element&, mv::Data::TensorIterator s, mv::Data::TensorIterator d, unsigned clusterId, std::unique_ptr<MVCNN::TensorReferenceT>& tensorT, const std::string& allocatorName)
{
    mv::DataModel dm(cm);

    auto tensorAllocators = s->get<std::set<std::string>>("allocators");

    auto tensorAllocatorName = tensorAllocators.begin();
    if(!allocatorName.empty())
        tensorAllocatorName = tensorAllocators.find(allocatorName);

    if(tensorAllocatorName == tensorAllocators.end()){
        throw mv::ArgumentError(cm, "updateTensorReferenceT", "0", "No tensor allocators found");
    }
    auto tensorAllocator = dm.getAllocator(*tensorAllocatorName);
    mv::Data::BufferIterator tensorBufferIt = tensorAllocator.getBuffer(0, s); // 0 is the only stage for now, but this will probably change in the future

    const mv::Tensor& subtensor = d->getSubTensor(clusterId);

    // Shape is always of the subtensor
    // If the tensor is broadcasted, then the shape of the subtensor is equal to the shape of the master tensor
    // if not, the subtensor shape is adjusted accordingly

    // Strides are computed depending on the memory location
    // Since subtensors are split only in CMX
    // In CMX we use the strides of the subtensor
    // In DDRs style memory we use the strides of the master tensor

    auto offset = subtensor.get<std::vector<std::size_t>>("offset");
    auto index = d->getOrder().subToInd(d->getShape(), offset);
    auto byte_index = index * d->getDType().getSizeInBits() / 8;

    // NOTE: This probably has to be done also when DDR kicks in
    // as CMX is the only memory with the cluster/slice approach
    auto starting_address = 0;
    auto masterBuffer = tensorAllocator.getTopMasterBuffer(tensorBufferIt);
    if(s->hasAttr("address"))
        starting_address = s->get<std::size_t>("address");
    else
    {
        starting_address = (*masterBuffer)->getOffset();
    }
    tensorT->data->data_index = starting_address + byte_index;

    auto strides = tensorBufferIt->getStrides();
    auto leading_offset = strides[0];
    tensorT->locale_index = std::vector<unsigned int>(1,0);
    if (leading_offset)
        tensorT->data->data_index += leading_offset;
}

//build tensorReference for subTensors - multiple clusters
std::unique_ptr<MVCNN::TensorReferenceT> mv::RuntimeModel::buildTensorReferenceT(mv::ComputationModel& cm, mv::Element&, mv::Data::TensorIterator t, unsigned clusterId, const std::string& allocatorName, bool isOutput)
{
    mv::DataModel dm(cm);
    mv::OpModel om(cm);
    unsigned numTask = 0;
    numTask = cm.getGlobalConfigParams()->get<int>("Number_of_Clusters");
    const mv::Tensor& subtensor = t->getSubTensor(clusterId);

    std::unique_ptr<MVCNN::TensorReferenceT> toBuild = std::unique_ptr<MVCNN::TensorReferenceT>(new MVCNN::TensorReferenceT());

    toBuild->name = subtensor.getName();

    auto tensorLocation = t->get<mv::Tensor::MemoryLocation>("Location");

    auto tensorAllocators = t->get<std::set<std::string>>("allocators");
    if (tensorAllocators.empty())
        throw mv::ArgumentError("buildTensorReferenceT", "",  "Tensor Allocators empty", "");
    auto tensorAllocatorName = tensorAllocators.begin();
    if(!allocatorName.empty())
        tensorAllocatorName = tensorAllocators.find(allocatorName);
    auto tensorAllocator = dm.getAllocator(*tensorAllocatorName);

    auto explicitStrides = (om.getSourceOp(t) && om.getSourceOp(t).leftmostChild() &&
                            om.getSourceOp(t).leftmostChild()->hasAttr("explicitStrides") && om.getSourceOp(t).leftmostChild()->get<bool>("explicitStrides"));

    mv::Data::BufferIterator tensorBufferIt = tensorAllocator.getBuffer(0, t); // 0 is the only stage for now, but this will probably change in the future

    // Shape is always of the subtensor
    // If the tensor is broadcasted, then the shape of the subtensor is equal to the shape of the master tensor
    // if not, the subtensor shape is adjusted accordingly
    std::vector<uint32_t> dimensions = subtensor.getShape();

    // Strides are computed depending on the memory location
    // Since subtensors are split only in CMX
    // In CMX we use the strides of the subtensor
    // In DDRs style memory we use the strides of the master tensor
    auto numericStrides = t->computeNumericStrides();
    numericStrides.push_back(t->getDType().getSizeInBits() / 8);

    if (*tensorAllocatorName == "VPU_CMX_NN" || *tensorAllocatorName == "ProgrammableOutput" ||
        (*tensorAllocatorName == "VPU_DDR_Heap" && !subtensor.getOrder().isColMajor())) {
        // NOTE: if I go from a k-split strategy to splitOverH i need to use my whole tensor,
        // cause the parent is full on its location, if I go from soh to soh i will dma the subs
        auto masterBuffer = tensorAllocator.getTopMasterBuffer(tensorBufferIt);
        bool attrIsEqual = false;
        if ((*masterBuffer)->getData()->hasAttr("splitStrategy")) {
            const auto attrData = (*masterBuffer)->getData()->get<std::string>("splitStrategy");
            attrIsEqual = (attrData == "SplitOverH" || attrData == "SplitOverHOverlapped" || attrData == "HKSwitch");
        }

        if (attrIsEqual)
            numericStrides = (*masterBuffer)->getData()->getSubTensor(clusterId).computeNumericStrides();
        else if (explicitStrides)
            numericStrides = t->computeNumericStrides();
        else
            numericStrides = (*masterBuffer)->getData()->computeNumericStrides();
        if (*tensorAllocatorName == "VPU_DDR_Heap" && (t->hasAttr("fusedConcatReshape") && t->get<bool>("fusedConcatReshape")))
        {
            for (std::size_t idx = 0; idx < numericStrides.size(); idx++)
            {
                if (idx == mv::IO_HEIGHT_DIMENSION || idx == mv::IO_BATCH_DIMENSION)
                    numericStrides[idx] = numericStrides[idx] * t->get<std::size_t>("numberOfConvsForAsymmetricalStride");
            }
        }
        numericStrides.push_back(subtensor.getDType().getSizeInBits() / 8);
    }
    else if (*tensorAllocatorName == "ProgrammableInput")
    {
        // this section updates the strides for inputs to ChMajor Conv where Streams:H>1 and SOH
        // Strides need to be based on masterBuffer (parent tensor), not the streamed slice
        auto masterBuffer = tensorAllocator.getTopMasterBuffer(tensorBufferIt);
        if ( (*masterBuffer)->getData()->hasAttr("splitStrategy") &&
            ((*masterBuffer)->getData()->get<std::string>("splitStrategy") == "SplitOverH" ||
             (*masterBuffer)->getData()->get<std::string>("splitStrategy") == "SplitOverHOverlapped") )
        {
            numericStrides = (*masterBuffer)->getData()->computeNumericStrides();
            numericStrides.push_back(subtensor.getDType().getSizeInBits() / 8);
        }
    }

    //Because according to graphfile order is given as NCHW, which is exactly the reverse of our shape assumption WHCN
    std::reverse(dimensions.begin(), dimensions.end());
    std::reverse(numericStrides.begin(), numericStrides.end());

    toBuild->dimensions = dimensions;
    toBuild->strides = numericStrides;

    toBuild->data = std::unique_ptr<MVCNN::IndirectDataReferenceT>(new MVCNN::IndirectDataReferenceT());
    if (*tensorAllocatorName == "GraphFile")
    {
        if(!t->isSparse())
        {

            // SOK non-sparse weights are serialised individually so that they can be compressed by the HDE
            // Weight tables and sparsity maps are not compressed
            if(t->get<std::string>("splitStrategy") == "SplitOverK" && !t->hasAttr("weightTable") && !t->hasAttr("sparsityMap")
               && !t->hasAttr("dilatedSubConvSM") && !t->hasAttr("dilatedSubConvSE")
               && !t->hasAttr("interpNNSM") && !t->hasAttr("interpNNSE"))
            {
                unsigned graphfileIndex = subtensor.get<unsigned>("graphFileIndex");
                toBuild->locale_index = std::vector<unsigned int>(1);
                toBuild->locale_index[0] = graphfileIndex;
                toBuild->data->data_index = 0;
            }
            else
            {
                unsigned graphfileIndex = t->get<unsigned>("graphFileIndex");
                toBuild->locale_index = std::vector<unsigned int>(1);
                toBuild->locale_index[0] = graphfileIndex;

                auto offset = subtensor.get<std::vector<std::size_t>>("offset");
                auto index = t->getOrder().subToInd(t->getShape(), offset);
                auto byte_index = index * t->getDType().getSizeInBits() / 8;
                toBuild->data->data_index = byte_index;
            }
        }
        else //Sparse
        {
            // In case data is sparse, packed subtensors are serialiazed. This simplifies our life a lot.
            // No data index to be provided, just have to take the graphfile index from the subtensor
            // Empty tensors will not have graphFileIndex because they will not be saved in the blob

            unsigned graphfileIndex = subtensor.hasAttr("graphFileIndex")? subtensor.get<unsigned>("graphFileIndex"): 0;
            toBuild->locale_index = std::vector<unsigned int>(1);
            toBuild->locale_index[0] = graphfileIndex;

            toBuild->data->data_index = 0;
        }
    }
    else if(*tensorAllocatorName == "ProgrammableInput" || *tensorAllocatorName == "ProgrammableOutput")
    {
        auto offset = subtensor.get<std::vector<std::size_t>>("offset");

        auto masterBuffer = tensorAllocator.getTopMasterBuffer(tensorBufferIt);
        auto index = (*masterBuffer)->getData()->getOrder().subToInd((*masterBuffer)->getData()->getShape(), offset);
        auto byte_index = index * t->getDType().getSizeInBits() / 8;
        toBuild->data->data_index = byte_index;
        auto strides = tensorBufferIt->getStrides();

        auto leading_offset = strides[0];
        toBuild->locale_index = std::vector<unsigned int>(1,0);


        if ((*masterBuffer)->getData()->hasAttr("inputIndex"))
            toBuild->locale_index[0] = (*masterBuffer)->getData()->get<uint8_t>("inputIndex");
        else if ((*masterBuffer)->getData()->hasAttr("outputIndex"))
            toBuild->locale_index[0] = (*masterBuffer)->getData()->get<uint8_t>("outputIndex");

        if (leading_offset)
            toBuild->data->data_index += leading_offset;

    }
    else if(*tensorAllocatorName == "VPU_DDR_BSS" || *tensorAllocatorName == "VPU_DDR_Heap")
    {
        unsigned byte_index;

        if (subtensor.hasAttr("offset_byte_index"))
        {
          byte_index = subtensor.get<unsigned>("offset_byte_index");
        }
        else
        {
          auto offset = subtensor.get<std::vector<std::size_t>>("offset");
          auto index = t->getOrder().subToInd(t->getShape(), offset);
          byte_index = index * t->getDType().getSizeInBits() / 8;
          auto tensorStrides = t->computeNumericStrides();
          tensorStrides.push_back(t->getDType().getSizeInBits() / 8);
          std::reverse(tensorStrides.begin(), tensorStrides.end());
          //NOTE: the division is going to extinguish the data type offset!!!
          if(numericStrides[4] != tensorStrides[4])
              byte_index = index * (double(numericStrides[4])/double(tensorStrides[4])) * t->getDType().getSizeInBits() / 8;

        // If SOH subtensors have one size but master buffer has different dims in C or W
        // need to multiply to find correct index in DDR
        if( t->get<std::string>("splitStrategy") == "SplitOverH" &&
            numericStrides[3] != tensorStrides[3]) // W
            byte_index = index * (double(numericStrides[3])/double(tensorStrides[3])) * t->getDType().getSizeInBits() / 8;

        if( t->get<std::string>("splitStrategy") == "SplitOverH" &&
            numericStrides[2] != tensorStrides[2]) // C
            byte_index = index * (double(numericStrides[2])/double(tensorStrides[2])) * t->getDType().getSizeInBits() / 8;

        }

        auto masterBuffer = tensorAllocator.getTopMasterBuffer(tensorBufferIt);

        // NOTE: This probably has to be done also when DDR kicks in
        // as CMX is the only memory with the cluster/slice approach
        auto starting_address = 0;

        if(t->hasAttr("address"))
            starting_address = t->get<std::size_t>("address");
        else
        {
            starting_address = (*masterBuffer)->getOffset();
        }

        if(subtensor.hasAttr("fused_concat_offset"))
            toBuild->data->data_index = starting_address + subtensor.get<std::size_t>("fused_concat_offset");
        else
            toBuild->data->data_index = starting_address + byte_index;

        auto strides = tensorBufferIt->getStrides();
        auto leading_offset = strides[0];
        toBuild->locale_index = std::vector<unsigned int>(1,0);
        if (leading_offset && !t->hasAttr("fusedConcatReshape"))
            toBuild->data->data_index += leading_offset;

    }
    else
    {

        // This part is for concat
        if(t->hasAttr("address"))
        {
            if (t->hasAttr("splitStrategy") &&
                    t->get<std::string>("splitStrategy") == "ClusteringAndSOH" && !isOutput)
            {
                auto firstSetSubTensor = t->getSubTensor(clusterId - numTask);
                toBuild->data->data_index = firstSetSubTensor.getAddress() +
                        (clusterId - numTask) * t->getSubTensor(clusterId).getShape()[mv::IO_CHANNEL_DIMENSION] *
                        t->getSubTensor(clusterId).getShape()[mv::IO_WIDTH_DIMENSION] *
                        t->getSubTensor(clusterId).getShape()[mv::IO_HEIGHT_DIMENSION] *
                        t->getDType().getSizeInBits() / 8;
            }
            else
                toBuild->data->data_index = subtensor.getAddress();
        }
        else if (t->hasAttr("cmx_concat_buffer")) {
          size_t net_address = tensorBufferIt->getOffset();
          if (subtensor.hasAttr("offset")) {
              auto offset = subtensor.get<std::vector<std::size_t>>("offset");
              auto index = t->getOrder().subToInd(t->getShape(), offset);
              size_t byte_index = index * t->getDType().getSizeInBits() / 8;
              auto tensorStrides = t->computeNumericStrides();
              tensorStrides.push_back(t->getDType().getSizeInBits() / 8);
              std::reverse(tensorStrides.begin(), tensorStrides.end());

              if(numericStrides[4] != tensorStrides[4]){
                  byte_index = index * (double(numericStrides[4])/double(tensorStrides[4]));
              }
              net_address += byte_index;
          }
          toBuild->data->data_index = net_address;
        }
        else
            toBuild->data->data_index = tensorBufferIt->getOffset();

        auto strides = tensorBufferIt->getStrides();
        auto leading_offset = strides[0];
        toBuild->data->data_index += leading_offset;

        if (t->hasAttr("splitStrategy") &&
                t->get<std::string>("splitStrategy") == "ClusteringAndSOH" && !isOutput)
            toBuild->locale_index = std::vector<unsigned int>(1, clusterId - numTask);
        else
            toBuild->locale_index = std::vector<unsigned int>(1, clusterId);

        if(t->isSparse())
        {
            toBuild->data->sparsity_index = subtensor.getSparsityMap()->getAddress();
            if(!t->isPopulated())
                toBuild->data->storage_element_index = subtensor.getStorageElement()->getAddress();
            else
                toBuild->data->storage_element_index = 0;
        }
    }

    toBuild->locale = convertAllocatorToMemoryLocale(*tensorAllocatorName, tensorLocation);
    toBuild->data_dtype = convertDtype(t->getDType());

    if (t->hasAttr("base_ptrs"))
        toBuild->base_ptrs = t->get<std::vector<unsigned short>>("base_ptrs");

    // could also be t->hasAttr("quantizationParameters")
    // but in my opinion quantization for a tensor of floats makes very little sense
    // leaving this comment here for future generations
    if(t->isQuantized())
    {
        auto quantizationParams = t->get<mv::QuantizationParams>("quantParams");

        // acording to the runtime code, Zero point only uses first value of array.
        // mult and shift are only used for eltwise output, not for other outputs.
        // https://github.com/movidius/vpuip_2/blob/develop/system/nn/nce_lib/src/2490/ppe_task.cpp
        auto quantZero = quantizationParams.getZeroPoint();
        toBuild->quant_zero = std::vector<unsigned char>(1, quantZero[0]);

        std::vector<unsigned> quantMult = {};
        if (quantizationParams.hasAttr("mult"))
            quantMult = quantizationParams.getMult();
        quantMult = reduceQuantVector_(quantMult);
        toBuild->quant_mult = std::vector<unsigned short int>(1, quantMult[0]);

        std::vector<unsigned> quantShift;
        if (quantizationParams.hasAttr("shift"))
            quantShift = quantizationParams.getShift();
        quantShift = reduceQuantVector_(quantShift);
        toBuild->quant_shift = std::vector<unsigned char>(1, quantShift[0]);
        toBuild->quant_post_shift_right = quantizationParams.getPostShift();
    }

    return toBuild;
}

std::unique_ptr<MVCNN::SummaryHeaderT> mv::RuntimeModel::buildSummaryHeaderMetaInformations(ComputationModel& cm, mv::Element& compilationDescriptor)
{
    mv::OpModel om(cm);

    std::unique_ptr<MVCNN::SummaryHeaderT> toBuild = std::unique_ptr<MVCNN::SummaryHeaderT>(new MVCNN::SummaryHeaderT());

    toBuild->version = buildVersionT();
    toBuild->original_structure = buildSourceStructureT(cm, compilationDescriptor);
    toBuild->layer_count = om.opsCount();

    return toBuild;
}


std::unique_ptr<MVCNN::SummaryHeaderT> mv::RuntimeModel::buildSummaryHeaderT(ComputationModel& cm, const mv::TargetDescriptor& td, mv::Element& compilationDescriptor, std::unique_ptr<MVCNN::SummaryHeaderT> originalHeader)
{
    mv::OpModel om(cm);

    std::unique_ptr<MVCNN::SummaryHeaderT> toBuild = std::unique_ptr<MVCNN::SummaryHeaderT>(new MVCNN::SummaryHeaderT());

    auto globalConfigurationParameters = cm.getGlobalConfigParams();
    auto paddOutput = globalConfigurationParameters->hasAttr("PadOutput") ? globalConfigurationParameters->get<bool>("PadOutput") : false;

    toBuild->version = std::move(originalHeader->version);
    toBuild->original_structure = std::move(originalHeader->original_structure);
    toBuild->resources = buildResourcesT(cm, td, compilationDescriptor);
    toBuild->identifier = cm.getName();
    toBuild->device = mapTargetDevice(td.getTarget());
    toBuild->device_revision = mapTargetDeviceRevision(globalConfigurationParameters);

    // Support multiple inputs
    auto numInputs = om.getNumNetworkInputs();
    if (numInputs == 1)
    {
        auto inputIt = om.getInput()->getOutputTensor(0);
        // Rename input tensor - same as input operation name
        inputIt->setName(om.getInput()->getName());
        toBuild->net_input = std::vector<std::unique_ptr<MVCNN::TensorReferenceT>>(1);
        toBuild->net_input[0] = buildTensorReferenceT(cm, compilationDescriptor, om.getInput()->getOutputTensor(0));
        cm.bufferMap().addInput(inputIt->getName(), inputIt->getOrder(), inputIt->getShape(), inputIt->getDType());
    }
    else
    {
        auto implicitInputOps = om.getNetworkInputs();
        toBuild->net_input = std::vector<std::unique_ptr<MVCNN::TensorReferenceT>>(implicitInputOps.size());
        for (size_t i = 0; i < implicitInputOps.size(); i++)
        {
            toBuild->net_input[i] = buildTensorReferenceT(cm, compilationDescriptor, implicitInputOps[i]->getOutputTensor(0));
        }
    }

    auto numOutputs = om.getNumNetworkOutputs();

    if (numOutputs == 1)
    {
        toBuild->net_output = std::vector<std::unique_ptr<MVCNN::TensorReferenceT>>(1);
        auto outputIt = om.getOutput()->getInputTensor(0);
        // Rename output tensor - same as output operation name
        outputIt->setName(om.getOutput()->getName());
        toBuild->net_output[0] = buildTensorReferenceT(cm, compilationDescriptor, outputIt);
        cm.bufferMap().addOutput(outputIt->getName(), outputIt->getOrder(), outputIt->getShape(), outputIt->getDType());
    }
    else
    {
        const auto implicitOutputOps = om.getNetworkOutputs();
        toBuild->net_output = std::vector<std::unique_ptr<MVCNN::TensorReferenceT>>(implicitOutputOps.size());
        for (size_t i = 0; i < implicitOutputOps.size(); i++)
        {
            auto destOp = implicitOutputOps[i];
            auto outputIt = destOp->getInputTensor(0);
            // Rename output tensor - same as output operation name
            if (destOp->hasAttr("networkOutputName")) {
                outputIt->setName(destOp->get("networkOutputName"));
            } else outputIt->setName(destOp->getName());
            toBuild->net_output[i] = buildTensorReferenceT(cm, compilationDescriptor, outputIt);
            cm.bufferMap().addOutput(outputIt->getName(), outputIt->getOrder(), outputIt->getShape(), outputIt->getDType());
        }
    }

    if (om.getNumProfilingOutputs())
    {
        const auto implicitProfilingOps = om.getProfilingOutputs();
        toBuild->profiling_output = std::vector<std::unique_ptr<MVCNN::TensorReferenceT>>(implicitProfilingOps.size());
        for (size_t i = 0; i < implicitProfilingOps.size(); i++)
        {
            auto destOp = implicitProfilingOps[i];
            auto outputIt = destOp->getInputTensor(0);
            outputIt->setName(destOp->getName());
            toBuild->profiling_output[i] = buildTensorReferenceT(cm, compilationDescriptor, outputIt);
            cm.bufferMap().addOutput(outputIt->getName(), outputIt->getOrder(), outputIt->getShape(), outputIt->getDType());
        }
    }

    if (paddOutput && om.getOutput()->getInputTensor(0)->hasAttr("alignment"))
        alignTensor(cm, toBuild->net_output[0], *om.getOutput()->getInputTensor(0), IO_CHANNEL_DIMENSION, paddOutput);
    auto taskCount = [](mv::OpModel & m)
    {
        unsigned i = 0;
        for(auto opIt = m.opBegin(); opIt != m.opEnd(); ++opIt)
        {
            if (opIt->hasAttr("toIgnore") && opIt->get<bool>("toIgnore"))
                continue;
            else
            {
                if(opIt->getOpType().find("Task") != std::string::npos)
                    ++i;
            }
        }
        return i;
    };

    toBuild->options = std::vector<MVCNN::ExecutionFlag>();
    if(!targetEmulator_(compilationDescriptor) && !globalConfigurationParameters->get<bool>("enableStaticBarriers"))
      toBuild->options.push_back(MVCNN::ExecutionFlag_DynamicBarriers);

    toBuild->layer_count = originalHeader->layer_count;
    toBuild->task_count = taskCount(om);

    return toBuild;
}

std::unique_ptr<MVCNN::VersionT> mv::RuntimeModel::buildVersionT()
{
    std::unique_ptr<MVCNN::VersionT> toBuild = std::unique_ptr<MVCNN::VersionT>(new MVCNN::VersionT());

#if defined(MVCNN_VERSION_MAJOR) && defined(MVCNN_VERSION_MINOR) && defined(MVCNN_VERSION_PATCH)
    toBuild->majorV = MVCNN_VERSION_MAJOR;
    toBuild->minorV = MVCNN_VERSION_MINOR;
    toBuild->patchV = MVCNN_VERSION_PATCH;
#endif

    toBuild->hash = vpux::VPUX_PLUGIN_VERSION;
    toBuild->context = "MCM Compiler";

    std::stringstream version_log;
    version_log << "Blob version: majorV= " << toBuild->majorV << ", minorV= "
                << toBuild->minorV << ", patch= " << toBuild->patchV
                << ", hash= " << toBuild->hash << ", context= " << toBuild->context;
    Logger::log(Logger::MessageType::Info, "HeadMetaInfo", version_log.str());

    return toBuild;
}

MVCNN::PhysicalProcessor mapProcessorName(const string& processorName)
{
    if (processorName == "LeonRT")
        return MVCNN::PhysicalProcessor_LEON_RT;
    if (processorName == "LeonNN")
        return MVCNN::PhysicalProcessor_LEON_NN;
    if (processorName == "UPAShaves")
        return MVCNN::PhysicalProcessor_UPA_SHV;
    if (processorName == "NNShaves")
        return MVCNN::PhysicalProcessor_NN_SHV;
    if (processorName == "ARM")
        return MVCNN::PhysicalProcessor_ARM;
    return MVCNN::PhysicalProcessor_NULL;
}

std::unique_ptr<MVCNN::ResourcesT> mv::RuntimeModel::buildResourcesT(ComputationModel& cm, const mv::TargetDescriptor& td, mv::Element&)

{
    std::unique_ptr<MVCNN::ResourcesT> toBuild = std::unique_ptr<MVCNN::ResourcesT>(new MVCNN::ResourcesT());
    auto globalConfigurationParams = cm.getGlobalConfigParams();

    toBuild->processor_allocation = std::vector<std::unique_ptr<MVCNN::ProcessorMappingT>>();

    auto processDefs = td.processorDefs();
    for (const auto &proc : processDefs)
    {
        std::unique_ptr<MVCNN::ProcessorMappingT> processorEntry =
             std::unique_ptr<MVCNN::ProcessorMappingT>(new MVCNN::ProcessorMappingT());
        processorEntry->item = mapProcessorName(proc.first);
        processorEntry->number = proc.second;
        if (processorEntry->item != MVCNN::PhysicalProcessor_NULL)
            toBuild->processor_allocation.push_back(std::move(processorEntry));
     }

     if(globalConfigurationParams->hasAttr("Number_of_DPUs")){
        std::unique_ptr<MVCNN::ProcessorMappingT> NNDPUProcessor =
            std::unique_ptr<MVCNN::ProcessorMappingT>(new MVCNN::ProcessorMappingT());
        NNDPUProcessor->item= MVCNN::PhysicalProcessor_NCE_PerClusterDPU;
        setIfPresent<double, int>(NNDPUProcessor->number, *globalConfigurationParams , "Number_of_DPUs");
        if(globalConfigurationParams->hasAttr("Number_of_Clusters")){
            int clusterNumber = globalConfigurationParams->get<int>("Number_of_Clusters");
            NNDPUProcessor->number = NNDPUProcessor->number / (double)clusterNumber;
        }
        toBuild->processor_allocation.push_back(std::move(NNDPUProcessor));
    }

    //clusters
    std::unique_ptr<MVCNN::ProcessorMappingT> NNClusterProcessor =
        std::unique_ptr<MVCNN::ProcessorMappingT>(new MVCNN::ProcessorMappingT());
    NNClusterProcessor->item= MVCNN::PhysicalProcessor_NCE_Cluster ;
    setIfPresent<double, int>(NNClusterProcessor->number, *globalConfigurationParams , "Number_of_Clusters");
    toBuild->processor_allocation.push_back(std::move(NNClusterProcessor));

    toBuild->memory_sizes = std::vector<std::unique_ptr<MVCNN::MemoryMappingT>>();
    if(globalConfigurationParams->hasAttr("cmx")){
        std::unique_ptr<MVCNN::MemoryMappingT> cmxMemorySize =
            std::unique_ptr<MVCNN::MemoryMappingT>(new MVCNN::MemoryMappingT());
        cmxMemorySize->item= MVCNN::PhysicalMem_NN_CMX;
        setIfPresent<double, int>(cmxMemorySize->number, *globalConfigurationParams , "cmx");
        toBuild->memory_sizes.push_back(std::move(cmxMemorySize));
    }
    if(globalConfigurationParams->hasAttr("DDRScratch")){
        std::unique_ptr<MVCNN::MemoryMappingT> DDRMemorySize =
            std::unique_ptr<MVCNN::MemoryMappingT>(new MVCNN::MemoryMappingT());
        DDRMemorySize->item= MVCNN::PhysicalMem_DDR;
        // Set DDR scratch value to high watermark, which is saved in BufferMap
        // Actually globalConfigParams in cm is also reset by high watermark so they're the same
        DDRMemorySize->number= (cm.bufferMap().getScratch()) ? cm.bufferMap().getScratch()->getSize() : 0;
        toBuild->memory_sizes.push_back(std::move(DDRMemorySize));
    }

    toBuild->memory_bandwidth = std::vector<std::unique_ptr<MVCNN::MemoryRelationshipMappingT>>();
    if(globalConfigurationParams->hasAttr("memoryBandwidth")){
        std::unique_ptr<MVCNN::MemoryRelationshipMappingT> ddrToCMX =
            std::unique_ptr<MVCNN::MemoryRelationshipMappingT>(new MVCNN::MemoryRelationshipMappingT());
        ddrToCMX->from_item= MVCNN::PhysicalMem_DDR;
        ddrToCMX->to_item= MVCNN::PhysicalMem_NN_CMX;
        setIfPresent<double, int>(ddrToCMX->number, *globalConfigurationParams , "memoryBandwidth");
        toBuild->memory_bandwidth.push_back(std::move(ddrToCMX));

        std::unique_ptr<MVCNN::MemoryRelationshipMappingT> cmxToDDR =
            std::unique_ptr<MVCNN::MemoryRelationshipMappingT>(new MVCNN::MemoryRelationshipMappingT());
        cmxToDDR->from_item= MVCNN::PhysicalMem_NN_CMX;
        cmxToDDR->to_item= MVCNN::PhysicalMem_DDR;
        setIfPresent<double, int>(cmxToDDR->number, *globalConfigurationParams , "memoryBandwidth");
        toBuild->memory_bandwidth.push_back(std::move(cmxToDDR));
    }

    toBuild->processor_frequencies = std::vector<std::unique_ptr<MVCNN::ProcessorMappingT>>();
    if(globalConfigurationParams->hasAttr("systemClockMhz")){
        std::unique_ptr<MVCNN::ProcessorMappingT> dpuFreq =
            std::unique_ptr<MVCNN::ProcessorMappingT>(new MVCNN::ProcessorMappingT());
        dpuFreq->item= MVCNN::PhysicalProcessor_NCE_Cluster;
        setIfPresent<double, int>(dpuFreq->number, *globalConfigurationParams , "systemClockMhz");
        toBuild->processor_frequencies.push_back(std::move(dpuFreq));
    }

    return toBuild;
}

template <typename T>
std::vector<uint64_t> packToInt64(const std::vector<T>& origData, mv::DType dtype)
{
    unsigned dataSize = origData.size();
    unsigned origDataSize = dtype.getSizeInBits();

    unsigned nElementToPack = 64 / origDataSize;
    unsigned finalLength = mv::ceil_division(dataSize , nElementToPack);

    std::vector<uint64_t> toReturn(finalLength, 0);

    for(unsigned i = 0; i < finalLength; ++i)
        for(unsigned j = 0; j < nElementToPack; ++j)
            if ((i*nElementToPack + j) < dataSize) {
                // When the elements packed in the less significant part ot he long unsingned int value
                // are negative, the sign extension gets copied over the more significant parts. As a result,
                // when the next element gets packed it wil have its bits flipped due to XOR.
                // That's why the mask is needed.
                const uint64_t mask = (1ULL << origDataSize) - 1;
                toReturn[i] ^= (origData[i*nElementToPack + j] & mask) << (j * origDataSize);
            }

    return toReturn;
}

std::unique_ptr<MVCNN::BinaryDataT> mv::RuntimeModel::buildBinaryDataT(ComputationModel&, mv::Element&, mv::Tensor& t, bool compression, bool csramCacheable)
{
    std::unique_ptr<MVCNN::BinaryDataT> toBuild = std::unique_ptr<MVCNN::BinaryDataT>(new MVCNN::BinaryDataT());

    /* Here we use the HDE to compress weights
     * We do not compress sparsity maps or fake sparsity maps yet
     * These should be comprssed for additional performance
    */

    if(compression && t.isPopulatedTensor() && t.getDType() != mv::DType("Float16"))
    {
        auto weightSizeKb = t.computeTotalSize() / 1024;

        //Minimum size that can be compressed is 4kB
        if(weightSizeKb > 4) {
            auto dataPacked = t.getDataPacked();
            auto compressedData = codec_->compress(dataPacked, t);
            toBuild->data = packToInt64(compressedData.first, t.getDType());

            //sometimes even if the tensor is > 4KB it might not be compressable
            if(t.hasAttr("CompressedSize"))
                toBuild->length = t.get<int>("CompressedSize");
            else
                toBuild->length = dataPacked.size() * t.getDType().getSizeInBits() / 8;
            toBuild->underlying_type = MVCNN::DType::DType_U8;
        }
        else {
            auto dataPacked = t.getDataPacked();
            toBuild->data = packToInt64(dataPacked, t.getDType());
            toBuild->length = dataPacked.size() * t.getDType().getSizeInBits() / 8;
            toBuild->underlying_type = MVCNN::DType::DType_U8;
            t.set<bool>("Compression", false);
        }
    }
    else if (t.getDType() == mv::DType("Float16") && !t.isSparse())
    {
        if (t.hasAttr("quantParams"))
        {
            const auto& quantParams = t.get<mv::QuantizationParams>("quantParams");
            if (!quantParams.isEmpty() && !quantParams.isNeutral()) {
                throw std::runtime_error("Bad quantParams for Float16 Binary Data");
            }
        }

        auto dataPacked = t.getIntData();
        toBuild->data = packToInt64(dataPacked, t.getDType());
        toBuild->length = dataPacked.size() * t.getDType().getSizeInBits() / 8;
        toBuild->underlying_type = MVCNN::DType::DType_U8;
        t.set<bool>("Compression", false);
    }
    else
    {
        auto dataPacked = t.getDataPacked();
        toBuild->data = packToInt64(dataPacked, t.getDType());
        toBuild->length = dataPacked.size() * t.getDType().getSizeInBits() / 8;
        toBuild->underlying_type = MVCNN::DType::DType_U8;
        t.set<bool>("Compression", false);
    }

    toBuild->csram_cacheable = csramCacheable;

    return toBuild;
}

// We have three taskslist for POC:
// Tasklist 0: Contains all the tasks
// We need to topologically sort the control model graph to get the tasks in the correct order.

std::vector<std::unique_ptr<MVCNN::TaskListT>> mv::RuntimeModel::buildTaskListT(ComputationModel& cm, mv::Element& compilationDescriptor)
{
    mv::OpModel om(cm);
    mv::ControlModel controlModel(cm);
    std::vector<std::unique_ptr<MVCNN::TaskListT>> toBuild = std::vector<std::unique_ptr<MVCNN::TaskListT>>(3);
    toBuild[0] = std::unique_ptr<MVCNN::TaskListT>(new MVCNN::TaskListT());
    toBuild[1] = std::unique_ptr<MVCNN::TaskListT>(new MVCNN::TaskListT());
    toBuild[2] = std::unique_ptr<MVCNN::TaskListT>(new MVCNN::TaskListT());

    // DMA re-ordering has been disabled, reverting to original schedule

    std::vector<Control::OpListIterator> sortedOps;
    if (targetEmulator_(compilationDescriptor))
    {
        const auto ops = om.topologicalSort();
        for (const auto& op : ops){
            if (op->getOpType() != "ImplicitOutput" &&
                op->getOpType() != "ImplicitInput" &&
                op->getOpType() != "ImplicitInputSlice" &&
                op->getOpType() != "ImplicitUnion" &&
                op->getOpType() != "Input" &&
                op->getOpType() != "Output")
            {
                sortedOps.push_back(controlModel.switchContext(op));
            }
        }
    }
    else
        sortedOps = controlModel.schedulingSort();

    int initialId = 0;
    uint8_t port = 0;

    for(auto vecIt = sortedOps.begin(); vecIt != sortedOps.end(); ++vecIt)
    {
        auto opIt = *vecIt;
        if (opIt->hasAttr("toIgnore") && opIt->get<bool>("toIgnore"))
            continue;
        std::unique_ptr<MVCNN::TaskListT> * listToUse = &toBuild[0]; // default to DPU task
        std::string opType = opIt->getOpType();
        if(opType.find("DPU") != std::string::npos)
            listToUse = &toBuild[0];
        if(opType.find("UPA") != std::string::npos)
            listToUse = &toBuild[0];
        else if(opType.find("DMA") != std::string::npos)
            listToUse = &toBuild[1];
        auto tasks = buildTaskT(cm, compilationDescriptor, opIt, &port);
        for(auto& task: tasks)
            (*listToUse)->content.push_back(std::move(task));
    }

    //Barrier task list has to be built in the correct order
    auto barrierTasks = om.getOps("BarrierTask");
    std::sort(
        barrierTasks.begin(),
        barrierTasks.end(),
        [](const mv::Data::OpListIterator& a, const mv::Data::OpListIterator& b) -> bool { return a->get<mv::Barrier>("Barrier").getIndex() < b->get<mv::Barrier>("Barrier").getIndex(); }
        );

    unsigned n = barrierTasks.size();
    for(unsigned i = 0; i < n; ++i)
    {
        auto tasks = buildTaskT(cm, compilationDescriptor, controlModel.switchContext(barrierTasks[i]), &port);
        for(auto& task: tasks)
            toBuild[2]->content.push_back(std::move(task));
    }

    // Filling node IDs
    for(auto& serialTask: toBuild[0]->content)
        serialTask->nodeID = initialId++;
    for(auto& serialTask: toBuild[1]->content)
        serialTask->nodeID = initialId++;
    for(auto& serialTask: toBuild[2]->content)
        serialTask->nodeID = initialId++;

    return toBuild;
}

std::vector<std::unique_ptr<MVCNN::TaskT>> mv::RuntimeModel::buildBarrierTaskT(ComputationModel& cm, Element& compilationDescriptor, Control::OpListIterator opIt)
{
    std::vector<std::unique_ptr<MVCNN::TaskT>> toReturn = std::vector<std::unique_ptr<MVCNN::TaskT>>(1);

    toReturn[0] = std::unique_ptr<MVCNN::TaskT>(new MVCNN::TaskT());
    toReturn[0]->task.type = MVCNN::SpecificTask_ControllerTask;

    auto controllerTask = new MVCNN::ControllerTaskT();
    controllerTask->task.type = MVCNN::ControllerSubTask_BarrierConfigurationTask;

    auto barrierConfigurationTask = new MVCNN::BarrierConfigurationTaskT();
    barrierConfigurationTask->target = buildBarrierT(cm, compilationDescriptor, opIt);

    controllerTask->task.value = barrierConfigurationTask;
    toReturn[0]->task.value = controllerTask;

    return toReturn;
}

std::vector<std::unique_ptr<MVCNN::TaskT>> mv::RuntimeModel::buildSpecificTaskUnion(ComputationModel& cm, mv::Element& compilationDescriptor, Control::OpListIterator& opIt, uint8_t* port)
{
    std::vector<std::unique_ptr<MVCNN::TaskT>> toBuild = std::vector<std::unique_ptr<MVCNN::TaskT>>();
    std::string taskType(opIt->getOpType());
    if ((targetEmulator_(compilationDescriptor)) && (taskType != "UPATask") && (taskType != "DPUTask")  && !(opIt->isImplicit()) && (taskType != "Concat"))
        return toBuild;
    if(taskType == "MvTensorTask")
        toBuild = buildMvTensorTaskT(cm, compilationDescriptor, opIt);
    else if(taskType == "DMATask")
    {
        auto direction = opIt->get<mv::DmaDirection>("direction");
        if (direction == mv::DmaDirectionEnum::HW2DDR)
        {
            toBuild = buildHWDMATaskT(cm, compilationDescriptor, opIt);
        }
        else if(direction == mv::DmaDirectionEnum::NNCMX2UPACMX ||
           direction == mv::DmaDirectionEnum::UPACMX2NNCMX ||
           direction == mv::DmaDirectionEnum::DDR2UPACMX   ||
           direction == mv::DmaDirectionEnum::UPACMX2DDR)
        {
            toBuild = buildUPADMATaskT(cm, compilationDescriptor, opIt);
        }
        else
        {
            std::string splitting = opIt->getOutputTensor(0)->get<std::string>("splitStrategy");
            toBuild = buildNNDMATaskT(cm, compilationDescriptor, opIt, splitting, port);
        }
    }
    else if(taskType == "NCE1Task")
        toBuild = buildNCE1TaskT(cm, compilationDescriptor, opIt);
    else if(taskType == "DPUTask")
    {
        const std::string splitting =
            (!targetEmulator_(compilationDescriptor)) ? opIt->get<std::string>("splitStrategy") : std::string();
        toBuild = buildNCE2TaskT(cm, compilationDescriptor, opIt, splitting);
    }
    else if(taskType == "UPATask")
        toBuild = buildUPATask(cm, compilationDescriptor, opIt);
    else if(taskType == "ControllerTask")
        toBuild = buildControllerTaskT(cm, compilationDescriptor, opIt);
    else if(taskType == "BarrierTask")
        toBuild = buildBarrierTaskT(cm, compilationDescriptor, opIt);
    else if((opIt->isImplicit() || (opIt->getOpType() == "Concat")) && targetEmulator_(compilationDescriptor))
        toBuild = buildImplicitTask(cm, compilationDescriptor, opIt);
    return toBuild;
}

std::vector<std::unique_ptr<MVCNN::TaskT>> mv::RuntimeModel::buildMvTensorTaskT(ComputationModel& /*cm*/, mv::Element& /*compilationDescriptor*/, Control::OpListIterator /*opIt*/)
{
    std::vector<std::unique_ptr<MVCNN::TaskT>> toReturn = std::vector<std::unique_ptr<MVCNN::TaskT>>(1);
    return toReturn;
}

std::vector<std::unique_ptr<MVCNN::TaskT>> mv::RuntimeModel::buildUPADMATaskT(ComputationModel& cm, mv::Element &compilationDescriptor, Control::OpListIterator opIt)
{
    std::vector<std::unique_ptr<MVCNN::TaskT>> toReturn = std::vector<std::unique_ptr<MVCNN::TaskT>>(1);
    toReturn[0] = std::unique_ptr<MVCNN::TaskT>(new MVCNN::TaskT());
    toReturn[0]->task.type = MVCNN::SpecificTask_UPADMATask;
    auto tmp = new MVCNN::UPADMATaskT();
    tmp->src = buildTensorReferenceT(cm, compilationDescriptor, opIt->getInputTensor(0));
    tmp->dst = buildTensorReferenceT(cm, compilationDescriptor, opIt->getOutputTensor(0));
    toReturn[0]->task.value = tmp;
    return toReturn;
}

bool checkUnstridedDMA(mv::Data::TensorIterator src, int i, MVCNN::NNDMATaskT * tmp)
{
    // Note: Zero-padding populated tensor used for large kernel support needs to keep existing dst strides
    auto skip_unstriding_dst = false;
    if(src->hasAttr("is_pad") && src->get<bool>("is_pad"))
        skip_unstriding_dst = true;

    if(tmp->src->locale == MVCNN::MemoryLocation_GraphFile)
    {
        unsigned totalSize = src->getSubTensor(i).getShape().totalSize();
        unsigned totalSizeDst = src->getSubTensor(i).getShape().totalSize();

        if(src->isSparse())
        {
            totalSize = src->getSubTensor(i).dataPackedSize();
            totalSizeDst = src->getSubTensor(i).dataPackedSize();
            // DMAs associated to empty Tensors will be optimized out
            if (totalSize == 0)
                return false;
        }

        if(src->getSubTensor(i).hasAttr("CompressedSize"))
            totalSize = src->getSubTensor(i).get<int>("CompressedSize");
        else
            totalSize *= src->getDType().getSizeInBits() / 8;

        std::vector<uint32_t> dimensions = {totalSize, 1, 1, 1};
        totalSizeDst *= src->getDType().getSizeInBits() / 8;
        std::vector<uint32_t> dimensionsdst = {totalSizeDst, 1, 1, 1};
        std::vector<float> strides = {1, 1, 1, 1, 1};
        auto dtype = MVCNN::DType::DType_U8;

        tmp->src->dimensions = dimensions;
        tmp->src->strides = strides;
        tmp->src->data_dtype = dtype;

        if(skip_unstriding_dst)
            return true;

        tmp->dst->dimensions = dimensionsdst;
        tmp->dst->strides = strides;
        tmp->dst->data_dtype = dtype;

        return true;
    }
    return true;
}

void mv::RuntimeModel::case1MC(unsigned numTasks, mv::ComputationModel& cm, mv::DmaDirection direction, mv::Element &compilationDescriptor,
                               bool padFinalOutput, bool dmaToDma, std::vector<std::unique_ptr<MVCNN::TaskT>>& toReturn, mv::Data::TensorIterator src, mv::Data::TensorIterator dst, uint8_t* port, int portLimit, const std::string& srcAllocator, const std::string& dstAllocator)
{
    std::unique_ptr<MVCNN::TaskT> toPush = std::unique_ptr<MVCNN::TaskT>(new MVCNN::TaskT());
    std::unique_ptr<MVCNN::NNDMATaskT> tmp = std::unique_ptr<MVCNN::NNDMATaskT>(new MVCNN::NNDMATaskT());
    toPush->task.type = MVCNN::SpecificTask_NNDMATask;

    tmp->src = buildTensorReferenceT(cm, compilationDescriptor, src, srcAllocator);
    tmp->dst = buildTensorReferenceT(cm, compilationDescriptor, dst, dstAllocator);

    tmp->port = (*port)++;
    if (portLimit <= *port)
    {
        *port = 0;
    }

    if(dmaToDma)
    {
        tmp->src->dimensions = tmp->dst->dimensions;
    }
    if (direction != mv::DDR2NNCMX && direction != mv::CSRAM2NNCMX)
    {
        if (padFinalOutput && dst->hasAttr("alignment"))
            alignTensor(cm, tmp->dst, *dst, IO_CHANNEL_DIMENSION, padFinalOutput);
    }

    std::vector<unsigned int> locale_index;
    for (unsigned idx = numTasks; idx > 0; idx--)
        locale_index.push_back(idx - 1);

    if(direction == mv::DDR2NNCMX || direction == mv::CSRAM2NNCMX)
        tmp->dst->locale_index = locale_index;

    // Passing -1 as subtensor index, will have us get the full tensor
    if (!checkUnstridedDMA(src, -1, tmp.get()))
    {
        return;
    }

    // Check if the HDE engine compressed the weights
    if(tmp->src->dimensions[0] != tmp->dst->dimensions[0] && !(src->hasAttr("is_pad") && src->get<bool>("is_pad")))
        tmp->compression =  true;

    toPush->task.value = tmp.release();

    toReturn.push_back(std::move(toPush));
}

void mv::RuntimeModel::case2MC(unsigned numTasks, ComputationModel& cm,  mv::DmaDirection direction, mv::Element &compilationDescriptor,
                               bool padFinalOutput, bool dmaToDMA, std::vector<std::unique_ptr<MVCNN::TaskT>>& toReturn,
                               mv::Data::TensorIterator src, mv::Data::TensorIterator dst, uint8_t* port, int portLimit, const std::string& srcAllocator,
                               const std::string& dstAllocator)
{
    mv::OpModel om(cm);
    for(unsigned i = 0; i < numTasks; ++i)
    {
        std::unique_ptr<MVCNN::TaskT> toPush = std::unique_ptr<MVCNN::TaskT>(new MVCNN::TaskT());
        std::unique_ptr<MVCNN::NNDMATaskT> tmp = std::unique_ptr<MVCNN::NNDMATaskT>(new MVCNN::NNDMATaskT());
        toPush->task.type = MVCNN::SpecificTask_NNDMATask;
        tmp->dst = buildTensorReferenceT(cm, compilationDescriptor, dst, i, dstAllocator);
        tmp->src = buildTensorReferenceT(cm, compilationDescriptor, src, i, srcAllocator);

        // if the DMA Out tensor is input Tensor for DMA in, have to make sure the src/dst dims match for CM Conv
        if(dmaToDMA)
        {
            tmp->src->dimensions = tmp->dst->dimensions;
            updateTensorReferenceT(cm, compilationDescriptor, src, dst, i, tmp->src, srcAllocator);
        }

        tmp->port = (*port)++;
        if (portLimit <= *port)
        {
            *port = 0;
        }

        if (direction != mv::DDR2NNCMX && direction != mv::CSRAM2NNCMX)
        {
            if (padFinalOutput && dst->hasAttr("alignment"))
                alignTensor(cm, tmp->dst, dst->getSubTensor(i), IO_CHANNEL_DIMENSION, padFinalOutput);
        }

        //Check if DMA is DDR2CMX or CSRAM2NNCMX
        if (direction == mv::DDR2NNCMX || direction == mv::CSRAM2NNCMX)
        {
            if (dst->hasAttr("alignWidth")){
                alignTensor(cm, tmp->dst, dst->getSubTensor(i), IO_WIDTH_DIMENSION, false);
            }
        }



        if (!checkUnstridedDMA(src, i, tmp.get()))
        {
            continue;
        }

        // Check if the HDE engine compressed the weights
        if(tmp->src->dimensions[0] != tmp->dst->dimensions[0])
            tmp->compression =  true;

        toPush->task.value = tmp.release();
        toReturn.push_back(std::move(toPush));
    }
}

std::vector<std::unique_ptr<MVCNN::TaskT>> mv::RuntimeModel::buildNNDMATaskT(ComputationModel& cm, mv::Element &compilationDescriptor, Control::OpListIterator opIt, std::string splitting, uint8_t* port)
{
    mv::DataModel dm(cm);
    mv::OpModel om(cm);

    auto direction = opIt->get<mv::DmaDirection>("direction");
    auto globalConfigParams = cm.getGlobalConfigParams();
    unsigned numTasks = globalConfigParams->get<int>("Number_of_Clusters");
    auto padFinalOutput = false;
    auto dmaToDma = false;

    auto inputTensor = opIt->getInputTensor(0);
    auto outputTensor = opIt->getOutputTensor(0);
    bool sourceIsBroadCasted = inputTensor->isBroadcasted();

    //NOTE: When strategy is overwritten
    if (direction == mv::DmaDirectionEnum::DDR2NNCMX || direction == mv::DmaDirectionEnum::CSRAM2NNCMX)
    {
       // inputTensor->setShape(outputTensor->getShape());
        if (inputTensor->hasAttr("overwriteStrategy"))
        {
            if (inputTensor->get<std::string>("overwriteStrategy") == "ClusteringToSoH")
                sourceIsBroadCasted = false;
            else if (inputTensor->get<std::string>("overwriteStrategy") == "SoHToClustering")
                sourceIsBroadCasted = true;
        }
    }
    else if (direction == mv::DmaDirectionEnum::NNCMX2DDR)
    {
        if (outputTensor->hasAttr("overwriteStrategy"))
        {
            if (outputTensor->get<std::string>("overwriteStrategy") == "SoHToClustering")
            {
                sourceIsBroadCasted = false;
                splitting = "SoHToClustering";
            }
            else if (outputTensor->get<std::string>("overwriteStrategy") == "ClusteringToSoH")
                sourceIsBroadCasted = true;
        }
    }

    auto tensorAllocatorName = outputTensor->get<std::set<std::string>>("allocators").begin();

    if(tensorAllocatorName == outputTensor->get<std::set<std::string>>("allocators").end()) {
        throw mv::ArgumentError(om, "buildNNDMATaskT", "0", "No tensor allocators found");
    }

    if (*tensorAllocatorName == "ProgrammableOutput")
        //Only if we are DMA-ing to programmable output check if we need to padd it
        padFinalOutput = cm.getGlobalConfigParams()->hasAttr("PadOutput") ? cm.getGlobalConfigParams()->get<bool>("PadOutput") : false;

    // check if we have 2 DMAs back to back and if an output tensor for one DMA has to become an input tensor for the next DMA
    // have to switch context to OM as CM OpIt gives barrier tasks too.
    auto omOpIt = om.switchContext(opIt);
    auto parentOp = om.getSourceOp(omOpIt->getInputTensor(0));
    if (parentOp->getOpType() == "DMATask" || parentOp->getOpType() == "ImplicitConcat")
    {

        auto sinkOperators = findSinkLayers(dm, omOpIt->getOutputTensor(0));

        if ((sinkOperators[0]->getOpType() == "Align" && sinkOperators[0]->getOutputTensor(0)->get<std::string>("splitStrategy") == "SplitOverHOverlapped") ||
            (omOpIt->getOutputTensor(0)->hasAttr("splitStrategy") && omOpIt->getOutputTensor(0)->get<std::string>("splitStrategy") == "SplitOverHOverlapped"))
        {
            if (parentOp->getOpType() == "DMATask")
            {
                // Indicate dmaToDma case if we have DMA(CMX2DDR)->DMA(DDR2CMX)
                auto parentDirection = parentOp->get<mv::DmaDirection>("direction");
                if (parentDirection == mv::NNCMX2DDR && direction == mv::DDR2NNCMX)
                    dmaToDma = true;
            }
            else if ((parentOp->getOpType() == "ImplicitConcat" && direction == mv::DDR2NNCMX))
            {
                // Indicate dmaToDma case if we have DMA(CMX2DDR)->ImplicitConcat->DMA(DDR2CMX)
                // TODO: Concat case should be revisited as concat might happen in CMX
                dmaToDma = true;
            }
        }
    }

    // TODO: We ought to use the port assigned during scheduling, but scheduling
    //       doesn't currently get to see split DMAs, so it schedules an entire
    //       split group onto a single DMA controller -- which is clearly incorrect.
    //       So instead, we implement a simple round-robin port assignment.
    auto globalConfig = cm.getGlobalConfigParams();
    int portLimit = 1;
    if (globalConfig->hasAttr("dmaControllers"))
    {
        portLimit = globalConfig->get<int>("dmaControllers");
    }

    // Case 1 of MC DMAs - Source tensor is broadcasted, i.e. present in it's entirety
    // in all clusters, OR populated tensors going into clustering op
    // (which for some reason are not marked as broadcasted).

    // Weights sparsity maps with new approach should be handled here

    // Strategy: 1 DMA to multiple slices -  multiple slices to 1 place
    // The second condition is necessary because when we spill we replace
    // the strategies, so a SplitOverK unpopulated tensor could potentially
    // not be marked as broadcasted
    if(sourceIsBroadCasted ||
      (splitting == "SplitOverK" && !outputTensor->isPopulated()) ||
      (splitting == "Clustering" && numTasks == 1))
    {
        std::vector<std::unique_ptr<MVCNN::TaskT>> toReturn;

        case1MC(numTasks, cm, direction, compilationDescriptor, padFinalOutput, dmaToDma, toReturn, inputTensor, outputTensor, port, portLimit);

        if(inputTensor->isSparse())
        {
            auto inputSparsityMap =
                dm.getTensor(inputTensor->getSparsityMap()->getName());
            auto outputSparsityMap =
                dm.getTensor(outputTensor->getSparsityMap()->getName());
          if (inputTensor->isPopulated()) {
            case1MC(numTasks, cm, direction, compilationDescriptor,
                padFinalOutput, dmaToDma, toReturn, inputSparsityMap,
                  outputSparsityMap, port, portLimit, "GraphFile", "VPU_CMX_NN");
          } else {
            auto inputStorageElementTable =
                dm.getTensor(inputTensor->getStorageElement()->getName());
            auto outputStorageElementTable =
                dm.getTensor(outputTensor->getStorageElement()->getName());

            case1MC(numTasks, cm, direction, compilationDescriptor,
                padFinalOutput, dmaToDma, toReturn, inputSparsityMap,
                  outputSparsityMap, port, portLimit);
            case1MC(numTasks, cm, direction, compilationDescriptor,
                padFinalOutput, dmaToDma, toReturn, inputStorageElementTable,
                  outputStorageElementTable, port, portLimit);
          }
        }

        return toReturn;
    }
    // Case 2 of MC DMAs - All cases that are not case 1 or 2. Mostly applied to SOH tensors for activation
    // and SOK for weights.

    // Weights sparsity maps with new approach has be handled in the future here, for the
    // case of SOK and weights sparsity.

    // Strategy: Multiple DMAs, yuppi!
    else
    {
        std::vector<std::unique_ptr<MVCNN::TaskT>> toReturn;

        case2MC(numTasks, cm, direction, compilationDescriptor, padFinalOutput,
            dmaToDma, toReturn, inputTensor, outputTensor, port, portLimit);
        // If the input tensor for a DMA task is sparse then we also need to
        // create DMA tasks which transfer Storage Element (SE) Table and
        // Sparsity Map (SM).
        if(inputTensor->isSparse())
        {
          auto inputSparsityMap =
            dm.getTensor(inputTensor->getSparsityMap()->getName());
          auto outputSparsityMap =
            dm.getTensor(outputTensor->getSparsityMap()->getName());
          if (inputTensor->isPopulated()) {
            case2MC(numTasks, cm, direction, compilationDescriptor,
                padFinalOutput, dmaToDma, toReturn, inputSparsityMap,
                  outputSparsityMap, port, portLimit, "GraphFile", "VPU_CMX_NN");
          } else {
            auto inputStorageElementTable =
                dm.getTensor(inputTensor->getStorageElement()->getName());
            auto outputStorageElementTable =
                dm.getTensor(outputTensor->getStorageElement()->getName());

            case2MC(numTasks, cm, direction, compilationDescriptor,
                padFinalOutput, dmaToDma, toReturn, inputSparsityMap,
                  outputSparsityMap, port, portLimit);

            case2MC(numTasks, cm, direction, compilationDescriptor,
                padFinalOutput, dmaToDma, toReturn, inputStorageElementTable,
                  outputStorageElementTable, port, portLimit);
          }
        }
        return toReturn;
    }
}

std::vector<std::unique_ptr<MVCNN::TaskT>> mv::RuntimeModel::buildHWDMATaskT(ComputationModel& cm, mv::Element &compilationDescriptor, Control::OpListIterator opIt)
{
    mv::DataModel dm(cm);
    mv::OpModel om(cm);

    auto inputTensor = opIt->getInputTensor(0);
    auto outputTensor = opIt->getOutputTensor(0);
    auto inputData = inputTensor->getData()[0];

    auto tensorAllocatorName = outputTensor->get<std::set<std::string>>("allocators").begin();
    if(tensorAllocatorName == outputTensor->get<std::set<std::string>>("allocators").end()) {
        throw mv::ArgumentError(om, "buildHWDMATaskT", "0", "No tensor allocators found");
    }

#if 0
    if (*tensorAllocatorName == "ProgrammableOutput")
        //Only if we are DMA-ing to programmable output check if we need to padd it
        padFinalOutput = cm.getGlobalConfigParams()->hasAttr("PadOutput") ? cm.getGlobalConfigParams()->get<bool>("PadOutput") : false;
#endif

    std::vector<std::unique_ptr<MVCNN::TaskT>> toReturn;
    std::unique_ptr<MVCNN::TaskT> toPush = std::unique_ptr<MVCNN::TaskT>(new MVCNN::TaskT());
    std::unique_ptr<MVCNN::NNDMATaskT> tmp = std::unique_ptr<MVCNN::NNDMATaskT>(new MVCNN::NNDMATaskT());
    toPush->task.type = MVCNN::SpecificTask_NNDMATask;
    //Output
    tmp->dst = buildTensorReferenceT(cm, compilationDescriptor, outputTensor);
    //Input
    tmp->src = std::unique_ptr<MVCNN::TensorReferenceT>(new MVCNN::TensorReferenceT());
    tmp->src->data_dtype = convertDtype(inputTensor->getDType());
    tmp->src->name = inputTensor->getName();
    /* The size of HW DMA operation is hardcoded to 4 bytes */
    tmp->src->dimensions = std::vector<uint32_t>{1,1,1,1};
    tmp->src->strides = std::vector<float>{4,4,4,4,4};
    tmp->src->locale = MVCNN::MemoryLocation_AbsoluteAddr;
    tmp->src->locale_index = std::vector<unsigned int>(1,0);
    tmp->src->data = std::unique_ptr<MVCNN::IndirectDataReferenceT>(new MVCNN::IndirectDataReferenceT());
    tmp->src->data->data_index = int64_t(inputData);
    tmp->src->data_dtype = MVCNN::DType_U32;

    //tmp->port = port;
    toPush->task.value = tmp.release();
    toReturn.push_back(std::move(toPush));

    return toReturn;
}

std::vector<std::unique_ptr<MVCNN::TaskT>> mv::RuntimeModel::buildNCE1TaskT(ComputationModel& /*cm*/, mv::Element& /*compilationDescriptor*/, Control::OpListIterator /*opIt*/)
{
    std::vector<std::unique_ptr<MVCNN::TaskT>> toReturn = std::vector<std::unique_ptr<MVCNN::TaskT>>(1);
    return toReturn;
}

MVCNN::DPULayerType mv::RuntimeModel::convertTaskOp(const std::string& opName)
{
    return dpuLayerMapping_.at(opName);
}


std::unique_ptr<MVCNN::PPEFixedFunctionT> mv::RuntimeModel::buildPPEFixedFunctionT(ComputationModel&, mv::Element&, const mv::PPEFixedFunction& ppeFixedFunction)
{
    std::unique_ptr<MVCNN::PPEFixedFunctionT> toBuild = std::unique_ptr<MVCNN::PPEFixedFunctionT>(new MVCNN::PPEFixedFunctionT());

    auto layers = ppeFixedFunction.getLayers();
    unsigned n = layers.size();
    toBuild->Ops = std::vector<MVCNN::PPELayerType>(n);
    for(unsigned i = 0; i < n; ++i)
        toBuild->Ops[i] = convertPPELayerType(layers[i]);
    toBuild->Clamp_Low = ppeFixedFunction.getLowClamp();
    toBuild->Clamp_High = ppeFixedFunction.getHighClamp();

    // Note depreciated attribute as the value is passed through weight table
    // However, Lrelu_Mult (scalar) is left for compatability
    toBuild->Lrelu_Mult = ppeFixedFunction.getLReluMult();

    toBuild->Lrelu_Shift = ppeFixedFunction.getLReluShift();

    return toBuild;
}

std::unique_ptr<MVCNN::PPETaskT> mv::RuntimeModel::buildPPETaskT(ComputationModel& cm, mv::Element& compilationDescriptor, Control::OpListIterator opIt)
{
    std::unique_ptr<MVCNN::PPETaskT> toBuild = std::unique_ptr<MVCNN::PPETaskT>(new MVCNN::PPETaskT());
    const mv::PPETask& ppeTask = opIt->get<PPETask>("PPETask");
    if(ppeTask.hasAttr("scaleData"))
        toBuild->scale_data = buildTensorReferenceT(cm, compilationDescriptor, ppeTask.getScaleData());
    toBuild->fixed_function = buildPPEFixedFunctionT(cm, compilationDescriptor, ppeTask.getFixedFunction());
    if (opIt->hasAttr("PWLType"))
    {
        auto index = opIt->get<std::size_t>("instructionListTableIndex");
        toBuild->instruction_list_data = buildTensorReferenceT(cm, compilationDescriptor, opIt->getInputTensor()[index]);
    }
    return toBuild;
}

std::unique_ptr<MVCNN::PPETaskT> mv::RuntimeModel::buildPPETaskT()
{
    std::unique_ptr<MVCNN::PPETaskT> toBuild = std::unique_ptr<MVCNN::PPETaskT>(new MVCNN::PPETaskT());
    toBuild->fixed_function = std::unique_ptr<MVCNN::PPEFixedFunctionT>(new MVCNN::PPEFixedFunctionT());
    toBuild->fixed_function->Clamp_High = 2147483647;
    toBuild->fixed_function->Clamp_Low = -2147483648;
    toBuild->fixed_function->Ops = std::vector<MVCNN::PPELayerType>();

    // Note depreciated attribute as the value is passed through weight table
    toBuild->fixed_function->Lrelu_Mult = 1;

    toBuild->fixed_function->Lrelu_Shift = 0;
    toBuild->fixed_function->Ops.reserve(5);

    return toBuild;
}

using fakeSparseAdaptorFunc = std::function<void(
    std::unique_ptr<MVCNN::NCEInvariantFieldsT>& inv,
    mv::Control::OpListIterator opIt)>;

void mv::RuntimeModel::adaptFakeSparsityIndex(
    std::unique_ptr<MVCNN::NCEInvariantFieldsT>& inv,
    Control::OpListIterator opIt,
    int clusterId)
{
    auto seTensorIdx = opIt->get<std::vector<std::size_t>>
        ("storageElementIndex");
    auto smTensorIdx = opIt->get<std::vector<std::size_t>>
        ("unpopulatedSparsityMapIndex");

    const std::unordered_map<std::string, fakeSparseAdaptorFunc> fakeSparseAdaptors =
    {
        {
            "Conv",
            [seTensorIdx, smTensorIdx](
                    std::unique_ptr<MVCNN::NCEInvariantFieldsT>& inv1,
                    Control::OpListIterator opIt1) {
                inv1->input_data->data->storage_element_index =
                    opIt1->getInputTensor(seTensorIdx[0])
                        ->getAddress();
                inv1->input_data->data->sparsity_index =
                    opIt1->getInputTensor(smTensorIdx[0])
                        ->getAddress();
            }
        },
        {
            "Eltwise",
            [seTensorIdx, smTensorIdx, clusterId](
                    std::unique_ptr<MVCNN::NCEInvariantFieldsT>& inv1,
                    Control::OpListIterator opIt1) {
                inv1->input_data->data->storage_element_index =
                    opIt1->getInputTensor(seTensorIdx[0])
                        ->getSubTensor(clusterId)
                        .getAddress();
                inv1->weights_data->data->storage_element_index =
                    opIt1->getInputTensor(seTensorIdx[1])
                        ->getSubTensor(clusterId)
                        .getAddress();

                inv1->input_data->data->sparsity_index =
                    opIt1->getInputTensor(smTensorIdx[0])
                        ->getSubTensor(clusterId)
                        .getAddress();
                inv1->weights_data->data->sparsity_index =
                    opIt1->getInputTensor(smTensorIdx[1])
                        ->getSubTensor(clusterId)
                        .getAddress();
            }
        },
        {
            "HwConvert",
            [seTensorIdx, smTensorIdx, clusterId](
                    std::unique_ptr<MVCNN::NCEInvariantFieldsT>& inv1,
                    Control::OpListIterator opIt1) {
                inv1->input_data->data->storage_element_index =
                    opIt1->getInputTensor(seTensorIdx[0])
                        ->getSubTensor(clusterId)
                        .getAddress();
                inv1->weights_data->data->storage_element_index =
                    opIt1->getInputTensor(seTensorIdx[0])
                        ->getSubTensor(clusterId)
                        .getAddress();

                inv1->input_data->data->sparsity_index =
                    opIt1->getInputTensor(smTensorIdx[0])
                        ->getSubTensor(clusterId)
                        .getAddress();
                inv1->weights_data->data->sparsity_index =
                    opIt1->getInputTensor(smTensorIdx[0])
                        ->getSubTensor(clusterId)
                        .getAddress();
            }
        }
    };

    auto fakeSparseAdaptor = fakeSparseAdaptors.find(
        opIt->get<std::string>("taskOp"));

    if (fakeSparseAdaptor != fakeSparseAdaptors.cend())
        fakeSparseAdaptor->second(inv, opIt);
    else
        Logger::log(mv::Logger::MessageType::Error, "RuntimeModel",
            opIt->getName() + ": No registered fake sparse adapter op type " +
            opIt->get<std::string>("taskOp"));
}

void mv::RuntimeModel::adaptFakeSparsityIndex(
    std::unique_ptr<MVCNN::NCEInvariantFieldsT>& inv,
    Control::OpListIterator opIt)
{
    auto seTensorIdx = opIt->get<std::vector<std::size_t>>
        ("storageElementIndex");
    auto smTensorIdx = opIt->get<std::vector<std::size_t>>
        ("unpopulatedSparsityMapIndex");

    const std::unordered_map<std::string, fakeSparseAdaptorFunc> fakeSparseAdaptors =
    {
        {
            "Conv",
            [seTensorIdx, smTensorIdx](std::unique_ptr<MVCNN::NCEInvariantFieldsT>& inv1,
                     Control::OpListIterator opIt1) {
                inv1->input_data->data->storage_element_index =
                    opIt1->getInputTensor(seTensorIdx[0])
                        ->getAddress();
                inv1->input_data->data->sparsity_index =
                    opIt1->getInputTensor(smTensorIdx[0])
                        ->getAddress();
            }
        },
        {
            "Eltwise",
            [seTensorIdx, smTensorIdx](std::unique_ptr<MVCNN::NCEInvariantFieldsT>& inv1,
                     Control::OpListIterator opIt1) {
                inv1->input_data->data->storage_element_index =
                    opIt1->getInputTensor(seTensorIdx[0])
                        ->getAddress();
                inv1->weights_data->data->storage_element_index =
                    opIt1->getInputTensor(seTensorIdx[1])
                        ->getAddress();

                inv1->input_data->data->sparsity_index =
                    opIt1->getInputTensor(smTensorIdx[0])
                        ->getAddress();
                inv1->weights_data->data->sparsity_index =
                    opIt1->getInputTensor(smTensorIdx[1])
                        ->getAddress();

            }
        },
        {
            "HwConvert",
            [seTensorIdx, smTensorIdx](std::unique_ptr<MVCNN::NCEInvariantFieldsT>& inv1,
                     Control::OpListIterator opIt1) {
                inv1->input_data->data->storage_element_index =
                    opIt1->getInputTensor(seTensorIdx[0])
                        ->getAddress();
                inv1->weights_data->data->storage_element_index =
                    opIt1->getInputTensor(seTensorIdx[0])
                        ->getAddress();

                inv1->input_data->data->sparsity_index =
                    opIt1->getInputTensor(smTensorIdx[0])
                        ->getAddress();
                inv1->weights_data->data->sparsity_index =
                    opIt1->getInputTensor(smTensorIdx[0])
                        ->getAddress();
            }
        }
    };

    auto fakeSparseAdaptor = fakeSparseAdaptors.find(
        opIt->get<std::string>("taskOp"));

    if (fakeSparseAdaptor != fakeSparseAdaptors.cend())
        fakeSparseAdaptor->second(inv, opIt);
    else
        Logger::log(mv::Logger::MessageType::Error, "RuntimeModel",
            opIt->getName() + ": No registered fake sparse adapter op type " +
            opIt->get<std::string>("taskOp"));
}

void mv::RuntimeModel::updatePWLTaskT(std::unique_ptr<MVCNN::NCEInvariantFieldsT>& toBuild , Control::OpListIterator& opIt){
    if (!opIt->hasAttr("pwlQuantParams"))
        return;

    auto pwlQuant = opIt->get<mv::QuantizationParams>("pwlQuantParams");
    auto quantMult = reduceQuantVector_(pwlQuant.getMult());
    toBuild->output_data->quant_mult = std::vector<unsigned short int>(quantMult.begin(), quantMult.end());
    toBuild->parent_output_tensor->quant_mult = std::vector<unsigned short int>(quantMult.begin(), quantMult.end());
    auto quantShift = reduceQuantVector_(pwlQuant.getShift());
    toBuild->output_data->quant_shift = std::vector<unsigned char>(quantShift.begin(), quantShift.end());
    toBuild->parent_output_tensor->quant_shift = std::vector<unsigned char>(quantShift.begin(), quantShift.end());
    auto quantZero = pwlQuant.getZeroPoint();
    toBuild->output_data->quant_zero = std::vector<unsigned char>(quantZero.begin(), quantZero.end());
    toBuild->parent_output_tensor->quant_zero = std::vector<unsigned char>(quantZero.begin(), quantZero.end());
    auto quantPostShift = pwlQuant.getPostShift();
    toBuild->output_data->quant_post_shift_right = quantPostShift;
    toBuild->parent_output_tensor->quant_post_shift_right = quantPostShift;
}

void mv::RuntimeModel::specializeBasePtrs(const mv::Control::OpListIterator& opIt, std::unique_ptr<MVCNN::NCEInvariantFieldsT>& toBuild, const unsigned int clusterId, const unsigned int maxClusters)
{
    if (!opIt->hasAttr("inputActivationSparsity") || !opIt->get<bool>("inputActivationSparsity"))
    {
        toBuild->input_data->base_ptrs.clear();
        if (opIt->hasAttr("taskOp") && opIt->get<std::string>("taskOp") == "Eltwise")
            toBuild->weights_data->base_ptrs.clear();
    }
    else if (opIt->hasAttr("taskOp") && opIt->get<std::string>("taskOp") == "Eltwise")
    {
        if (opIt->getInputTensor(0)->hasAttr("base_ptrs") && opIt->getInputTensor(1)->hasAttr("base_ptrs"))
        {
            const auto firstInputBasePtrs = opIt->getInputTensor(0)->get<std::vector<unsigned short>>("base_ptrs");
            toBuild->input_data->base_ptrs = std::vector<unsigned short>(maxClusters, 0);
            toBuild->input_data->base_ptrs[0] = firstInputBasePtrs[clusterId];

            const auto secondInputBasePtrs = opIt->getInputTensor(1)->get<std::vector<unsigned short>>("base_ptrs");
            toBuild->weights_data->base_ptrs = std::vector<unsigned short>(maxClusters, 0);
            toBuild->weights_data->base_ptrs[1] = secondInputBasePtrs[clusterId];
        }
        else
            Logger::log(mv::Logger::MessageType::Warning, "RuntimeModel",
                "Sparse Eltwise (" + opIt->getName() + ") needs base_ptrs for both inputs");
    }
    else if (!opIt->hasAttr("is_segmented") || !opIt->get<bool>("is_segmented"))
    {
        if (opIt->getInputTensor(0)->hasAttr("base_ptrs"))
        {
            const auto basePtrs = opIt->getInputTensor(0)->get<std::vector<unsigned short>>("base_ptrs");
            toBuild->input_data->base_ptrs = std::vector<unsigned short>(maxClusters, 0);
            toBuild->input_data->base_ptrs[0] = basePtrs[clusterId];
        }
    }

    if (!opIt->hasAttr("outputActivationSparsity") || !opIt->get<bool>("outputActivationSparsity"))
    {
        toBuild->output_data->base_ptrs.clear();
    }
    else
    {
        if (opIt->getOutputTensor(0)->hasAttr("base_ptrs"))
        {
            const auto basePtrs = opIt->getOutputTensor(0)->get<std::vector<unsigned short>>("base_ptrs");
            toBuild->output_data->base_ptrs = std::vector<unsigned short>(maxClusters, 0);
            toBuild->output_data->base_ptrs[clusterId] = basePtrs[clusterId];
        }
    }
}

std::unique_ptr<MVCNN::NCEInvariantFieldsT> mv::RuntimeModel::buildNCEInvariantFieldsT(ComputationModel& cm, mv::Element &compilationDescriptor, Control::OpListIterator opIt)
{
    std::unique_ptr<MVCNN::NCEInvariantFieldsT> toBuild = std::unique_ptr<MVCNN::NCEInvariantFieldsT>(new MVCNN::NCEInvariantFieldsT());

    auto taskOp = opIt->get<std::string>("taskOp");
    toBuild->dpu_task_type = convertTaskOp(taskOp);

    if(opIt->hasAttr("PPETask"))
        toBuild->ppe_task = buildPPETaskT(cm, compilationDescriptor, opIt);
    else
        toBuild->ppe_task = buildPPETaskT();

    if (opIt->hasAttr("kSize"))
    {
        auto kernelShape = opIt->get<std::array<unsigned short, 2>>("kSize");
        toBuild->kernelW = kernelShape[0];
        toBuild->kernelH = kernelShape[1];
    }

    if (opIt->hasAttr("stride"))
    {
        auto kernelStride = opIt->get<std::array<unsigned short, 2>>("stride");
        toBuild->kernel_strideW = kernelStride[0];
        toBuild->kernel_strideH = kernelStride[1];
    }

    if (opIt->hasAttr("padding"))
    {
        auto kernelPadding = opIt->get<std::array<unsigned short, 4>>("padding");
        toBuild->kernel_padLeft = kernelPadding[0];
        toBuild->kernel_padRight = kernelPadding[1];
        toBuild->kernel_padTop = kernelPadding[2];
        toBuild->kernel_padBottom = kernelPadding[3];
    }
    //input
    auto inputTensor = opIt->getInputTensor(0);

    toBuild->input_data = buildTensorReferenceT(cm, compilationDescriptor, inputTensor);
    toBuild->parent_input_tensor = buildTensorReferenceT(cm, compilationDescriptor, inputTensor);
    toBuild->parent_input_tensor->data->sparsity_index = 999999999999999999;
    toBuild->parent_input_tensor->data->storage_element_index = 999999999999999999;

    //output
    auto outputTensor = opIt->getOutputTensor(0);

    toBuild->output_data = buildTensorReferenceT(cm, compilationDescriptor, outputTensor);
    toBuild->parent_output_tensor = buildTensorReferenceT(cm, compilationDescriptor, outputTensor);
    toBuild->parent_output_tensor->data->sparsity_index = 999999999999999999;
    toBuild->parent_output_tensor->data->storage_element_index = 999999999999999999;

    updatePWLTaskT(toBuild, opIt);

    if(opIt->hasAttr("fakeSparsity"))
    {
        auto activationWindowTensorIterator = opIt->getInputTensor(opIt->get<std::size_t>("fakeSparsityIndex"));
        toBuild->activation_window = buildTensorReferenceT(cm, compilationDescriptor, activationWindowTensorIterator);
        toBuild->activation_window_channel_length = activationWindowTensorIterator->get<int>("channelLength");
    }

    if(toBuild->dpu_task_type != MVCNN::DPULayerType_ELTWISE)
    {
        auto weightsTableTensorIterator = opIt->getInputTensor(opIt->get<std::size_t>("weightsTableIndex"));
        toBuild->weights_table = buildTensorReferenceT(cm, compilationDescriptor, weightsTableTensorIterator);
    }

    switch (toBuild->dpu_task_type)
    {
        case MVCNN::DPULayerType_CONV:
        case MVCNN::DPULayerType_DWCONV:
        case MVCNN::DPULayerType_CMCONV:
        case MVCNN::DPULayerType_FCL:
        case MVCNN::DPULayerType_ELTWISE:
            //std::unique_ptr<TensorReferenceT> parent_weights_tensor;
            if (opIt->isEltwiseSingleInputTypeOp())
                // Single input Eltwise ops are Eltwise with weight_data = input_data
                toBuild->weights_data = buildTensorReferenceT(cm, compilationDescriptor, inputTensor);
            else
                toBuild->weights_data = buildTensorReferenceT(cm, compilationDescriptor, opIt->getInputTensor(1));
            break;
        default:
            break;
    }

    if ((opIt->getOpType() == "DPUTask"
            && opIt->get<std::string>("taskOp") == "Eltwise"))
    {
        auto firstInput = opIt->getInputTensor()[mv::IO_TENSOR_INPUT];
        auto secondInput = opIt->getInputTensor()[mv::IO_TENSOR_WEIGHTS_SET];
        if (firstInput->hasAttr("doubleTest") &&
            firstInput->get<bool>("doubleTest"))
            toBuild->input_data->dimensions = toBuild->weights_data->dimensions;
        else if (secondInput->hasAttr("doubleTest") &&
            secondInput->get<bool>("doubleTest"))
            toBuild->weights_data->dimensions = toBuild->input_data->dimensions;
    }

    if ((opIt->hasAttr("activationSparsityCompilerSolving")
        && opIt->get<bool>("activationSparsityCompilerSolving")) ||
            (opIt->hasAttr("activationSparsityCompilerSolvingForDilatedConv") &&
             opIt->get<bool>("activationSparsityCompilerSolvingForDilatedConv")) ||
            (opIt->hasAttr("activationSparsityCompilerSolvingForInterpNN") &&
             opIt->get<bool>("activationSparsityCompilerSolvingForInterpNN")))

        adaptFakeSparsityIndex(toBuild, opIt);

    if (opIt->hasAttr("is_segmented"))
        toBuild->is_segmented = opIt->get<bool>("is_segmented");

    return toBuild;
}


// Multicluster version
std::unique_ptr<MVCNN::NCEInvariantFieldsT> mv::RuntimeModel::buildNCEInvariantFieldsT(ComputationModel& cm, mv::Element &compilationDescriptor, Control::OpListIterator opIt, int clusterId)
{
    unsigned numTask = 0;
    numTask = cm.getGlobalConfigParams()->get<int>("Number_of_Clusters");
    std::unique_ptr<MVCNN::NCEInvariantFieldsT> toBuild = std::unique_ptr<MVCNN::NCEInvariantFieldsT>(new MVCNN::NCEInvariantFieldsT());

    auto taskOp = opIt->get<std::string>("taskOp");
    toBuild->dpu_task_type = convertTaskOp(taskOp);

    if(opIt->hasAttr("PPETask"))
        toBuild->ppe_task = buildPPETaskT(cm, compilationDescriptor, opIt);
    else
        toBuild->ppe_task = buildPPETaskT();

    if (opIt->hasAttr("kSize"))
    {
        auto kernelShape = opIt->get<std::array<unsigned short, 2>>("kSize");
        toBuild->kernelW = kernelShape[0];
        toBuild->kernelH = kernelShape[1];
    }

    if (opIt->hasAttr("stride"))
    {
        auto kernelStride = opIt->get<std::array<unsigned short, 2>>("stride");
        toBuild->kernel_strideW = kernelStride[0];
        toBuild->kernel_strideH = kernelStride[1];
    }

    if (opIt->hasAttr("padding"))
    {
        auto kernelPadding = opIt->get<std::array<unsigned short, 4>>("padding");
        if(opIt->hasAttr("splitStrategy") &&
           opIt->get<std::string>("splitStrategy") == "SplitOverH" &&
           opIt->get<std::string>("taskOp") == "ChannelMajorConvolution")
        {
            const unsigned numClusters = cm.getGlobalConfigParams()->get<int>("Number_of_Clusters");
            kernelPadding = getNewPadding(kernelPadding, clusterId, numClusters);
        }

        toBuild->kernel_padLeft = kernelPadding[0];
        toBuild->kernel_padRight = kernelPadding[1];
        toBuild->kernel_padTop = kernelPadding[2];
        toBuild->kernel_padBottom = kernelPadding[3];
    }

    //input
    auto parentInputTensor = opIt->getInputTensor(IO_TENSOR_INPUT);
    if(opIt->get<std::string>("splitStrategy") == "SplitOverH" &&
       opIt->getInputTensor(IO_TENSOR_INPUT)->get<std::string>("splitStrategy") == "ClusteringAndSOH")
    {
        toBuild->input_data = buildTensorReferenceT(cm, compilationDescriptor, parentInputTensor, clusterId + numTask);
    }
    else if (opIt->get<std::string>("splitStrategy") == "SplitOverK")
    {
        toBuild->input_data = buildTensorReferenceT(cm, compilationDescriptor, parentInputTensor);
        const std::vector<unsigned int> locale_index = { static_cast<unsigned int>(clusterId) };
        toBuild->input_data->locale_index = locale_index;
        toBuild->out_channel_offset = 0;
        for (int i = 0; i < clusterId; i++)
            toBuild->out_channel_offset +=
                opIt->getOutputTensor(IO_TENSOR_OUTPUT)->getSubTensor(i).getShape()[IO_CHANNEL_DIMENSION];
    }
    else
    {
        toBuild->input_data = buildTensorReferenceT(cm, compilationDescriptor, parentInputTensor, clusterId);
    }

    toBuild->parent_input_tensor = buildTensorReferenceT(cm, compilationDescriptor, parentInputTensor);
    toBuild->parent_input_tensor->data->sparsity_index = 999999999999999999;
    toBuild->parent_input_tensor->data->storage_element_index = 999999999999999999;

    if (opIt->hasAttr("cmx_concat_reader")) {
      toBuild->input_data->strides = toBuild->parent_input_tensor->strides;
    }

    //output
    const auto parentOutputTensor = opIt->getOutputTensor(IO_TENSOR_OUTPUT);
    toBuild->output_data = buildTensorReferenceT(cm, compilationDescriptor, parentOutputTensor, clusterId, "", true);

    if (opIt->hasAttr("multiCast") && opIt->get<bool>("multiCast") )
    {
        unsigned numTasks = cm.getGlobalConfigParams()->get<int>("Number_of_Clusters");
        toBuild->output_data = buildTensorReferenceT(cm, compilationDescriptor, parentOutputTensor, clusterId, "", true);
        std::vector<unsigned int> locale_index;

        // if SOK and spill to DDR - no need to broadcast to all clusters.
        // Just broadcast to cluster 0 where the DMA out will occur from
        DataModel dm(cm);
        mv::Data::OpListIterator nextOp = dm.switchContext(opIt).leftmostOutput().sink();
        while(nextOp->isImplicit())
            nextOp = nextOp.leftmostOutput().sink();

        if ((opIt->get<std::string>("splitStrategy")=="SplitOverK") &&
            (nextOp->getOpType() == "DMATask") &&
            (nextOp->get<mv::DmaDirection>("direction") == mv::DmaDirectionEnum::NNCMX2DDR) )
        {   // just broadcast to cluster 0
            locale_index.push_back(0);
        }
        else
        {   // normal operation - broadcast to all clusters
            for (unsigned idx = numTasks; idx > 0; idx--)
                locale_index.push_back(idx-1);
        }

        toBuild->output_data->locale_index = locale_index;
        auto numericStrides = parentOutputTensor->computeNumericStrides();
        numericStrides.push_back(parentOutputTensor->getDType().getSizeInBits() / 8);
        std::reverse(numericStrides.begin(), numericStrides.end());
        toBuild->output_data->strides = numericStrides;
    }

    toBuild->parent_output_tensor = buildTensorReferenceT(cm, compilationDescriptor, parentOutputTensor);
    toBuild->parent_output_tensor->data->sparsity_index = 999999999999999999;
    toBuild->parent_output_tensor->data->storage_element_index = 999999999999999999;

    if (opIt->hasAttr("cmx_concat_writer")) {
      toBuild->output_data->strides = toBuild->parent_output_tensor->strides;
    }

    if (opIt->hasAttr("multiCast") && opIt->get<bool>("multiCast"))
    {
        if (opIt->get<std::string>("splitStrategy") == "HKSwitch")
        {
            auto outputTensor = opIt->getOutputTensor(0);
            const auto& subtensor = opIt->getOutputTensor(0)->getSubTensor(clusterId);
            auto offset = subtensor.get<std::vector<std::size_t>>("offset");
            auto index = outputTensor->getOrder().subToInd(outputTensor->getShape(), offset);
            auto byte_index = index * outputTensor->getDType().getSizeInBits() / 8;

            toBuild->output_data->data->data_index += byte_index;
        }
    }

    updatePWLTaskT(toBuild, opIt);

    //OP inputs == n ->
    // n - 2 activation window (when present)
    // n - 1 weights table
    if(opIt->hasAttr("fakeSparsity"))
    {
        auto activationWindowTensorIterator = opIt->getInputTensor(opIt->get<std::size_t>("fakeSparsityIndex"));
        toBuild->activation_window = buildTensorReferenceT(cm, compilationDescriptor, activationWindowTensorIterator, clusterId);
        toBuild->activation_window_channel_length = activationWindowTensorIterator->get<int>("channelLength");
    }

    if(toBuild->dpu_task_type != MVCNN::DPULayerType_ELTWISE)
    {
        auto weightsTableTensorIterator = opIt->getInputTensor(opIt->get<std::size_t>("weightsTableIndex"));
        toBuild->weights_table = buildTensorReferenceT(cm, compilationDescriptor, weightsTableTensorIterator, clusterId);
    }

    switch (toBuild->dpu_task_type)
    {
        case MVCNN::DPULayerType_CONV:
        case MVCNN::DPULayerType_DWCONV:
        case MVCNN::DPULayerType_CMCONV:
        case MVCNN::DPULayerType_FCL:
        case MVCNN::DPULayerType_ELTWISE:
            if (opIt->isEltwiseSingleInputTypeOp())
                // Single input Eltwise ops are Eltwise with weight_data = input_data
                toBuild->weights_data = buildTensorReferenceT(cm, compilationDescriptor, parentInputTensor, clusterId);
            else if (opIt->get<std::string>("splitStrategy") == "SplitOverH" &&
                opIt->getInputTensor(1)->get<std::string>("splitStrategy") == "ClusteringAndSOH")
                toBuild->weights_data = buildTensorReferenceT(cm, compilationDescriptor, opIt->getInputTensor(1), clusterId + numTask);
            else
                toBuild->weights_data = buildTensorReferenceT(cm, compilationDescriptor, opIt->getInputTensor(1), clusterId);
            break;
        default:
            break;
    }

    if ((opIt->getOpType() == "DPUTask"
            && opIt->get<std::string>("taskOp") == "Eltwise"))
    {
        if (opIt->getInputTensor()[0]->hasAttr("doubleTest") &&
            opIt->getInputTensor()[0]->get<bool>("doubleTest"))
            toBuild->input_data->dimensions = toBuild->weights_data->dimensions;
        else if (opIt->getInputTensor()[1]->hasAttr("doubleTest") &&
            opIt->getInputTensor()[1]->get<bool>("doubleTest"))
            toBuild->weights_data->dimensions = toBuild->input_data->dimensions;
    }

    if ((opIt->hasAttr("activationSparsityCompilerSolving")
        && opIt->get<bool>("activationSparsityCompilerSolving")) ||
            (opIt->hasAttr("activationSparsityCompilerSolvingForDilatedConv") &&
             opIt->get<bool>("activationSparsityCompilerSolvingForDilatedConv")) ||
            (opIt->hasAttr("activationSparsityCompilerSolvingForInterpNN") &&
             opIt->get<bool>("activationSparsityCompilerSolvingForInterpNN")))
        adaptFakeSparsityIndex(toBuild, opIt);

    if (opIt->hasAttr("is_segmented"))
        toBuild->is_segmented = opIt->get<bool>("is_segmented");

    return toBuild;
}

MVCNN::MPE_Mode mv::RuntimeModel::convertMPEMode(mv::MPE_Mode mpe)
{
    switch (mpe)
    {
        case mv::MPE_Mode::Matrix:
            return MVCNN::MPE_Mode::MPE_Mode_MATRIX;
        case mv::MPE_Mode::Vector:
            return MVCNN::MPE_Mode::MPE_Mode_VECTOR;
        case mv::MPE_Mode::Vector_FP16:
            return MVCNN::MPE_Mode::MPE_Mode_VECTOR_FP16;
        case mv::MPE_Mode::CUBOID_16x16:
            return MVCNN::MPE_Mode::MPE_Mode_CUBOID_16x16;
        case mv::MPE_Mode::CUBOID_4x16:
            return MVCNN::MPE_Mode::MPE_Mode_CUBOID_4x16;
        case mv::MPE_Mode::CUBOID_8x16:
            return MVCNN::MPE_Mode::MPE_Mode_CUBOID_8x16;
        default:
            return MVCNN::MPE_Mode::MPE_Mode_VECTOR;
    }
}

bool mv::RuntimeModel::hardwareBugDepthwise(Control::OpListIterator opIt)
{
    auto splitStrategy = opIt->get<std::string>("splitStrategy");
    auto padding = opIt->get<std::array<unsigned short, 4>>("padding");
    auto kernelSize = opIt->get<std::array<unsigned short, 2>>("kSize");
    return (((splitStrategy == "SplitOverH") || splitStrategy == "SplitOverHOverlapped") &&
        (padding[0] % 2 == 1) &&
        (kernelSize[mv::KERNEL_HEIGHT] > 1));
}


std::array<unsigned short, 4> mv::RuntimeModel::getNewPadding(std::array<unsigned short, 4> padding, int clusterId, int numClusters)
{
        if (clusterId == 0)
        {
            padding[3] = 0;
        }
        else if (clusterId == numClusters - 1)
        {
            padding[2] = 0;
        }
        else
        {
            padding[2] = 0;
            padding[3] = 0;
        }
        return padding;
}

void mv::RuntimeModel::getWorkloadPadding(Control::OpListIterator opIt, Workload &workload, unsigned clusterId, const std::string strategy)
{
    if (opIt->isEltwiseTypeOp())
    {
        workload.padLeft = 0;
        workload.padTop = 0;
        workload.padRight = 0;
        workload.padBottom = 0;
    }
    else
    {
        std::array<unsigned short, 4> padding;
        if (strategy != "Clustering")
            padding = getPadding(opIt, clusterId);
        else
            padding = opIt->get<std::array<unsigned short, 4>>("padding");

        auto outputWidth = opIt->getOutputTensor(0)->getShape()[mv::IO_WIDTH_DIMENSION];
        auto outputHeight = opIt->getOutputTensor(0)->getShape()[mv::IO_HEIGHT_DIMENSION];


        long long  pad_left = padding[mv::PADDING_LEFT] - workload.MinX;
        workload.padLeft = (pad_left > 0) ? pad_left : 0;
        long long  pad_top = padding[mv::PADDING_TOP] -  workload.MinY;
        workload.padTop = (pad_top > 0) ? pad_top : 0;
        long long  pad_right = workload.MaxX + 1 + padding[mv::PADDING_RIGHT] - outputWidth;
        workload.padRight = (pad_right > 0) ? pad_right : 0;
        long long  pad_bottom = workload.MaxY + 1 + padding[mv::PADDING_BOT] - outputHeight;
        workload.padBottom = (pad_bottom > 0) ? pad_bottom : 0;

    }
    return;
}

std::array <unsigned short, 4>  mv::RuntimeModel::getPadding(Control::OpListIterator opIt, unsigned clusterId)
{
    std::array <unsigned short, 4> padding = opIt->get<std::array<unsigned short, 4>>("padding");

    if(clusterId !=0)
    {
        const auto& subTensor = opIt->getOutputTensor(0)->getSubTensor(clusterId);
        std::vector<std::size_t> offset = subTensor.get<std::vector<std::size_t>>("offset");

        //NOTE:Padding up
        if (offset[1] != 0)
            padding[2] = 0;

        //NOTE:Padding left
        if (offset[0] != 0)
            padding[0] = 0;

        //NOTE:Padding down
        if (subTensor.getShape()[IO_HEIGHT_DIMENSION] + offset[1] != opIt->getOutputTensor(0)->getShape()[IO_HEIGHT_DIMENSION])
            padding[3] = 0;

        if (subTensor.getShape()[IO_WIDTH_DIMENSION] + offset[0] != opIt->getOutputTensor(0)->getShape()[IO_WIDTH_DIMENSION])
            padding[1] = 0;
    }

    return padding;
}

std::unique_ptr<MVCNN::NCEVariantFieldsT> mv::RuntimeModel::buildNCEVariantFieldsT(ComputationModel& , mv::Element& /*compilationDescriptor*/, Control::OpListIterator opIt, Workload workload, unsigned clusterId, std::string strategy)
{
    std::unique_ptr<MVCNN::NCEVariantFieldsT> toBuild = std::unique_ptr<MVCNN::NCEVariantFieldsT>(new MVCNN::NCEVariantFieldsT());

    toBuild->mpe_mode = convertMPEMode(workload.MPEMode);
    toBuild->workload_start_X = workload.MinX;
    toBuild->workload_start_Y = workload.MinY;
    toBuild->workload_start_Z = workload.MinZ;
    toBuild->workload_end_X = workload.MaxX;
    toBuild->workload_end_Y = workload.MaxY;
    toBuild->workload_end_Z = workload.MaxZ;
    getWorkloadPadding(opIt, workload, clusterId, strategy);
    toBuild->padLeft = workload.padLeft;
    toBuild->padRight = workload.padRight;
    toBuild->padTop = workload.padTop;
    toBuild->padBottom = workload.padBottom;

    return toBuild;
}

std::vector<std::unique_ptr<MVCNN::NCEVariantFieldsT>> mv::RuntimeModel::buildNCEVariantFieldsTVector(ComputationModel& cm, mv::Element &compilationDescriptor,
                                                                                                      Control::OpListIterator opIt, unsigned numTask, std::string strategy)
{
    std::vector<std::unique_ptr<MVCNN::NCEVariantFieldsT>> toBuild;
    if (targetEmulator_(compilationDescriptor))
        return toBuild;

    std::vector <mv::Workload> workloads;
    //NOTE: For Clustering SubTensors equal Workloads per Subtensor
    if (strategy == "Clustering" && numTask > 0)
        workloads = opIt->get<mv::Workloads>("Workloads" + std::to_string(0)).getWorkloads();
    else
        workloads = opIt->get<mv::Workloads>("Workloads" + std::to_string(numTask)).getWorkloads();

    toBuild.reserve(workloads.size());
    for(std::size_t i = 0; i < workloads.size(); ++i)
    {
        toBuild.push_back(buildNCEVariantFieldsT(cm, compilationDescriptor, opIt, workloads[i], numTask, strategy));
    }
    return toBuild;
}

std::vector<std::unique_ptr<MVCNN::TaskT>> mv::RuntimeModel::buildNCE2TaskT(ComputationModel& cm, mv::Element &compilationDescriptor, Control::OpListIterator opIt, const std::string& splitting)
{
    unsigned numTask = 0;
    numTask = cm.getGlobalConfigParams()->get<int>("Number_of_Clusters");

    std::vector<std::unique_ptr<MVCNN::TaskT>> toReturn = std::vector<std::unique_ptr<MVCNN::TaskT>>(numTask);
    if (splitting != "Clustering" && !targetEmulator_(compilationDescriptor))
        for(unsigned i = 0; i < numTask; ++i)
        {
            toReturn[i] = std::unique_ptr<MVCNN::TaskT>(new MVCNN::TaskT());
            toReturn[i]->task.type = MVCNN::SpecificTask_NCE2Task;
            auto toBuild = new MVCNN::NCE2TaskT();
            toBuild->variant = buildNCEVariantFieldsTVector(cm, compilationDescriptor, opIt, i, splitting);
            toBuild->invariant = buildNCEInvariantFieldsT(cm, compilationDescriptor, opIt, i);
            specializeBasePtrs(opIt, toBuild->invariant, i);

            auto hash = [](const MVCNN::MPE_Mode &g){ return static_cast<std::size_t>(g); };
            auto comp = [](const MVCNN::MPE_Mode &l, const MVCNN::MPE_Mode &r){ return l == r; };

            std::unordered_map<MVCNN::MPE_Mode, unsigned, decltype(hash), decltype(comp)> frequencyCounter(4, hash, comp);
            for(auto& variantField : toBuild->variant)
                ++frequencyCounter[variantField->mpe_mode];

            unsigned maxFrequency = 0;
            for(auto& frequencyCouple : frequencyCounter)
                if(frequencyCouple.second > maxFrequency)
                    toBuild->invariant->mpe_frequent_mode = frequencyCouple.first;

            toReturn[i]->task.value = toBuild;
        }
    else
        for(unsigned i = 0; i < numTask; ++i)
        {
            //Clustering in multiple clusters has to be executed in every cluster
            toReturn[i] = std::unique_ptr<MVCNN::TaskT>(new MVCNN::TaskT());
            toReturn[i]->task.type = MVCNN::SpecificTask_NCE2Task;
            auto toBuild = new MVCNN::NCE2TaskT();
            toBuild->variant = buildNCEVariantFieldsTVector(cm, compilationDescriptor, opIt, i, splitting);
            toBuild->invariant = buildNCEInvariantFieldsT(cm, compilationDescriptor, opIt);

            if(targetEmulator_(compilationDescriptor))
            {
                toReturn[i]->task.value = toBuild;
                continue;
            }

            specializeBasePtrs(opIt, toBuild->invariant, i);

            auto locale_index = std::vector<unsigned int>(1,i);
            toBuild->invariant->input_data->locale_index = locale_index;
            toBuild->invariant->output_data->locale_index = locale_index;

            if (opIt->hasAttr("cmx_concatable")) {
              // clustering DPU task set locale = [0, 1, 2, 4] //
              std::vector<unsigned int> cmx_concat_locale_index;
              for (unsigned idx = numTask; idx > 0; idx--) {
                cmx_concat_locale_index.push_back(idx-1);
              }
              toBuild->invariant->output_data->locale_index =
                  cmx_concat_locale_index;
            }

            if (opIt->get<std::string>("taskOp") != "MaxPool")
                toBuild->invariant->weights_data->locale_index = locale_index;

            if (!opIt->isEltwiseTypeOp())
                toBuild->invariant->weights_table->locale_index = locale_index;

            if(opIt->hasAttr("fakeSparsity"))
                toBuild->invariant->activation_window->locale_index = locale_index;

            auto hash = [](const MVCNN::MPE_Mode &g){ return static_cast<std::size_t>(g); };
            auto comp = [](const MVCNN::MPE_Mode &l, const MVCNN::MPE_Mode &r){ return l == r; };

            std::unordered_map<MVCNN::MPE_Mode, unsigned, decltype(hash), decltype(comp)> frequencyCounter(4, hash, comp);
            for(auto& variantField : toBuild->variant)
                ++frequencyCounter[variantField->mpe_mode];

            unsigned maxFrequency = 0;
            for(auto& frequencyCouple : frequencyCounter)
                if(frequencyCouple.second > maxFrequency)
                    toBuild->invariant->mpe_frequent_mode = frequencyCouple.first;

            toReturn[i]->task.value = toBuild;
        }

    /* Remove unused custom PWL attribute */
    if(opIt->hasAttr("PWLType"))
    {
        opIt->erase("PWLType");
    }

    return toReturn;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPASoftmaxTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_SoftmaxParams;
    auto softLayerParamsValue = new MVCNN::SoftmaxParamsT();

    std::string axis = opIt->get<std::string>("axis");
    // TODO: magic numbers
    if (axis.compare(std::string("C")) == 0)
        softLayerParamsValue->axis = 1;
    else if (axis.compare(std::string("H")) == 0)
        softLayerParamsValue->axis = 2;
    else if (axis.compare(std::string("W")) == 0)
        softLayerParamsValue->axis = 3;

    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPALeakyReluTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    auto softLayerParamsValue = new MVCNN::PostOpsParamsT();

    auto leakyReluParams = new MVCNN::LeakyReluParamsT();
    auto alpha = opIt->get<double>("alpha");
    leakyReluParams->negative_slope = alpha;

    softLayerParamsValue->nested_params.type = MVCNN::PostOpsNestedParams_LeakyReluParams;
    softLayerParamsValue->nested_params.value = leakyReluParams;
     toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPASigmoidTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    auto softLayerParamsValue = new MVCNN::PostOpsParamsT();
    softLayerParamsValue->nested_params.type = MVCNN::PostOpsNestedParams_SigmoidParams;
    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAClampTask(ComputationModel &cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    auto softLayerParamsValue = new MVCNN::ClampParamsT();

    float min = opIt->get<float>("min");
    float max = opIt->get<float>("max");

    softLayerParamsValue->min = min;
    softLayerParamsValue->max = max;

    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT *mv::RuntimeModel::buildUPANormalizeTask(ComputationModel &cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_NormalizeParams;
    auto softLayerParamsValue = new MVCNN::NormalizeParamsT();

    softLayerParamsValue->eps = static_cast<float>(opIt->get<double>("eps"));
    softLayerParamsValue->across_spatial = static_cast<int32_t>(opIt->get<unsigned>("across_spatial"));
    softLayerParamsValue->channel_shared = static_cast<int32_t>(opIt->get<unsigned>("channel_shared"));

    toBuild->softLayerParams.value = softLayerParamsValue;
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, opIt->getInputTensor(1)));

    return toBuild;
}

MVCNN::UPALayerTaskT *mv::RuntimeModel::buildUPAProposalTask(ComputationModel &cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{

    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_ProposalParams;
    auto softLayerParamsValue = new MVCNN::ProposalParamsT();

    // input_tensor mapping:
    // 0 = cls_pred --> UPALayerTask.input_data
    // 1 = bbox_pred --> UPALayerTask.weights_data
    // 2 = im_info --> UPALayerTask.weights_table
    auto cls_pred = opIt->getInputTensor(0);
    auto bbox_pred = opIt->getInputTensor(1);
    auto im_info = opIt->getInputTensor(2);

    // output tensor mapping:
    // 0 = output --> UPALayerTask.output_data
    auto output = opIt->getOutputTensor(0);

    // Build scale vector
    auto scale_vector = opIt->get<std::vector<float>>("scale");

    // Build ratio vector
    auto ratio_vector = opIt->get<std::vector<float>>("ratio");

    // Fill in tensors
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, cls_pred));
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, bbox_pred));
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, im_info));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    // Fill in required params
    softLayerParamsValue->ratio = std::move(ratio_vector);
    softLayerParamsValue->scale = std::move(scale_vector);
    softLayerParamsValue->base_size = opIt->get<unsigned>("base_size");
    softLayerParamsValue->pre_nms_topn = opIt->get<unsigned>("pre_nms_topn");
    softLayerParamsValue->post_nms_topn = opIt->get<unsigned>("post_nms_topn");
    softLayerParamsValue->nms_thresh = static_cast<float>(opIt->get<double>("nms_thresh"));
    softLayerParamsValue->feat_stride = opIt->get<unsigned>("feat_stride");
    softLayerParamsValue->min_size = opIt->get<unsigned>("min_size");

    // Fill in optional params
    softLayerParamsValue->pre_nms_thresh = static_cast<float>(opIt->get<double>("pre_nms_thresh"));
    softLayerParamsValue->clip_before_nms = opIt->get<bool>("clip_before_nms");
    softLayerParamsValue->clip_after_nms = opIt->get<bool>("clip_after_nms");
    softLayerParamsValue->normalize = opIt->get<bool>("normalize");
    softLayerParamsValue->box_size_scale = static_cast<float>(opIt->get<double>("box_size_scale"));
    softLayerParamsValue->box_coordinate_scale = static_cast<float>(opIt->get<double>("box_coordinate_scale"));
    softLayerParamsValue->framework = opIt->get<std::string>("framework");
    softLayerParamsValue->for_deformable = opIt->get<bool>("for_deformable");

    toBuild->softLayerParams.value = softLayerParamsValue;

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAROIPoolingTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{

    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_ROIPoolingParams;
    auto softLayerParamsValue = new MVCNN::ROIPoolingParamsT();

    auto input = opIt->getInputTensor(0);
    auto coords = opIt->getInputTensor(1);

    auto output = opIt->getOutputTensor(0);

    // Fill in tensors
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, coords));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    // Fill in required params
    softLayerParamsValue->pooled_w = opIt->get<unsigned>("pooled_w");
    softLayerParamsValue->pooled_h = opIt->get<unsigned>("pooled_h");
    softLayerParamsValue->spatial_scale = static_cast<float>(opIt->get<double>("spatial_scale"));
    softLayerParamsValue->roi_pooling_method = opIt->get<unsigned>("roi_pooling_method");
    softLayerParamsValue->num_rois = opIt->get<unsigned>("num_rois");

    toBuild->softLayerParams.value = softLayerParamsValue;

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAPSROIPoolingTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PSROIPoolingParams;
    auto softLayerParamsValue = new MVCNN::PSROIPoolingParamsT();

    auto input  = opIt->getInputTensor(0);
    auto coords = opIt->getInputTensor(1);
    auto output = opIt->getOutputTensor(0);

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, coords));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    softLayerParamsValue->output_dim    = opIt->get<std::size_t>("output_dim");
    softLayerParamsValue->group_size    = opIt->get<std::size_t>("group_size");
    softLayerParamsValue->pooled_w    = opIt->get<std::size_t>("pooled_w");
    softLayerParamsValue->pooled_h    = opIt->get<std::size_t>("pooled_h");
    softLayerParamsValue->spatial_scale = static_cast<float>(opIt->get<double>("spatial_scale"));
    softLayerParamsValue->spatial_bin_x = opIt->get<std::size_t>("spatial_bin_x");
    softLayerParamsValue->spatial_bin_y = opIt->get<std::size_t>("spatial_bin_y");

    auto mode = opIt->get<std::string>("mode");
    if (mode.compare(std::string("average")) == 0) {
        softLayerParamsValue->mode = MVCNN::PSROIPoolingMode_AVERAGE;
    } else if (mode.compare(std::string("bilinear")) == 0) {
        softLayerParamsValue->mode = MVCNN::PSROIPoolingMode_BILINEAR;
    } else if (mode.compare(std::string("bilinear_deformable")) == 0) {
        softLayerParamsValue->mode = MVCNN::PSROIPoolingMode_BILINEAR_DEFORMABLE;
    } else {
        delete toBuild;
        delete softLayerParamsValue;
        throw ArgumentError("buildUPAPSROIPoolingTask", "file:content", "invalid", "Invalid mode for PSROIPooling");
    }

    toBuild->softLayerParams.value = softLayerParamsValue;

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAInterpTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{

    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_InterpParams;
    auto softLayerParamsValue = new MVCNN::InterpParamsT();

    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);

    // Fill in tensors
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    // Fill in required params
    softLayerParamsValue->pad_beg = opIt->get<unsigned>("pad_beg");
    softLayerParamsValue->pad_end = opIt->get<unsigned>("pad_end");
    softLayerParamsValue->align_corners = opIt->get<bool>("align_corners");
    toBuild->softLayerParams.value = softLayerParamsValue;

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAInterpolateTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto toBuild = std::make_unique<MVCNN::UPALayerTaskT>();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_InterpolateParams;
    auto softLayerParamsValue = std::make_unique<MVCNN::InterpolateParamsT>();

    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);

    std::string mode = opIt->get<std::string>("mode");
    std::string coord = opIt->get<std::string>("coordinate_transformation_mode");
    std::string near = opIt->get<std::string>("nearest_mode");

    // Fill in required params
    softLayerParamsValue->antialias = opIt->get<bool>("antialias");

    const auto interpolateModeIter = interpModeMap.find(mode);
    if (interpolateModeIter == interpModeMap.end())
        throw std::runtime_error("interpModeMap map doesn't contain reqested interpolate mode");
    softLayerParamsValue->interpolationMode = MVCNN::InterpolationMethod(interpolateModeIter->second);

    const auto coordModeIter = coordTransformModeMap.find(coord);
    if (coordModeIter == coordTransformModeMap.end())
        throw std::runtime_error("coordTransformModeMap map doesn't contain reqested coordinate transformation mode");
    softLayerParamsValue->coordTransformMode = MVCNN::InterpolationCoordTransMode(coordModeIter->second);

    const auto nearestModeIter = nearestModeMap.find(near);
    if (nearestModeIter == nearestModeMap.end())
        throw std::runtime_error("nearestModeMap map doesn't contain reqested nearest mode");
    softLayerParamsValue->nearestMode = MVCNN::InterpolationNearestMode(nearestModeIter->second);

    softLayerParamsValue->align_corners = softLayerParamsValue->coordTransformMode == coordTransformModeMap.find("align_corners")->second;

    // Fill in tensors
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));
    toBuild->softLayerParams.value = softLayerParamsValue.release();

    return toBuild.release();
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAReverseSequenceTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto toBuild = std::make_unique<MVCNN::UPALayerTaskT>();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_ReversesequenceParams;
    auto softLayerParamsValue = std::make_unique<MVCNN::ReversesequenceParamsT>();

    auto input = opIt->getInputTensor(0);
    auto length = opIt->getInputTensor(1);
    auto output = opIt->getOutputTensor(0);

    // Fill in required params
    softLayerParamsValue->seq_axis = opIt->get<int64_t>("seq_axis");
    softLayerParamsValue->batch_axis = opIt->get<int64_t>("batch_axis");

    toBuild->softLayerParams.value = softLayerParamsValue.release();

    // Fill in tensors
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, length));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild.release();
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPANormTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{

    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_NormParams;
    auto softLayerParamsValue = new MVCNN::NormParamsT();

    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);

    // Fill in tensors
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    // Fill in required params
    softLayerParamsValue->alpha = opIt->get<double>("alpha");
    softLayerParamsValue->beta = opIt->get<double>("beta");
    softLayerParamsValue->region = opIt->get<std::string>("region");
    softLayerParamsValue->local_size = opIt->get<unsigned>("local_size");
    toBuild->softLayerParams.value = softLayerParamsValue;

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAQuantizeTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_QuantizeParams;
    auto softLayerParamsValue = new MVCNN::QuantizeParamsT();

    auto quantizationParams = output->getQuantParams();
    auto quantScale = quantizationParams.getScale();
    auto quantZero = quantizationParams.getZeroPoint();

    // Convert vectors to fp16
    auto scale_vector = std::vector<unsigned short>();
    for (unsigned i = 0; i < quantScale.size(); ++i)
        scale_vector.push_back(mv::fp32_to_fp16(static_cast<float>(quantScale.at(i))));

    auto zero_vector = std::vector<unsigned short>();
    for (unsigned i = 0; i < quantZero.size(); ++i)
        zero_vector.push_back(mv::fp32_to_fp16(static_cast<float>(quantZero.at(i))));

    toBuild->softLayerParams.value = softLayerParamsValue;
    softLayerParamsValue->scale = std::vector<unsigned short>(scale_vector.begin(), scale_vector.end());
    softLayerParamsValue->zero = std::vector<unsigned short>(zero_vector.begin(), zero_vector.end());

    toBuild->input_data = buildTensorReferenceT(cm, compilationDescriptor, input);
    toBuild->output_data = buildTensorReferenceT(cm, compilationDescriptor, output);

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAResampleTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_ResampleParams;
    auto softLayerParamsValue = new MVCNN::ResampleParamsT();
    std::string interpolation = opIt->get<std::string>("interpolation");
    if (interpolation.compare(std::string("BILINEAR")) == 0)
        softLayerParamsValue->interpolation = MVCNN::InterpolationMethod_BILINEAR;
    else if (interpolation.compare(std::string("BICUBIC")) == 0)
        softLayerParamsValue->interpolation = MVCNN::InterpolationMethod_BICUBIC;
    else
        softLayerParamsValue->interpolation = MVCNN::InterpolationMethod_NEAREST;

    softLayerParamsValue->antialias = opIt->get<bool>("antialias");

    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->input_data = buildTensorReferenceT(cm, compilationDescriptor, input);
    toBuild->output_data = buildTensorReferenceT(cm, compilationDescriptor, output);

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}


MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAReshapeTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_ReshapeParams;
    auto softLayerParamsValue = new MVCNN::ReshapeParamsT();

    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->input_data = buildTensorReferenceT(cm, compilationDescriptor, input);
    toBuild->output_data = buildTensorReferenceT(cm, compilationDescriptor, output);

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPARegionYoloTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_RegionYOLOParams;
    auto softLayerParamsValue = new MVCNN::RegionYOLOParamsT();

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    softLayerParamsValue->coords = opIt->get<unsigned>("coords");
    softLayerParamsValue->classes = opIt->get<unsigned>("classes");
    softLayerParamsValue->num = opIt->get<unsigned>("num");
    softLayerParamsValue->do_softmax = opIt->get<bool>("do_softmax");
    auto mask_uint = opIt->get<std::vector<unsigned>>("mask");
    std::vector<int> mask(std::begin(mask_uint), std::end(mask_uint));
    softLayerParamsValue->mask = mask;

    toBuild->softLayerParams.value = softLayerParamsValue;

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAReorgYoloTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_ReorgYOLOParams;
    auto softLayerParamsValue = new MVCNN::ReorgYOLOParamsT();

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    softLayerParamsValue->stride = static_cast<int>(opIt->get<unsigned>("stride"));

    toBuild->softLayerParams.value = softLayerParamsValue;

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAPermuteTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PermuteParams;
    auto softLayerParamsValue = new MVCNN::PermuteParamsT();

    auto x = opIt->get<unsigned>("permute_order_x");
    auto y = opIt->get<unsigned>("permute_order_y");
    auto z = opIt->get<unsigned>("permute_order_z");
    std::unique_ptr<MVCNN::order3> order3 = std::unique_ptr<MVCNN::order3>(new MVCNN::order3(x,y,z));
    softLayerParamsValue->permute_order = std::move(order3);

    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->input_data = buildTensorReferenceT(cm, compilationDescriptor, input);
    toBuild->output_data = buildTensorReferenceT(cm, compilationDescriptor, output);

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAPermuteNDTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PermuteNDParams;
    auto softLayerParamsValue = new MVCNN::PermuteNDParamsT();

    auto perm_order = opIt->get<std::vector<int64_t>>("perm_order");
    for (const auto& dim : perm_order) {
        softLayerParamsValue->permute_nd_order.push_back(dim);
    }

    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->input_data = buildTensorReferenceT(cm, compilationDescriptor, input);
    toBuild->output_data = buildTensorReferenceT(cm, compilationDescriptor, output);

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPADetectionOutputTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{

    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_DetectionOutputParams;
    auto softLayerParamsValue = new MVCNN::DetectionOutputParamsT();

    auto box_logits = opIt->getInputTensor(0);
    auto class_preds = opIt->getInputTensor(1);
    auto proposals = opIt->getInputTensor(2);

    auto output = opIt->getOutputTensor(0);

    // Fill in tensors
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, box_logits));
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, class_preds));
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, proposals));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    // Parse code_type
    std::string code_type = "CORNER";
    if(opIt->get<std::string>("code_type").compare(std::string("caffe.PriorBoxParameter.CORNER_SIZE")) == 0)
        code_type = "CORNER_SIZE";
    else if(opIt->get<std::string>("code_type").compare(std::string("caffe.PriorBoxParameter.CENTER_SIZE")) ==  0)
        code_type = "CENTER_SIZE";

    // Fill in required params
    softLayerParamsValue->num_classes = opIt->get<int64_t>("num_classes");
    softLayerParamsValue->keep_top_k = opIt->get<int64_t>("keep_top_k");
    softLayerParamsValue->nms_threshold = static_cast<float>(opIt->get<double>("nms_threshold"));
    softLayerParamsValue->background_label_id = opIt->get<int64_t>("background_label_id");
    softLayerParamsValue->top_k = opIt->get<int64_t>("top_k");
    softLayerParamsValue->variance_encoded_in_target = opIt->get<bool>("variance_encoded_in_target");
    softLayerParamsValue->code_type = code_type;
    softLayerParamsValue->share_location = opIt->get<bool>("share_location");
    softLayerParamsValue->confidence_threshold = static_cast<float>(opIt->get<double>("confidence_threshold"));
    softLayerParamsValue->clip_before_nms = opIt->get<bool>("clip_before_nms");
    softLayerParamsValue->clip_after_nms = opIt->get<bool>("clip_after_nms");
    softLayerParamsValue->decrease_label_id = opIt->get<int64_t>("decrease_label_id");
    softLayerParamsValue->normalized = opIt->get<bool>("normalized");
    softLayerParamsValue->input_height = opIt->get<int64_t>("input_height");
    softLayerParamsValue->input_width = opIt->get<int64_t>("input_width");
    softLayerParamsValue->objectness_score = static_cast<float>(opIt->get<double>("objectness_score"));

    toBuild->softLayerParams.value = softLayerParamsValue;

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAPriorboxTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{

    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PriorboxParams;
    auto softLayerParamsValue = new MVCNN::PriorboxParamsT();

    auto priorbox = opIt->getInputTensor(0);
    auto image = opIt->getInputTensor(1);
    auto min_sizes = opIt->getInputTensor(2);
    auto max_sizes = opIt->getInputTensor(3);
    auto aspect_ratios = opIt->getInputTensor(4);
    auto variances = opIt->getInputTensor(5);

    auto output = opIt->getOutputTensor(0);

    auto min_sizes_vector = std::vector<float>();
    auto min_sizes_data = min_sizes->getData();
    for (unsigned i = 0; i < min_sizes->size(); ++i)
        min_sizes_vector.push_back(mv::fp16_to_fp32(static_cast<uint16_t>(min_sizes->getIntData().at(i))));

    auto max_sizes_vector = std::vector<float>();
    for (unsigned i = 0; i < max_sizes->size(); ++i)
        max_sizes_vector.push_back(mv::fp16_to_fp32(static_cast<uint16_t>(max_sizes->getIntData().at(i))));

    auto aspect_ratios_vector = std::vector<float>();
    for (unsigned i = 0; i < aspect_ratios->size(); ++i)
        aspect_ratios_vector.push_back(mv::fp16_to_fp32(static_cast<uint16_t>(aspect_ratios->getIntData().at(i))));

    auto variances_vector = std::vector<float>();
    for (unsigned i = 0; i < variances->size(); ++i)
        variances_vector.push_back(mv::fp16_to_fp32(static_cast<uint16_t>(variances->getIntData().at(i))));

    // Fill in tensors
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, image));
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, priorbox));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    // Fill in required params
    softLayerParamsValue->min_sizes = std::vector<float>(min_sizes_vector.begin(), min_sizes_vector.end());
    softLayerParamsValue->max_sizes = std::vector<float>(max_sizes_vector.begin(), max_sizes_vector.end());
    softLayerParamsValue->aspect_ratios = std::vector<float>(aspect_ratios_vector.begin(), aspect_ratios_vector.end());
    softLayerParamsValue->variances = std::vector<float>(variances_vector.begin(), variances_vector.end());
    softLayerParamsValue->flip = opIt->get<unsigned>("flip");
    softLayerParamsValue->clip = opIt->get<unsigned>("clip");
    softLayerParamsValue->step_w = static_cast<float>(opIt->get<double>("step_w"));
    softLayerParamsValue->step_h = static_cast<float>(opIt->get<double>("step_h"));
    softLayerParamsValue->offset = static_cast<float>(opIt->get<double>("offset"));

    toBuild->softLayerParams.value = softLayerParamsValue;

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAArgmaxTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_ArgMaxParams;
    auto softLayerParamsValue = new MVCNN::ArgMaxParamsT();

    auto out_max_val = static_cast<bool>(opIt->get<int64_t>("out_max_val"));
    auto top_k = static_cast<unsigned>(opIt->get<int64_t>("top_k"));
    auto axis = opIt->get<int64_t>("axis");

    softLayerParamsValue->out_max_val = out_max_val;
    softLayerParamsValue->top_k = top_k;
    softLayerParamsValue->axis = axis;

    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->input_data = buildTensorReferenceT(cm, compilationDescriptor, input);
    toBuild->output_data = buildTensorReferenceT(cm, compilationDescriptor, output);

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPATopKTask(ComputationModel&, Element &, Control::OpListIterator)
{
    throw ArgumentError("tools:RuntimeModel", "UPATask", "Unsupported", "topK not implemented yet");

    auto toBuild = new MVCNN::UPALayerTaskT();
    //TODO
    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAPassthroughTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PassthroughParams;
    auto softLayerParamsValue = new MVCNN::PassthroughParamsT();
    //softLayerParamsValue->min_delay_us = 1000;
    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAEltwiseFP16Task(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input0 = opIt->getInputTensor(0);
    auto input1 = opIt->getInputTensor(1);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_EltwiseParams;
    auto softLayerParamsValue = new MVCNN::EltwiseParamsT();

    toBuild->softLayerParams.value = softLayerParamsValue;
    std::string operation = opIt->get<std::string>("eltwiseType");
    if (operation.compare(std::string("Add")) == 0)
        softLayerParamsValue->operation = "sum";
    else if (operation.compare(std::string("Multiply")) == 0)
        softLayerParamsValue->operation = "prod";
    else if (operation.compare(std::string("SqDiff")) == 0)
        softLayerParamsValue->operation = "sqdiff";
    else if (operation.compare(std::string("Maximum")) == 0)
        softLayerParamsValue->operation = "max";
    else if (operation.compare(std::string("Equal")) == 0)
        softLayerParamsValue->operation = "equal";
    else if (operation.compare(std::string("NotEqual")) == 0)
        softLayerParamsValue->operation = "not_equal";
    else if (operation.compare(std::string("Greater")) == 0)
        softLayerParamsValue->operation = "greater";
    else if (operation.compare(std::string("GreaterEqual")) == 0)
        softLayerParamsValue->operation = "greater_equal";
    else if (operation.compare(std::string("Less")) == 0)
        softLayerParamsValue->operation = "less";
    else if (operation.compare(std::string("LessEqual")) == 0)
        softLayerParamsValue->operation = "less_equal";
    else
        throw std::runtime_error("buildUPAEltwiseFP16Task: unsupported SW Eltwise Operation, check implementation.");

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input0));
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input1));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPATileTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_TileParams;
    auto softLayerParamsValue = new MVCNN::TileParamsT();

    // Fill in required params
    // reverse axis position  since buildTensorReferenceT reverses shape order
    softLayerParamsValue->axis = opIt->get<unsigned>("axis");
    softLayerParamsValue->tiles = opIt->get<unsigned>("tiles");

    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->input_data = buildTensorReferenceT(cm, compilationDescriptor, input);
    toBuild->output_data = buildTensorReferenceT(cm, compilationDescriptor, output);

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPACTCDecoderTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input0 = opIt->getInputTensor(0);
    auto input1 = opIt->getInputTensor(1);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    toBuild->maxShaves = 1;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_CTCDecoderParams;
    auto softLayerParamsValue = new MVCNN::CTCDecoderParamsT();

    // Fill in required params
    softLayerParamsValue->ctc_merge_repeated = opIt->get<bool>("ctc_merge_repeated");

    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input0));
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input1));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPADummyTask(ComputationModel& /*cm*/, Element& /*compilationDescriptor*/, Control::OpListIterator /*opIt*/)
{
    auto toBuild = new MVCNN::UPALayerTaskT();
    //toBuild->maxShaves = ;
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_DummyParams;
    auto softLayerParamsValue = new MVCNN::DummyParamsT();
    //softLayerParamsValue->message = "Hello!";
    //softLayerParamsValue->executeShaveKernel = false;
    toBuild->softLayerParams.value = softLayerParamsValue;
    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPACustomOclTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto toBuild = new MVCNN::UPALayerTaskT();
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_CustomLayerOclParams;

    for (size_t i = 0; i < opIt->inputSlots(); i++) {
        const auto input = opIt->getInputTensor(i);
        toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    }

    for (size_t i = 0; i < opIt->outputSlots(); i++) {
        const auto output = opIt->getOutputTensor(i);
        toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));
    }

    auto softParams = new MVCNN::CustomLayerOclParamsT();
    toBuild->softLayerParams.value = softParams;

    softParams->leonPreambleID = -1u;  // unused

    const auto pack = [](const std::vector<uint8_t>& src) {
        auto packed = std::vector<uint64_t>(ceil_division(src.size(), 8));
        for (size_t i = 0; i < src.size(); i++) {
            ((uint8_t *) packed.data())[i] = src[i];
        }
        return packed;
    };

    const auto paramData = opIt->get<std::vector<uint8_t>>("paramData");
    softParams->paramData = std::unique_ptr<MVCNN::BinaryDataT>(new MVCNN::BinaryDataT());
    softParams->paramData->underlying_type = MVCNN::DType::DType_U8;
    softParams->paramData->data = pack(paramData);
    softParams->paramData->length = paramData.size();

    const auto kernelData = opIt->get<std::vector<uint8_t>>("kernelData");
    softParams->kernelData = std::unique_ptr<MVCNN::BinaryDataT>(new MVCNN::BinaryDataT());
    softParams->kernelData->underlying_type = MVCNN::DType::DType_U8;
    softParams->kernelData->data = pack(kernelData);
    softParams->kernelData->length = kernelData.size();

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPACustomCppTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto toBuild = new MVCNN::UPALayerTaskT();
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_CustomLayerCppParams;

    for (size_t i = 0; i < opIt->inputSlots(); i++) {
        const auto input = opIt->getInputTensor(i);
        toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    }

    for (size_t i = 0; i < opIt->outputSlots(); i++) {
        const auto output = opIt->getOutputTensor(i);
        toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));
    }

    auto softParams = new MVCNN::CustomLayerCppParamsT();
    toBuild->softLayerParams.value = softParams;

    softParams->leonPreambleID = -1u;  // unused

    const auto pack = [](const std::vector<uint8_t>& src) {
        auto packed = std::vector<uint64_t>(ceil_division(src.size(), 8));
        for (size_t i = 0; i < src.size(); i++) {
            ((uint8_t *) packed.data())[i] = src[i];
        }
        return packed;
    };

    const auto paramData = opIt->get<std::vector<uint8_t>>("paramData");
    softParams->paramData = std::unique_ptr<MVCNN::BinaryDataT>(new MVCNN::BinaryDataT());
    softParams->paramData->underlying_type = MVCNN::DType::DType_U8;
    softParams->paramData->data = pack(paramData);
    softParams->paramData->length = paramData.size();

    const auto kernelData = opIt->get<std::vector<uint8_t>>("kernelData");
    softParams->kernelData = std::unique_ptr<MVCNN::BinaryDataT>(new MVCNN::BinaryDataT());
    softParams->kernelData->underlying_type = MVCNN::DType::DType_U8;
    softParams->kernelData->data = pack(kernelData);
    softParams->kernelData->length = kernelData.size();

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPADeconvTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto toBuild = new MVCNN::UPALayerTaskT();
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_DeconvolutionParams;
    auto softLayerParamsValue = new MVCNN::DeconvolutionParamsT();

    auto input = opIt->getInputTensor(0);
    auto weights = opIt->getInputTensor(1);
    // auto biases = opIt->getInputTensor(2);  biases
    auto output = opIt->getOutputTensor(0);

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, weights));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    // Kernel
    auto kernel =
        std::unique_ptr<MVCNN::order3>(new MVCNN::order3(weights->getShape()[mv::IO_WIDTH_DIMENSION], weights->getShape()[mv::IO_HEIGHT_DIMENSION], 0));
    softLayerParamsValue->kernel = std::move(kernel);

    // Strides
    if (opIt->hasAttr("stride"))
    {
        auto kernelStride = opIt->get<std::array<unsigned short, 2>>("stride");
        auto strides =
            std::unique_ptr<MVCNN::order3>(new MVCNN::order3(kernelStride[0], kernelStride[1], 0));
        softLayerParamsValue->strides = std::move(strides);
    }

    // Paddings
    if (opIt->hasAttr("padding"))
    {
        auto kernelPadding = opIt->get<std::array<unsigned short, 4>>("padding");

        auto pads_begin =
            std::unique_ptr<MVCNN::order3>(new MVCNN::order3(kernelPadding[0], kernelPadding[2], 0)); // Left,Top,Front
        softLayerParamsValue->pads_begin = std::move(pads_begin);

        auto pads_end =
            std::unique_ptr<MVCNN::order3>(new MVCNN::order3(kernelPadding[1], kernelPadding[3], 0)); // Right,Bottom,Back
        softLayerParamsValue->pads_end = std::move(pads_end);
    }

    // Dilations
    if (opIt->hasAttr("dilationFactor"))
    {
        auto kernelDilation = opIt->get<unsigned>("dilationFactor");

        auto dilations =
            std::unique_ptr<MVCNN::order3>(new MVCNN::order3(kernelDilation, kernelDilation, 0));
        softLayerParamsValue->dilations = std::move(dilations);
    }

    // Depthwise flag
    softLayerParamsValue->is_depthwise = opIt->get<bool>("is_depthwise");

    toBuild->softLayerParams.value = softLayerParamsValue;

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPARefConvTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    const auto toBuild = new MVCNN::UPALayerTaskT();
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_ConvolutionParams;

    const auto numInputs = opIt->inputSlots();
    assert(numInputs == 2 || numInputs == 3);

    const auto input = opIt->getInputTensor(0);
    const auto weights = opIt->getInputTensor(1);
    const auto output = opIt->getOutputTensor(0);

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, weights));
    if (numInputs == 3)
    {
        const auto biases = opIt->getInputTensor(2);
        toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, biases));
    }
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    const auto softLayerParamsValue = new MVCNN::ConvolutionParamsT();

    // Kernel

    softLayerParamsValue->kernel =
        std::unique_ptr<MVCNN::order3>(
            new MVCNN::order3(
                weights->getShape()[mv::IO_WIDTH_DIMENSION],
                weights->getShape()[mv::IO_HEIGHT_DIMENSION],
                0));

    // Strides

    const auto kernelStride = opIt->get<std::array<unsigned short, 2>>("stride");

    softLayerParamsValue->strides =
        std::unique_ptr<MVCNN::order3>(
            new MVCNN::order3(kernelStride[0], kernelStride[1], 0));

    // Paddings

    const auto padding = opIt->get<std::array<unsigned short, 4>>("padding");

    softLayerParamsValue->pads_begin =
        std::unique_ptr<MVCNN::order3>(
            new MVCNN::order3(padding[0], padding[2], 0)); // Left,Top,Front

    softLayerParamsValue->pads_end =
        std::unique_ptr<MVCNN::order3>(
            new MVCNN::order3(padding[1], padding[3], 0)); // Right,Bottom,Back

    // Dilations

    const auto dilationFactor = opIt->get<unsigned>("dilationFactor");

    softLayerParamsValue->dilations =
        std::unique_ptr<MVCNN::order3>(
            new MVCNN::order3(dilationFactor, dilationFactor, 0));

    // Group

    const auto group = opIt->get<unsigned>("group");

    softLayerParamsValue->group = group;

    toBuild->softLayerParams.value = softLayerParamsValue;

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAFakeQuantizeTask(ComputationModel &cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    const auto toBuild = new MVCNN::UPALayerTaskT();
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_FakeQuantizeParams;

    const auto input = opIt->getInputTensor(0);
    const auto output = opIt->getOutputTensor(0);

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    const auto input_low = opIt->getInputTensor(1)->getIntData();
    const auto input_high = opIt->getInputTensor(2)->getIntData();
    const auto output_low = opIt->getInputTensor(3)->getIntData();
    const auto output_high = opIt->getInputTensor(4)->getIntData();

    const auto softLayerParamsValue = new MVCNN::FakeQuantizeParamsT();

    softLayerParamsValue->levels = opIt->get<unsigned>("levels");

    softLayerParamsValue->input_low.resize(input_low.size());
    std::copy_n(input_low.data(), input_low.size(), softLayerParamsValue->input_low.data());

    softLayerParamsValue->input_high.resize(input_high.size());
    std::copy_n(input_high.data(), input_high.size(), softLayerParamsValue->input_high.data());

    softLayerParamsValue->output_low.resize(output_low.size());
    std::copy_n(output_low.data(), output_low.size(), softLayerParamsValue->output_low.data());

    softLayerParamsValue->output_high.resize(output_high.size());
    std::copy_n(output_high.data(), output_high.size(), softLayerParamsValue->output_high.data());

    toBuild->softLayerParams.value = softLayerParamsValue;

    return toBuild;
}

MVCNN::UPALayerTaskT *mv::RuntimeModel::buildUPAGatherTask(mv::ComputationModel &cm, mv::Element &compilationDescriptor, mv::Control::OpListIterator opIt)
{
    auto input0 = opIt->getInputTensor(0);
    auto input1 = opIt->getInputTensor(1);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_GatherParams;

    auto softLayerParamsValue = new MVCNN::GatherParamsT();

    // Fill in required params

    softLayerParamsValue->axis = opIt->get<unsigned>("axis");

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input0));
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input1));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    toBuild->softLayerParams.value = softLayerParamsValue;

    return toBuild;
}

MVCNN::UPALayerTaskT *mv::RuntimeModel::buildUPAHSwishTask(mv::ComputationModel &cm, mv::Element &compilationDescriptor, mv::Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    auto softLayerParamsValue = new MVCNN::PostOpsParamsT();

    softLayerParamsValue->nested_params.type = MVCNN::PostOpsNestedParams_HSwishParams;
    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT *mv::RuntimeModel::buildUPASwishTask(mv::ComputationModel &cm, mv::Element &compilationDescriptor, mv::Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();

    auto* postOpsParamsValue = new MVCNN::SwishParamsT();
    postOpsParamsValue->beta = opIt->get<double>("beta");

    auto* softLayerParamsValue = new MVCNN::PostOpsParamsT();
    softLayerParamsValue->nested_params.type = MVCNN::PostOpsNestedParams_SwishParams;
    softLayerParamsValue->nested_params.value = postOpsParamsValue;

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT *mv::RuntimeModel::buildUPAMishTask(mv::ComputationModel &cm, mv::Element &compilationDescriptor, mv::Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    auto softLayerParamsValue = new MVCNN::PostOpsParamsT();

    softLayerParamsValue->nested_params.type = MVCNN::PostOpsNestedParams_MishParams;
    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT *mv::RuntimeModel::buildUPAFloorTask(mv::ComputationModel &cm,
                                                          mv::Element &compilationDescriptor,
                                                          mv::Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    auto softLayerParamsValue = new MVCNN::PostOpsParamsT();

    softLayerParamsValue->nested_params.type = MVCNN::PostOpsNestedParams_FloorParams;
    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT *mv::RuntimeModel::buildUPARoundTask(mv::ComputationModel &cm,
                                                          mv::Element &compilationDescriptor,
                                                          mv::Control::OpListIterator opIt)
{
    MVCNN::RoundMode roundMode;
    const auto mode = opIt->get<std::string>("mode");
    if (mode == "half_to_even") {
        roundMode = MVCNN::RoundMode::RoundMode_HALF_TO_EVEN;
    } else if (mode == "half_away_from_zero") {
        roundMode = MVCNN::RoundMode::RoundMode_HALF_AWAY_FROM_ZERO;
    } else {
        Logger::log(mv::Logger::MessageType::Error, "RuntimeModel",
                    "buildUPARoundTask: unsupported mode: " + mode);
        return nullptr; // failure
    }

    auto* postOpsParamsValue = new MVCNN::RoundParamsT();
    postOpsParamsValue->mode = roundMode;

    auto* softLayerParamsValue = new MVCNN::PostOpsParamsT();
    softLayerParamsValue->nested_params.type = MVCNN::PostOpsNestedParams_RoundParams;
    softLayerParamsValue->nested_params.value = postOpsParamsValue;

    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT *mv::RuntimeModel::buildUPACeilingTask(mv::ComputationModel &cm,
                                                            mv::Element &compilationDescriptor,
                                                            mv::Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    auto softLayerParamsValue = new MVCNN::PostOpsParamsT();

    softLayerParamsValue->nested_params.type = MVCNN::PostOpsNestedParams_CeilingParams;
    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT *mv::RuntimeModel::buildUPAErfTask(mv::ComputationModel &cm,
                                                        mv::Element &compilationDescriptor,
                                                        mv::Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    auto softLayerParamsValue = new MVCNN::PostOpsParamsT();

    softLayerParamsValue->nested_params.type = MVCNN::PostOpsNestedParams_ErfParams;
    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT *mv::RuntimeModel::buildUPAGeluTask(mv::ComputationModel &cm,
                                                        mv::Element &compilationDescriptor,
                                                        mv::Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    auto softLayerParamsValue = new MVCNN::PostOpsParamsT();

    softLayerParamsValue->nested_params.type = MVCNN::PostOpsNestedParams_GeluParams;
    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT *mv::RuntimeModel::buildUPAExpTask(mv::ComputationModel &cm,
                                                        mv::Element &compilationDescriptor,
                                                        mv::Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    auto softLayerParamsValue = new MVCNN::PostOpsParamsT();

    softLayerParamsValue->nested_params.type = MVCNN::PostOpsNestedParams_ExpParams;
    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT *mv::RuntimeModel::buildUPALogTask(mv::ComputationModel &cm,
                                                        mv::Element &compilationDescriptor,
                                                        mv::Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    auto softLayerParamsValue = new MVCNN::PostOpsParamsT();

    softLayerParamsValue->nested_params.type = MVCNN::PostOpsNestedParams_LogParams;
    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT *mv::RuntimeModel::buildUPAConversionTask(mv::ComputationModel &cm, mv::Element &compilationDescriptor, mv::Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_ConvertParams;
    auto softLayerParamsValue = new MVCNN::ConvertParamsT();
    softLayerParamsValue->scale = 1;
    softLayerParamsValue->bias  = 0;

    if (opIt->hasAttr("scale") && opIt->hasAttr("bias"))
    {
        // Use preconfigured scale and bias if such exists
        softLayerParamsValue->scale = opIt->get<double>("scale");
        softLayerParamsValue->bias = opIt->get<int64_t>("bias");
    }
    else
    {
        if (input->getDType() == mv::DType("UInt8") &&
        (output->getDType() == mv::DType("Float16") || output->getDType() == mv::DType("Float32")))
        {
            softLayerParamsValue->scale = input->getQuantParams().getScale()[0];
            softLayerParamsValue->bias  = -input->getQuantParams().getZeroPoint()[0] / input->getQuantParams().getScale()[0];
        }
        else if ((input->getDType() == mv::DType("Float16") || input->getDType() == mv::DType("Float32")) &&
                output->getDType() == mv::DType("UInt8"))
        {
            softLayerParamsValue->scale = 1.0 / output->getQuantParams().getScale()[0];
            softLayerParamsValue->bias  = output->getQuantParams().getZeroPoint()[0];
        }
    }

    // TODO: set DetectionOutput-specific attributes when necessary

    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAReluTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    auto softLayerParamsValue = new MVCNN::PostOpsParamsT();
    softLayerParamsValue->nested_params.type = MVCNN::PostOpsNestedParams_ReluParams;
    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAPreluTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input   = opIt->getInputTensor(mv::IO_TENSOR_INPUT);
    auto weights = opIt->getInputTensor(mv::IO_TENSOR_WEIGHTS_SET);
    auto output  = opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT);

    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    auto softLayerParamsValue = new MVCNN::PostOpsParamsT();

    softLayerParamsValue->nested_params.type = MVCNN::PostOpsNestedParams_PReluParams;
    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, weights));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT *mv::RuntimeModel::buildUPAEluTask(mv::ComputationModel &cm, mv::Element &compilationDescriptor, mv::Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();

    const auto x = opIt->get<double>("alpha");

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    auto softLayerParamsValue = new MVCNN::PostOpsParamsT();

    softLayerParamsValue->nested_params.type = MVCNN::PostOpsNestedParams_EluParams;
    auto postOpsParamsValue = new MVCNN::EluParamsT();
    postOpsParamsValue->x = x;
    softLayerParamsValue->nested_params.value = postOpsParamsValue;

    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPATanhTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    auto softLayerParamsValue = new MVCNN::PostOpsParamsT();

    softLayerParamsValue->nested_params.type = MVCNN::PostOpsNestedParams_TanhParams;
    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT *mv::RuntimeModel::buildUPASoftPlusTask(mv::ComputationModel &cm, mv::Element &compilationDescriptor, mv::Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PostOpsParams;
    auto softLayerParamsValue = new MVCNN::PostOpsParamsT();

    softLayerParamsValue->nested_params.type = MVCNN::PostOpsNestedParams_SoftPlusParams;
    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT *mv::RuntimeModel::buildUPAPadTask(mv::ComputationModel &cm, mv::Element &compilationDescriptor, mv::Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(IO_TENSOR_INPUT);
    auto output = opIt->getOutputTensor(IO_TENSOR_OUTPUT);
    auto toBuild = new MVCNN::UPALayerTaskT();

    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_PadParams;
    auto softLayerParamsValue = new MVCNN::PadParamsT();

    // paddings

    const auto pads_begin = opIt->get<std::array<unsigned short, 4>>("pads_begin");
    const auto pads_end   = opIt->get<std::array<unsigned short, 4>>("pads_end");

    // Convert vectors to uint32_t
    auto pads_begin_uint = std::vector<unsigned int>();
    auto pads_end_uint   = std::vector<unsigned int>();
    for (unsigned i = 0; i < 4; ++i)
    {
        pads_begin_uint.push_back(static_cast<unsigned int>(pads_begin.at(i)));
        pads_end_uint.push_back(static_cast<unsigned int>(pads_end.at(i)));
    }

    softLayerParamsValue->pads_begin = std::vector<unsigned int>(pads_begin_uint.begin(), pads_begin_uint.end());
    softLayerParamsValue->pads_end   = std::vector<unsigned int>(pads_end_uint.begin(), pads_end_uint.end());

   //pad_mode
    auto pad_mode = opIt->get<std::string>("pad_mode");
    if (pad_mode.compare(std::string("constant")) == 0) {
        softLayerParamsValue->pad_mode = MVCNN::PadMode_Constant;
    } else if (pad_mode.compare(std::string("edge")) == 0) {
        softLayerParamsValue->pad_mode = MVCNN::PadMode_Edge;
    } else if (pad_mode.compare(std::string("reflect")) == 0) {
        softLayerParamsValue->pad_mode = MVCNN::PadMode_Reflect;
    } else if (pad_mode.compare(std::string("symmetric")) == 0) {
        softLayerParamsValue->pad_mode = MVCNN::PadMode_Symmetric;
    } else {
        delete toBuild;
        delete softLayerParamsValue;
        throw ArgumentError("buildUPAPadTask", "file:content", "invalid", "Invalid pad_mode for Pad");
    }

    //padValue
    softLayerParamsValue->padValue = static_cast<float>(opIt->get<double>("pad_value"));

    toBuild->softLayerParams.value = softLayerParamsValue;
    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAMVNTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(0);
    auto output = opIt->getOutputTensor(0);
    auto toBuild = new MVCNN::UPALayerTaskT();
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_MVNParams;
    auto softLayerParamsValue = new MVCNN::MVNParamsT();

    softLayerParamsValue->across_channels = opIt->get<bool>("across_channels");
    softLayerParamsValue->normalize_variance = opIt->get<bool>("normalize_variance");
    softLayerParamsValue->eps = opIt->get<double>("eps");

    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPASpaceToDepthTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(mv::IO_TENSOR_INPUT);
    auto output = opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT);
    auto toBuild = new MVCNN::UPALayerTaskT();
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_SpaceToDepthParams;
    auto softLayerParamsValue = new MVCNN::SpaceToDepthParamsT();

    auto mode = opIt->get<std::string>("mode");
    if (mode.compare(std::string("blocks_first")) == 0) {
        softLayerParamsValue->mode = MVCNN::SpaceToDepthMode::SpaceToDepthMode_BLOCKS_FIRST;
    } else if (mode.compare(std::string("depth_first")) == 0) {
        softLayerParamsValue->mode = MVCNN::SpaceToDepthMode::SpaceToDepthMode_DEPTH_FIRST;
    } else {
        delete toBuild;
        delete softLayerParamsValue;
        throw ArgumentError("buildUPAPadTask", "file:content", "invalid", "Invalid mode for SpaceToDepth");
    }

    softLayerParamsValue->blockSize = opIt->get<uint32_t>("block_size");

    toBuild->softLayerParams.value = softLayerParamsValue;

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPACTCGreedyDecoderSeqLenTask(ComputationModel& cm, Element& compilationDescriptor, Control::OpListIterator opIt)
{
    auto toBuild = new MVCNN::UPALayerTaskT();
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_CTCGreedyDecoderSeqLenParams;
    toBuild->maxShaves = 1;

    for (std::size_t i = 0; i < opIt->inputSlots(); ++i) {
        auto input = opIt->getInputTensor(i);
        toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    }

    for (std::size_t i = 0; i < opIt->outputSlots(); ++i) {
        auto output = opIt->getOutputTensor(i);
        toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));
    }

    auto params = new MVCNN::CTCGreedyDecoderSeqLenParamsT();
    params->mergeRepeated = opIt->get<bool>("merge_repeated");

    toBuild->softLayerParams.value = params;

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPADepthToSpaceTask(ComputationModel& cm, Element &compilationDescriptor, Control::OpListIterator opIt)
{
    auto input = opIt->getInputTensor(mv::IO_TENSOR_INPUT);
    auto output = opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT);
    auto toBuild = std::unique_ptr<MVCNN::UPALayerTaskT>(new MVCNN::UPALayerTaskT());
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_DepthToSpaceParams;
    auto softLayerParamsValue = std::unique_ptr<MVCNN::DepthToSpaceParamsT>(new MVCNN::DepthToSpaceParamsT());
    auto mode = opIt->get<std::string>("mode");
    if (mode.compare(std::string("blocks_first")) == 0) {
        softLayerParamsValue->mode = MVCNN::DepthToSpaceMode::DepthToSpaceMode_BLOCKS_FIRST;
    } else if (mode.compare(std::string("depth_first")) == 0) {
        softLayerParamsValue->mode = MVCNN::DepthToSpaceMode::DepthToSpaceMode_DEPTH_FIRST;
    } else {
        throw ArgumentError("buildUPAPadTask", "file:content", "invalid", "Invalid mode for DepthToSpace");
    }

    softLayerParamsValue->blockSize = opIt->get<uint32_t>("block_size");

    toBuild->softLayerParams.value = softLayerParamsValue.release();

    toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));

    toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));

    return toBuild.release();
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAStridedSliceTask(ComputationModel& cm, Element& compilationDescriptor, Control::OpListIterator opIt)
{
    auto toBuild = new MVCNN::UPALayerTaskT();
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_StridedSliceParams;

    for (std::size_t i = 0; i < opIt->inputSlots(); ++i) {
        auto input = opIt->getInputTensor(i);
        toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    }

    for (std::size_t i = 0; i < opIt->outputSlots(); ++i) {
        auto output = opIt->getOutputTensor(i);
        toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));
    }

    auto params = new MVCNN::StridedSliceParamsT();
    params->begins = opIt->get<std::vector<unsigned>>("begins");
    params->ends = opIt->get<std::vector<unsigned>>("ends");
    params->strides = opIt->get<std::vector<unsigned>>("strides");

    toBuild->softLayerParams.value = params;

    return toBuild;
}

// For now 1:1 mapping
std::vector<std::unique_ptr<MVCNN::TaskT>> mv::RuntimeModel::buildUPATask(ComputationModel& cm, mv::Element &compilationDescriptor, Control::OpListIterator opIt)
{
    std::vector<std::unique_ptr<MVCNN::TaskT>> toReturn = std::vector<std::unique_ptr<MVCNN::TaskT>>(1);

    toReturn[0] = std::unique_ptr<MVCNN::TaskT>(new MVCNN::TaskT());
    toReturn[0]->task.type = MVCNN::SpecificTask_UPALayerTask;

    std::string underlyingTask = opIt->get<std::string>("taskOp");
    if(underlyingTask == "Identity")
        toReturn[0]->task.value = buildUPAPassthroughTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Dummy")
        toReturn[0]->task.value = buildUPADummyTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Softmax")
        toReturn[0]->task.value = buildUPASoftmaxTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Sigmoid")
        toReturn[0]->task.value = buildUPASigmoidTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Proposal")
        toReturn[0]->task.value = buildUPAProposalTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "ROIPooling")
        toReturn[0]->task.value = buildUPAROIPoolingTask(cm, compilationDescriptor, opIt);
    else if (underlyingTask == "PSROIPooling")
        toReturn[0]->task.value = buildUPAPSROIPoolingTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Quantize")
        toReturn[0]->task.value = buildUPAQuantizeTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Resample")
        toReturn[0]->task.value = buildUPAResampleTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Reshape")
        toReturn[0]->task.value = buildUPAReshapeTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "RegionYolo")
        toReturn[0]->task.value = buildUPARegionYoloTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "ReorgYolo")
        toReturn[0]->task.value = buildUPAReorgYoloTask(cm, compilationDescriptor, opIt);
    else if (underlyingTask == "Normalize")
        toReturn[0]->task.value = buildUPANormalizeTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Permute")
        toReturn[0]->task.value = buildUPAPermuteTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "PermuteND")
        toReturn[0]->task.value = buildUPAPermuteNDTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Eltwise")
        toReturn[0]->task.value = buildUPAEltwiseFP16Task(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Interp")
        toReturn[0]->task.value = buildUPAInterpTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Norm")
        toReturn[0]->task.value = buildUPANormTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "DetectionOutput")
        toReturn[0]->task.value = buildUPADetectionOutputTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Priorbox")
        toReturn[0]->task.value = buildUPAPriorboxTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Argmax")
        toReturn[0]->task.value = buildUPAArgmaxTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "CustomOcl")
        toReturn[0]->task.value = buildUPACustomOclTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "CustomCpp")
        toReturn[0]->task.value = buildUPACustomCppTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "topK")
        toReturn[0]->task.value = buildUPATopKTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Deconv")
        toReturn[0]->task.value = buildUPADeconvTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Tile")
        toReturn[0]->task.value = buildUPATileTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "CTCDecoder")
        toReturn[0]->task.value = buildUPACTCDecoderTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "RefConv")
        toReturn[0]->task.value = buildUPARefConvTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "FakeQuantize")
        toReturn[0]->task.value = buildUPAFakeQuantizeTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Gather")
        toReturn[0]->task.value = buildUPAGatherTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "HSwish")
        toReturn[0]->task.value = buildUPAHSwishTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Swish")
        toReturn[0]->task.value = buildUPASwishTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Mish")
        toReturn[0]->task.value = buildUPAMishTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Floor")
        toReturn[0]->task.value = buildUPAFloorTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Round")
        toReturn[0]->task.value = buildUPARoundTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Ceiling")
        toReturn[0]->task.value = buildUPACeilingTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Erf")
        toReturn[0]->task.value = buildUPAErfTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Gelu")
        toReturn[0]->task.value = buildUPAGeluTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Exp")
        toReturn[0]->task.value = buildUPAExpTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Log")
        toReturn[0]->task.value = buildUPALogTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Conversion")
        toReturn[0]->task.value = buildUPAConversionTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Relu")
        toReturn[0]->task.value = buildUPAReluTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Clamp")
        toReturn[0]->task.value = buildUPAClampTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Elu")
        toReturn[0]->task.value = buildUPAEluTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Tanh")
        toReturn[0]->task.value = buildUPATanhTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "SoftPlus")
        toReturn[0]->task.value = buildUPASoftPlusTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Pad")
        toReturn[0]->task.value = buildUPAPadTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Interpolate")
        toReturn[0]->task.value = buildUPAInterpolateTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "MVN")
        toReturn[0]->task.value = buildUPAMVNTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "LeakyRelu")
        toReturn[0]->task.value = buildUPALeakyReluTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "SpaceToDepth")
        toReturn[0]->task.value = buildUPASpaceToDepthTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "CTCGreedyDecoderSeqLen")
        toReturn[0]->task.value = buildUPACTCGreedyDecoderSeqLenTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Prelu")
        toReturn[0]->task.value = buildUPAPreluTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "DepthToSpace")
        toReturn[0]->task.value = buildUPADepthToSpaceTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "ReverseSequence")
        toReturn[0]->task.value = buildUPAReverseSequenceTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "StridedSlice")
        toReturn[0]->task.value = buildUPAStridedSliceTask(cm, compilationDescriptor, opIt);

    // TODO: Add other UPA layers

    if(opIt->hasAttr("trailing") && opIt->get<bool>("trailing"))
        static_cast<MVCNN::UPALayerTaskT*>(toReturn[0]->task.value)->isTrailingSWLayer = true;

    return toReturn;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAConcatTask(ComputationModel& cm, Element& compilationDescriptor, Control::OpListIterator opIt)
{
    auto toBuild = new MVCNN::UPALayerTaskT();
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_ConcatParams;

    for (std::size_t i = 0; i < opIt->inputSlots(); ++i) {
        auto input = opIt->getInputTensor(i);
        toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    }

    for (std::size_t i = 0; i < opIt->outputSlots(); ++i) {
        auto output = opIt->getOutputTensor(i);
        toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));
    }

    auto params = new MVCNN::ConcatParamsT;
    auto axisToConcat = opIt->get<std::string>("axis");
    auto numericAxisToConcat = mv::Shape::getAxis(axisToConcat);
    params->axis = numericAxisToConcat;

    toBuild->softLayerParams.value = params;

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPACopyTask(ComputationModel& cm, Element& compilationDescriptor, Control::OpListIterator opIt)
{
    auto toBuild = new MVCNN::UPALayerTaskT();
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_CopyParams;

    for (std::size_t i = 0; i < opIt->inputSlots(); ++i) {
        auto input = opIt->getInputTensor(i);
        toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    }

    for (std::size_t i = 0; i < opIt->outputSlots(); ++i) {
        auto output = opIt->getOutputTensor(i);
        toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));
    }

    auto params = new MVCNN::CopyParamsT();

    toBuild->softLayerParams.value = params;

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPACropTask(ComputationModel& cm, Element& compilationDescriptor, Control::OpListIterator opIt)
{
    auto toBuild = new MVCNN::UPALayerTaskT();
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_CropParams;

    for (std::size_t i = 0; i < opIt->inputSlots(); ++i) {
        auto input = opIt->getInputTensor(i);
        toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    }

    for (std::size_t i = 0; i < opIt->outputSlots(); ++i) {
        auto output = opIt->getOutputTensor(i);
        toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));
    }

    auto params = new MVCNN::CropParamsT();
    params->cropVal = opIt->get<std::uint32_t>("cropVal");
    params->dimension = opIt->get<std::uint32_t>("dimension");

    toBuild->softLayerParams.value = params;

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPASliceTask(ComputationModel& cm, Element& compilationDescriptor, Control::OpListIterator opIt)
{
    auto toBuild = new MVCNN::UPALayerTaskT();
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_SliceParams;

    for (std::size_t i = 0; i < opIt->inputSlots(); ++i) {
        auto input = opIt->getInputTensor(i);
        toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    }

    for (std::size_t i = 0; i < opIt->outputSlots(); ++i) {
        auto output = opIt->getOutputTensor(i);
        toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));
    }

    auto params = new MVCNN::SliceParamsT();
    params->begin = opIt->get<mv::Shape>("begin");
    params->size = opIt->get<mv::Shape>("size");

    toBuild->softLayerParams.value = params;

    return toBuild;
}

MVCNN::UPALayerTaskT * mv::RuntimeModel::buildUPAAlignTask(ComputationModel& cm, Element& compilationDescriptor, Control::OpListIterator opIt)
{
    auto toBuild = new MVCNN::UPALayerTaskT();
    toBuild->softLayerParams.type = MVCNN::SoftwareLayerParams_AlignParams;

    for (std::size_t i = 0; i < opIt->inputSlots(); ++i) {
        auto input = opIt->getInputTensor(i);
        toBuild->inputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, input));
    }

    for (std::size_t i = 0; i < opIt->outputSlots(); ++i) {
        auto output = opIt->getOutputTensor(i);
        toBuild->outputs.push_back(buildTensorReferenceT(cm, compilationDescriptor, output));
    }

    auto params = new MVCNN::AlignParamsT();
    params->dimension = opIt->get<std::size_t>("dimension");
    params->pad = opIt->get<std::size_t>("pad");

    toBuild->softLayerParams.value = params;

    return toBuild;
}

std::vector<std::unique_ptr<MVCNN::TaskT>> mv::RuntimeModel::buildImplicitTask(ComputationModel& cm, mv::Element& compilationDescriptor, Control::OpListIterator& opIt)
{
    std::vector<std::unique_ptr<MVCNN::TaskT>> toReturn = std::vector<std::unique_ptr<MVCNN::TaskT>>(1);

    toReturn[0] = std::unique_ptr<MVCNN::TaskT>(new MVCNN::TaskT());
    toReturn[0]->task.type = MVCNN::SpecificTask_UPALayerTask;

    std::string underlyingTask = opIt->getOpType();
    if(underlyingTask == "Concat")
        toReturn[0]->task.value = buildUPAConcatTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Copy")
        toReturn[0]->task.value = buildUPACopyTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Crop")
        toReturn[0]->task.value = buildUPACropTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Slice")
        toReturn[0]->task.value = buildUPASliceTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "Align")
        toReturn[0]->task.value = buildUPAAlignTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "ImplicitConcat")
        toReturn[0]->task.value = buildUPAConcatTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "ImplicitPermute")
    {
        mv::OpModel om(cm);
        mv::DataModel dm(cm);
        mv::ControlModel controlModel(cm);

        const bool software =
            opIt->hasAttr("softwareExecuted") && opIt->get<bool>("softwareExecuted");

        const auto name = opIt->getName() + "_upa";
        const auto& attrsToCopy = opIt->getAttrs();
        const auto& inputs = opIt->getInputTensor();
        const auto& outputTensor = opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT);
        const auto quantParams = outputTensor->getQuantParams();
        const auto dType = outputTensor->getDType();
        const auto order = outputTensor->getOrder();

        auto upaPermute = mv::convertPermuteToUPATask(om, inputs, attrsToCopy, name, software, quantParams, dType, order);

        auto sourceTensor = opIt->getInputTensor(mv::IO_TENSOR_INPUT);
        auto parentOpIt = om.getSourceOp(sourceTensor);

        mv::linkNewOperationsReplacement(parentOpIt, upaPermute, om, dm.switchContext(opIt));
        opIt = controlModel.switchContext(om.getOp(name));
        toReturn[0]->task.value = buildUPAPermuteTask(cm, compilationDescriptor, opIt);
    }
    else if(underlyingTask == "ImplicitResample")
        toReturn[0]->task.value = buildUPAResampleTask(cm, compilationDescriptor, opIt);
    else if(underlyingTask == "ImplicitReshape")
        toReturn[0]->task.value = buildUPAReshapeTask(cm, compilationDescriptor, opIt);

    return toReturn;
}

std::vector<std::unique_ptr<MVCNN::TaskT>> mv::RuntimeModel::buildControllerTaskT(ComputationModel& /*cm*/, mv::Element& /*compilationDescriptor*/, Control::OpListIterator /*opIt*/)
{
    std::vector<std::unique_ptr<MVCNN::TaskT>> toReturn = std::vector<std::unique_ptr<MVCNN::TaskT>>(1);
    return toReturn;
}

std::unique_ptr<MVCNN::BarrierReferenceT> mv::RuntimeModel::buildBarrierReferenceT(ComputationModel& , Element& , BarrierDependencies dep)
{
    std::unique_ptr<MVCNN::BarrierReferenceT> toBuild = std::unique_ptr<MVCNN::BarrierReferenceT>(new MVCNN::BarrierReferenceT());
    if (dep.hasWaitBarriers()) {
      toBuild->wait_barriers = dep.getWait();
      toBuild->virtual_wait_barriers = dep.getWait();
    }
    toBuild->update_barriers = dep.getUpdate();
    toBuild->virtual_update_barriers = dep.getUpdate();
    return toBuild;
}

std::unique_ptr<MVCNN::BarrierReferenceT> mv::RuntimeModel::buildBarrierReferenceT()
{
    std::unique_ptr<MVCNN::BarrierReferenceT> toBuild = std::unique_ptr<MVCNN::BarrierReferenceT>(new MVCNN::BarrierReferenceT());
    return toBuild;
}

std::vector<std::unique_ptr<MVCNN::TaskT>> mv::RuntimeModel::buildTaskT(ComputationModel& cm, mv::Element &compilationDescriptor, Control::OpListIterator opIt, uint8_t* port)
{
    std::vector<std::unique_ptr<MVCNN::TaskT>> vecToBuild = buildSpecificTaskUnion(cm, compilationDescriptor, opIt, port);

    for(auto& toBuild: vecToBuild)
    {
        toBuild->name = opIt->getName();

        // NOTE: This might change in the future
        if(opIt->hasAttr("opId"))
            toBuild->sourceTaskIDs = {opIt->get<unsigned>("opId")};

        if(opIt->hasAttr("BarrierDeps"))
            toBuild->associated_barriers = buildBarrierReferenceT(cm, compilationDescriptor, opIt->get<mv::BarrierDependencies>("BarrierDeps"));
        else
            toBuild->associated_barriers = buildBarrierReferenceT();
    }

    return vecToBuild;
}

unsigned mv::RuntimeModel::countProducerConsumerTasks(mv::ComputationModel& cm, mv::Control::OpListIterator opIt, bool trimEmptyTensors)
{
    std::string taskType = opIt->getOpType();
    int64_t toReturn = 0;
    unsigned numClusters = cm.getGlobalConfigParams()->get<int>("Number_of_Clusters");

    if(taskType == "DPUTask")
    {
        if(opIt->hasAttr("splitStrategy"))
        {
            std::string strategy = opIt->get<std::string>("splitStrategy");
            if(strategy != "Clustering")
            {
                for(unsigned i = 0; i < numClusters; ++i)
                {
                    if (opIt->hasAttr("Workloads" + std::to_string(i)))
                    {
                        auto& workloads = opIt->get<mv::Workloads>("Workloads" + std::to_string(i));
                        toReturn += workloads.nWorkloads();
                    }
                    else
                        toReturn = numClusters;
                }
            }
            else
            {
                auto& workloads = opIt->get<mv::Workloads>("Workloads0");
                toReturn = workloads.nWorkloads() * numClusters;
            }
        }
    }
    else if(taskType == "DMATask")
    {
        auto inputTensor = opIt->getInputTensor(0);
        // Weights sparsity new approach: it doesn't matter what strategy is chosen,
        // the number of dma is doubled since the strategy is shared between weights
        // and sparsity map. Techinically the isPopulated() check is not needed
        // because we never transfer sparse activation tensors.
        unsigned multiplicator = 1;
        if(inputTensor->isPopulated() && inputTensor->isSparse())
            multiplicator = 2;
        else if (inputTensor->isSparse()) {
            multiplicator = 3;
        }

        // For sparse cases when the tensor is empty
        // we are better off to schedule DMA's just for the
        // sparse map and pointer table.
        // Providing runtiem with a DMA of size 0 to schedule
        // will end up halting the DMA HW.
        size_t empty_tensors = 0;
        if(numClusters > 1)
        {
            bool sourceIsBroadCasted = inputTensor->isBroadcasted();
            //NOTE: When strategy is overwritten
            if (opIt->get<mv::DmaDirection>("direction") == mv::DmaDirectionEnum::DDR2NNCMX)
            {
                if (inputTensor->hasAttr("overwriteStrategy"))
                {
                    if (inputTensor->get<std::string>("overwriteStrategy") == "ClusteringToSoH")
                        sourceIsBroadCasted = false;
                    else if (inputTensor->get<std::string>("overwriteStrategy") == "SoHToClustering")
                        sourceIsBroadCasted = true;
                }
            }
            else if (opIt->get<mv::DmaDirection>("direction") == mv::DmaDirectionEnum::NNCMX2DDR)
            {
                auto outputTensor = opIt->getOutputTensor(0);
                 if (outputTensor->hasAttr("overwriteStrategy"))
                 {
                     if (outputTensor->get<std::string>("overwriteStrategy") == "ClusteringToSoH")
                         sourceIsBroadCasted = true;
                     else if (outputTensor->get<std::string>("overwriteStrategy") == "SoHToClustering")
                         sourceIsBroadCasted = false;
                 }
            }

            if(!sourceIsBroadCasted)
                toReturn = numClusters;
            else
                toReturn = 1;
            if ((inputTensor->get<std::string>("splitStrategy") == "Clustering"))
                toReturn = 1;

            if (inputTensor->isPopulated() && inputTensor->isSparse())
            {
                if (inputTensor->isAllocatedPerCluster() && inputTensor->hasSubTensors()) {
                    for (size_t i = 0; i < inputTensor->numSubTensors(); ++i)
                        if (inputTensor->getSubTensor(i).dataPackedSize() == 0)
                            empty_tensors++;
                } else {
                    if (inputTensor->dataPackedSize() == 0)
                    {
                        empty_tensors++;
                    }
                }
            }
        }
        else
        {
            toReturn = 1;
            if (inputTensor->isPopulated() && inputTensor->isSparse() &&
                inputTensor->dataPackedSize() == 0)
                empty_tensors++;
        }

        toReturn *= multiplicator;
        if (trimEmptyTensors)
            toReturn -= empty_tensors;
    }
    else if(taskType == "UPATask" || opIt->isImplicit())
        toReturn = 1;

    if (toReturn < 0){
        throw mv::ArgumentError(cm, "countProducerConsumerTasks", "0", "Sub zero barrier count");
    }

    return toReturn;
}

std::unique_ptr<MVCNN::BarrierT> mv::RuntimeModel::buildBarrierT(mv::ComputationModel& model, mv::Element& , mv::Control::OpListIterator opIt)
{
    mv::ControlModel cm(model);
    auto globalParams = model.getGlobalConfigParams();
    bool isStatic= false;
    if (globalParams->hasAttr("enableStaticBarriers"))
    {
      isStatic= globalParams->get<bool>("enableStaticBarriers");
    }
    std::unique_ptr<MVCNN::BarrierT> toBuild = std::unique_ptr<MVCNN::BarrierT>(new MVCNN::BarrierT());
    auto barrier = opIt->get<mv::Barrier>("Barrier");

    if (isStatic){
      toBuild->barrier_id = barrier.getRealBarrierIndex();
    }else{
      toBuild->barrier_id = barrier.getIndex();
    }
    toBuild->consumer_count = 0;
    toBuild->producer_count = 0;

    for(auto producer = opIt.leftmostParent(); producer != cm.opEnd(); ++producer) {
        toBuild->producer_count += countProducerConsumerTasks(model, producer, true);
    }

    for(auto consumer = opIt.leftmostChild(); consumer != cm.opEnd(); ++consumer) {
        toBuild->consumer_count += countProducerConsumerTasks(model, consumer, true);
    }
    return toBuild;
}

std::vector<std::unique_ptr<MVCNN::BarrierT>> mv::RuntimeModel::buildBarrierTable(mv::ComputationModel& cm, mv::Element& compilationDescriptor)
{
    mv::OpModel om(cm);
    mv::ControlModel controlModel(cm);

    auto barrierTasks = om.getOps("BarrierTask");
    std::sort(
        barrierTasks.begin(),
        barrierTasks.end(),
        [](const mv::Data::OpListIterator& a, const mv::Data::OpListIterator& b) -> bool { return a->get<mv::Barrier>("Barrier").getIndex() < b->get<mv::Barrier>("Barrier").getIndex(); }
        );

    unsigned n = barrierTasks.size();
    std::vector<std::unique_ptr<MVCNN::BarrierT>> toBuild = std::vector<std::unique_ptr<MVCNN::BarrierT>>(n);
    for(unsigned i = 0; i < n; ++i)
        toBuild[i] = buildBarrierT(cm, compilationDescriptor, controlModel.switchContext(barrierTasks[i]));
    return toBuild;
}

void mv::RuntimeModel::buildHeader(ComputationModel &cm, Element &compilationDescriptor)
{
    //HEADER
    graphFile_.header = buildSummaryHeaderMetaInformations(cm, compilationDescriptor);
}

void mv::RuntimeModel::buildGraphFile(ComputationModel& cm, const mv::TargetDescriptor& td, mv::Element& compilationDescriptor)
{
    mv::OpModel om(cm);
    mv::DataModel dm(cm);

    unsigned numClusters = dm.getGlobalConfigParams()->get<int>("Number_of_Clusters");

    auto globalConfigurationParameters = cm.getGlobalConfigParams();
    auto compression = globalConfigurationParameters->get<bool>("HuffmanCompression");
    bool hasCSRAM = (globalConfigurationParameters->hasAttr("csramLimit") &&
                        globalConfigurationParameters->get<int>("csramLimit") != 0);

    graphFile_.header = buildSummaryHeaderT(cm, td, compilationDescriptor, std::move(graphFile_.header));

    // Binary Data
    graphFile_.binary_data = std::vector<std::unique_ptr<MVCNN::BinaryDataT>>();
    std::unordered_set<Tensor *> csramCacheable;
    std::vector<Tensor *> toSort;
    for(auto opIterator = om.opBegin(); opIterator != om.opEnd(); ++opIterator)
    {
        std::string opType = opIterator->getOpType();
        if (opIterator->hasAttr("toIgnore"))
            continue;
        if (opType == "Constant" || opType == "ConstantInt" || opType == "ConstantDataElement")
        {
            auto tIt = opIterator->getOutputTensor(0);
            if (tIt->hasAttr("toIgnore") && tIt->get<bool>("toIgnore"))
                continue;

            // Weights sparsity new approach: there is a separate constant for
            // each cluster
            if(tIt->isSparse())
            {
                auto sparsityMapIterator = dm.getTensor(tIt->getSparsityMap()->getName());
                toSort.push_back(&(*sparsityMapIterator));
                // avoid to save empty tensor in the blob
                if(tIt->get<std::string>("splitStrategy") == "SplitOverK")
                {
                    for(size_t i = 0; i < numClusters; ++i)
                        if(tIt->getSubTensor(i).dataPackedSize())
                            toSort.push_back(&(tIt->getSubTensor(i)));
                }
                else if(tIt->dataPackedSize())
                    toSort.push_back(&(*tIt));
            }
            else if(tIt->isAllocatedPerCluster())
                for(size_t i = 0; i < numClusters; ++i)
                    toSort.push_back(&(tIt->getSubTensor(i)));
            else
                toSort.push_back(&(*tIt));

        }
        else if (opType == "DMATask")
        {
            // Add inputs to NNDMA operations to the cacheable tensor set.
            // NNDMA tasks are DMATasks whose direction does not involve UPA.
            //
            // N.B. This loop will add all tensors that are inputs to NNDMA operations to the cacheable set,
            // including program inputs and spilled tensors.  This is fine; the cacheable info will only be
            // accessed for tensors that we're actually adding to the output.
            auto direction = opIterator->get<mv::DmaDirection>("direction");
            if (hasCSRAM &&
                direction != mv::DmaDirectionEnum::NNCMX2UPACMX &&
                direction != mv::DmaDirectionEnum::UPACMX2NNCMX &&
                direction != mv::DmaDirectionEnum::DDR2UPACMX   &&
                direction != mv::DmaDirectionEnum::UPACMX2DDR)
            {
                for (auto& tIt : opIterator->getInputTensor())
                {
                    csramCacheable.insert(&*tIt);
                    if(tIt->isSparse())
                    {
                        auto sparsityMapIterator = dm.getTensor(tIt->getSparsityMap()->getName());
                        csramCacheable.insert(&(*sparsityMapIterator));
                        if(tIt->get<std::string>("splitStrategy") == "SplitOverK")
                        {
                            for(size_t i = 0; i < numClusters; ++i)
                                csramCacheable.insert(&(tIt->getSubTensor(i)));
                        }
                    }
                    else if(tIt->isAllocatedPerCluster())
                        for(size_t i = 0; i < numClusters; ++i)
                            csramCacheable.insert(&(tIt->getSubTensor(i)));
                }
            }
        }
    }

    std::sort(toSort.begin(), toSort.end(), [](mv::Tensor * t1, mv::Tensor * t2){return (t1->get<unsigned>("graphFileIndex") < t2->get<unsigned>("graphFileIndex"));});
    for(auto& tIt : toSort)
    {
        graphFile_.binary_data.push_back(buildBinaryDataT(cm, compilationDescriptor, *tIt, compression, csramCacheable.count(tIt) != 0));
    }
    // TASKS
    graphFile_.task_lists = buildTaskListT(cm, compilationDescriptor);

    // Barrier Table must be build only on dynamic scheduling
    if(!targetEmulator_(compilationDescriptor) && !globalConfigurationParameters->get<bool>("enableStaticBarriers"))
        graphFile_.barrier_table = buildBarrierTable(cm, compilationDescriptor);
}

void mv::RuntimeModel::serialize(size_t weight_alignment)
{
    flatbuffers::FlatBufferBuilder fbb;

    // N.B. In order to build the embedded weight vectors with the correct
    // alignment (which slightly reduces first-run latency, as the runtime
    // doesn't need to re-align the weights), we unpack and serialize the
    // GraphFile components ourselves, instead of using MVCNN::CreateGraphFile()
    // directly on graphFile_.
    //
    // Note that flatbuffers are built back-to-front, which is why it looks like
    // we're building things in reverse.

    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<MVCNN::BinaryData>>> binary_data_offset = 0;
    {
        std::vector<flatbuffers::Offset<MVCNN::BinaryData>> binary_datas;
        binary_datas.reserve(graphFile_.binary_data.size());
        for (auto it = graphFile_.binary_data.rbegin(); it != graphFile_.binary_data.rend(); ++it) {
            auto& binary_data = *it;
            fbb.ForceVectorAlignment(binary_data->data.size(), sizeof(binary_data->data[0]), weight_alignment);
            auto data_offset = fbb.CreateVector(binary_data->data);
            MVCNN::BinaryDataBuilder bdb(fbb);
            bdb.add_underlying_type(binary_data->underlying_type);
            bdb.add_csram_cacheable(binary_data->csram_cacheable);
            bdb.add_length(binary_data->length);
            bdb.add_data(data_offset);
            binary_datas.emplace_back(bdb.Finish());
        }
        reverse(binary_datas.begin(), binary_datas.end());
        binary_data_offset = fbb.CreateVector(binary_datas);
    }

    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<MVCNN::TaskList>>> task_lists_offset = 0;
    {
        std::vector<flatbuffers::Offset<MVCNN::TaskList>> task_lists;
        task_lists.reserve(graphFile_.task_lists.size());
        for (auto it = graphFile_.task_lists.rbegin(); it != graphFile_.task_lists.rend(); ++it) {
            task_lists.emplace_back(MVCNN::CreateTaskList(fbb, it->get()));
        }
        reverse(task_lists.begin(), task_lists.end());
        task_lists_offset = fbb.CreateVector(task_lists);
    }

    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<MVCNN::Barrier>>> barrier_table_offset = 0;
    {
        std::vector<flatbuffers::Offset<MVCNN::Barrier>> barriers;
        barriers.reserve(graphFile_.barrier_table.size());
        for (auto it = graphFile_.barrier_table.rbegin(); it != graphFile_.barrier_table.rend(); ++it) {
            barriers.emplace_back(MVCNN::CreateBarrier(fbb, it->get()));
        }
        reverse(barriers.begin(), barriers.end());
        barrier_table_offset = fbb.CreateVector(barriers);
    }

    flatbuffers::Offset<MVCNN::SummaryHeader> header = 0;
    if (graphFile_.header) {
        header = MVCNN::CreateSummaryHeader(fbb, graphFile_.header.get());
    }

    MVCNN::GraphFileBuilder gfb(fbb);
    gfb.add_header(header);
    gfb.add_barrier_table(barrier_table_offset);
    gfb.add_task_lists(task_lists_offset);
    gfb.add_binary_data(binary_data_offset);
    auto graphfile_offset = gfb.Finish();

    MVCNN::FinishGraphFileBuffer(fbb, graphfile_offset);

    binaryData_ = std::make_shared<std::vector<char>>(fbb.GetSize());
    std::memcpy(binaryData_->data(), (char*)fbb.GetBufferPointer(), binaryData_->size());
}

void mv::RuntimeModel::serialize(const std::string& filename, size_t weight_alignment)
{
    serialize(weight_alignment);
    if (flatbuffers::SaveFile((filename).c_str(), binaryData_->data(), binaryData_->size(), true))
        Logger::log(mv::Logger::MessageType::Debug, "RuntimeModel", "File successfully written to: " + filename);
    else
        Logger::log(mv::Logger::MessageType::Error, "RuntimeModel", "File was not created. Check configuration");
}

void mv::RuntimeModel::deserialize(const std::string& path)
{
    std::ifstream ifs(path.c_str(), std::ifstream::binary|std::ifstream::in);
    ifs.seekg(0, std::ios::end);
    unsigned length = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    binaryData_ = std::shared_ptr<std::vector<char>>(new std::vector<char>(length));

    ifs.read(binaryData_->data(), length);
    ifs.close();
    deserialize(binaryData_->data(), binaryData_->size());
}

void mv::RuntimeModel::deserialize(const char *dataBuffer, int length)
{
    flatbuffers::Verifier verifier(reinterpret_cast<const unsigned char*>(dataBuffer), length);
    if (!MVCNN::VerifyGraphFileBuffer(verifier))
        throw ArgumentError("tools:GraphComparator", "file:content", "invalid", "GraphFile verification failed");
    Logger::log(mv::Logger::MessageType::Debug, "RuntimeModel", "GraphFile verification successful");
    const MVCNN::GraphFile *graphPtr = MVCNN::GetGraphFile(dataBuffer);
    graphPtr->UnPackTo(&graphFile_);
}

std::shared_ptr<std::vector<char>> mv::RuntimeModel::getBlob()
{
    if(nullptr == binaryData_)
        serialize();
    return binaryData_;
}

void mv::RuntimeModel::serializeHelper(const std::function<void(MVCNN::GraphFileT&)>& serializeCallback) {
    serializeCallback(graphFile_);
}

const MVCNN::GraphFileT& mv::RuntimeModel::getGraphFile()
{
    return graphFile_;
}

void mv::RuntimeModel::clear()
{
    graphFile_ = MVCNN::GraphFileT();
    binaryData_->clear();
}

mv::Order mv::RuntimeModel::stridesToOrder(std::vector<float> strides, std::vector<unsigned> dims)
{

    std::vector<std::size_t> contVector;
    std::map<float, unsigned> indices;

    // Hardcoded for 3d tensors
    // TODO update when 3d RT ops enabled
    auto current_idx = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        // if stride not already in map, add it
        float unique_stride = strides[i] + float(i) / 100;
        if (indices.find(unique_stride) == indices.end())
            indices[unique_stride] = current_idx++;
    }

    for (auto it = indices.begin(); it != indices.end(); ++it)
        contVector.push_back(it->second);

    auto order = OrderRegistry::instance().getLabel(contVector);

    // Check for non-standard order & replace with standard order if tensor dimension is flat
    if (order == "NHCW" && dims[mv::IO_CHANNEL_DIMENSION] == 1)
        order = "NCHW";

    return order;
}
