#include "include/mcm/pass/pass_registry.hpp"
#include "include/mcm/op_model.hpp"
#include "include/mcm/computation/model/data_model.hpp"
#include "include/mcm/utils/custom_math.hpp"
#include "include/mcm/pass/pass_utils.hpp"

const size_t FULLY_CONNECTED_KERNEL = 1;

static void fullyConnectedAsConv2DFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model);
static void tensorsToFP16Fcn(const mv::pass::PassEntry& , mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);
static void tensorsToU8Fcn(const mv::pass::PassEntry& , mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);
static void averageAsDepthWiseFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model);
static void replacementOpsFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);
static void scaleAsDepthwiseFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model);

namespace mv
{

    namespace pass
    {

        MV_REGISTER_PASS(TensorsToFP16)
        .setFunc(tensorsToFP16Fcn)
        .setDescription(
            "Replaces full precision tensors with FP16 tensors"
        );

        MV_REGISTER_PASS(TensorsToU8)
        .setFunc(tensorsToU8Fcn)
        .setDescription(
            "Replaces quantized int8 tensors with U8 tensors"
        );

        MV_REGISTER_PASS(ReplacementOps)
        .setFunc(replacementOpsFcn)
        .setDescription(
            "Replaces Average with Depthwise and FullyConnected with Convolution"
        );

    }

}


mv::Data::OpListIterator linkNewOperationsReplacementFully(mv::Data::OpListIterator parentOpIt,
                        mv::Data::TensorIterator sourceTensor, mv::OpModel om, mv::Data::OpListIterator opIt)
{
    //Important: do not change the order of this ops
    std::vector<mv::Data::OpListIterator> opsToLink;
    std::vector<std::size_t> inputSlots;
    for (auto sinkFlow = opIt.leftmostOutput(); sinkFlow != om.flowEnd(); ++sinkFlow)
    {
        opsToLink.push_back(sinkFlow.sink());
        inputSlots.push_back(sinkFlow->get<std::size_t>("sinkInput"));
    }

    auto paramOp = opIt.leftmostParent();
    while(paramOp != om.opEnd())
    {
        if (paramOp->getOpType() == "Constant" || paramOp->getOpType() == "ConstantInt"
                || paramOp->getOpType() == "ConstantDataElement")
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
        opsToLink[j]->setInputTensor(sourceTensor, inputSlots[j]);
        om.defineFlow(sourceTensor, opsToLink[j], inputSlots[j]);
    }

    return opIt;
}

mv::Data::OpListIterator linkNewOperationsReplacementAverage(mv::Data::OpListIterator parentOpIt,
                            mv::Data::TensorIterator sourceTensor, mv::OpModel om, mv::Data::OpListIterator opIt)
{
    //Important: do not change the order of this ops
    bool cascading = false;
    std::vector<mv::Data::OpListIterator> opsToLink;
    std::vector<std::size_t> outputSlots;
    for (auto sinkFlow = opIt.leftmostOutput(); sinkFlow != om.flowEnd(); ++sinkFlow)
    {
        opsToLink.push_back(sinkFlow.sink());
        outputSlots.push_back(sinkFlow->get<std::size_t>("sinkInput"));
    }

    om.removeOp(opIt);
    opIt = parentOpIt;
    //Average pool to depthwise will never change the shape of the output tensor, no cascade
    for (unsigned j = 0; j < opsToLink.size(); ++j)
    {
        opsToLink[j]->setInputTensor(sourceTensor, outputSlots[j], cascading);
        om.defineFlow(sourceTensor, opsToLink[j], outputSlots[j]);
    }
    return opIt;
}

mv::Data::OpListIterator linkNewOperationsReplacementScale(mv::Data::OpListIterator parentOpIt,
                mv::Data::TensorIterator sourceTensor, mv::OpModel om, mv::Data::OpListIterator opIt, bool removeCostantOps)
{
    //Important: do not change the order of this ops
    std::vector<mv::Data::OpListIterator> opsToLink;
    std::vector<std::size_t> inputSlots;
    for (auto sinkFlow = opIt.leftmostOutput(); sinkFlow != om.flowEnd(); ++sinkFlow)
    {
        opsToLink.push_back(sinkFlow.sink());
        inputSlots.push_back(sinkFlow->get<std::size_t>("sinkInput"));
    }

    auto inputFlow = opIt.leftmostInput();
    while(inputFlow != om.flowEnd())
    {
        auto paramOp = inputFlow.sink();
        auto backup = inputFlow;
        ++inputFlow;
        om.undefineFlow(backup);
        if(removeCostantOps)
            if (paramOp->getOpType() == "Constant" || paramOp->getOpType() == "ConstantInt" ||
                    paramOp->getOpType() == "ConstantDataElement")
                om.removeOp(paramOp);
    }

    om.removeOp(opIt);
    opIt = parentOpIt;

    for (unsigned j = 0; j < opsToLink.size(); ++j)
    {
        opsToLink[j]->setInputTensor(sourceTensor, inputSlots[j]);
        om.defineFlow(sourceTensor, opsToLink[j], inputSlots[j]);
    }

    return opIt;
}

void tensorsToFP16Fcn(const mv::pass::PassEntry&, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&)
{
    using namespace mv;
    OpModel om(model);

    auto kernelOp = om.getInput();
    while (kernelOp != om.opEnd())
    {
        if(kernelOp.outputsSize() > 0)
        {
            auto outputTensor = kernelOp->getOutputTensor(0);
            if(outputTensor->get<mv::DType>("dType") == mv::DType("Float64") ||
               outputTensor->get<mv::DType>("dType") == mv::DType("Float32"))
            {
                auto opId = kernelOp->get<unsigned>("opId");
                if (outputTensor->isPopulated())
                {
                    std::vector<double> oldData = kernelOp->getOutputTensor(0)->getDoubleData();
                    std::vector<int64_t> newData(oldData.size());
                    mv::QuantizationParams quantParams = {{},{},{},{}};
                    if(outputTensor->hasAttr("quantParams"))
                        quantParams = outputTensor->get<mv::QuantizationParams>("quantParams");

                    for(unsigned i = 0; i < oldData.size(); ++i)
                        newData[i] = mv::fp32_to_fp16(oldData[i]);
                    auto kernelShape = kernelOp->getOutputTensor(0)->getShape();
                    auto kernelOrder = kernelOp->getOutputTensor(0)->getOrder();
                    //with data flows I am finding where the op was attached to attache the new one!!!
                    auto outputDataFlows = mv::getOutputDataFlow(om, kernelOp);

                    auto newKernel = om.constantInt(newData, kernelShape, mv::DType("Float16"), kernelOrder, quantParams);
                    auto newKernelOp = om.getSourceOp(newKernel);
                    newKernelOp->set<unsigned>("opId", opId);
                    newKernelOp->set<mv::DType>("dType",  mv::DType("Float16"));
                    mv::setOutputDataFlow(om, newKernel, outputDataFlows);
                }
                else
                {
                    mv::DType newType = mv::DType("Float16");
                    outputTensor->setDType(newType);
                    kernelOp->set<mv::DType>("dType",  mv::DType("Float16"));
                    ++kernelOp;
                }
            }
            else
                ++kernelOp;
        }
        else
            ++kernelOp;
    }
}

// Pass logic:
// Runtime will handle the input, we uniform all the rest to UInt8
void tensorsToU8Fcn(const mv::pass::PassEntry&  , mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&)
{
    mv::OpModel om(model);

    int64_t zeroPointShift = 128;
    auto sourceDType = mv::DType("Int8");
    auto targetDType = mv::DType("UInt8");

    auto kernelOp = om.getInput();
    auto inputType = kernelOp->getOutputTensor(0)->getDType();
    if(inputType == mv::DType("Int8"))
        throw std::runtime_error("Compiler doesn't support I8 inputs for the moment, please rescale your data to U8");

    for (; kernelOp != om.opEnd(); ++kernelOp)
    {
        if(kernelOp.outputsSize() > 0)
        {
            auto outputTensor = kernelOp->getOutputTensor(0);
            auto outputTensorDType = outputTensor->get<mv::DType>("dType");
            if(outputTensorDType == sourceDType)
            {
                mv::DType newType = targetDType;
                auto quantParams = outputTensor->get<mv::QuantizationParams>("quantParams");
                auto quantParamsZp = quantParams.getZeroPoint();
                for(auto& zp: quantParamsZp)
                    zp += zeroPointShift;
                quantParams = mv::QuantizationParams(quantParamsZp, quantParams.getScale(),{},{});
                outputTensor->setDType(newType);
                kernelOp->set<mv::DType>("dType",  newType);
                outputTensor->set<mv::QuantizationParams>("quantParams", quantParams);
                kernelOp->set<mv::QuantizationParams>("quantParams", quantParams);
                if (outputTensor->isPopulated())
                    for(unsigned i = 0; i < outputTensor->size(); ++i)
                        outputTensor->at(i) += zeroPointShift;
            }
        }
    }
}

void replacementOpsFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&)
{
    fullyConnectedAsConv2DFcn(pass, model);
    averageAsDepthWiseFcn(pass, model);
    scaleAsDepthwiseFcn(pass, model);
}

void fullyConnectedAsConv2DFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model)
{

    MV_PROFILED_FUNCTION(MV_PROFILE_PASS)
    using namespace mv;

    OpModel om(model);

    auto fullyConnectedOps = om.getOps("FullyConnected");

    for (auto& opIt : fullyConnectedOps)
    {
        auto outputMemoryLocation = opIt->getOutputTensor(0)->get<mv::Tensor::MemoryLocation>("Location");

        pass.log(Logger::MessageType::Debug, "Found FullyConnected op " + opIt->getName());

        auto sourceTensor = opIt->getInputTensor(0);
        auto parentOpIt = om.getSourceOp(sourceTensor);
        auto weightsData = opIt->getInputTensor(1)->getData();
        auto inputShape = sourceTensor->getShape();
        mv::QuantizationParams weightsTensorQuantizationParams = {{},{},{},{}};
        mv::QuantizationParams outputTensorQuantizationParams = {{},{},{},{}};

        if (opIt->getInputTensor(1)->isQuantized())
        {
            weightsTensorQuantizationParams = opIt->getInputTensor(1)->get<mv::QuantizationParams>("quantParams");
            outputTensorQuantizationParams = opIt->getOutputTensor(0)->get<mv::QuantizationParams>("quantParams");
        }
        auto weights = om.constantDataElement(weightsData, {FULLY_CONNECTED_KERNEL, FULLY_CONNECTED_KERNEL, inputShape[mv::IO_CHANNEL_DIMENSION],
        opIt->getInputTensor(1)->getShape()[mv::IO_HEIGHT_DIMENSION]}, sourceTensor->getDType(),
        mv::Order::getZMajorID(4), weightsTensorQuantizationParams, opIt->getName() + "_weights");
        auto outputTensorType = opIt->getOutputTensor(0)->get<mv::DType>("dType");

        auto conv2D = om.conv(sourceTensor, weights, {1, 1}, {0, 0, 0, 0}, 1, 1, outputTensorType, outputTensorQuantizationParams,  opIt->getName() + "_2DConv");
        pass.log(Logger::MessageType::Info, "Replaced FullyConnected op " + opIt->getName() + " with " + conv2D->getName());

        if (opIt->hasAttr("bias"))
        {
            auto biasTensorName = opIt->get<std::string>("bias");
            om.addAttr(om.getSourceOp(conv2D), "bias", biasTensorName);
            pass.log(Logger::MessageType::Info, "Moved Bias attribute of FullyConnected op " + opIt->getName() + " to " + conv2D->getName());
        }

        auto convOp = om.getSourceOp(conv2D);
        auto weightsOp = om.getSourceOp(weights);

        if(opIt->hasAttr("opId"))
        {
            unsigned currentOpId = opIt->get<unsigned>("opId");
            weightsOp->set<unsigned>("opId", currentOpId);
            convOp->set<unsigned>("opId", currentOpId);
        }

        linkNewOperationsReplacementFully(parentOpIt, conv2D, om, opIt);
        conv2D->set<mv::Tensor::MemoryLocation>("Location", outputMemoryLocation);
    }
}

void scaleAsDepthwiseFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model)
{

    MV_PROFILED_FUNCTION(MV_PROFILE_PASS)

    mv::OpModel om(model);

    auto scaleOps = om.getOps("Scale");

    for (auto& opIt : scaleOps)
    {
        pass.log(mv::Logger::MessageType::Debug, "Found Scale op " + opIt->getName());

        // First of all, we have to check if the scale can be fused in previous operation
        // According to our fuse_passes.cpp, we can fuse it only if the previous operation is a convolution

        auto sourceTensor = opIt->getInputTensor(0);
        auto outputMemoryLocation = opIt->getOutputTensor(0)->get<mv::Tensor::MemoryLocation>("Location");
        auto parentOpIt = om.getSourceOp(sourceTensor);
        if (parentOpIt->getOpType() == "Conv")
            continue;

        // From here on, we can assume unfusable scale, so we have to replace it with depthwise convolution
        auto scales = opIt->getInputTensor(1);
        auto scalesShape = scales->getShape();
        auto name = opIt->getName();

        // Changing the shape of the populated tensor, but it's not risky in this case
        scales->setShape({1,1,scalesShape[0],1});

        mv::QuantizationParams quantParams({{}, {}, {}, {}});
        if (sourceTensor->isQuantized())
            quantParams = opIt->get<mv::QuantizationParams>("quantParams");

        auto depthwiseConv = om.depthwiseConv(sourceTensor, scales, {1,1}, {0,0,0,0}, 1, mv::DType("Default"), quantParams, name + "_DepthwiseConv");
        auto depthwiseConvOp = om.getSourceOp(depthwiseConv);

        if(opIt->hasAttr("opId"))
        {
            unsigned currentOpId = opIt->get<unsigned>("opId");
            depthwiseConvOp->set<unsigned>("opId", currentOpId);
        }
        pass.log(mv::Logger::MessageType::Info, "Replaced unfusable scale op " + opIt->getName() + " with " + depthwiseConvOp->getName());
        depthwiseConv->set<mv::Tensor::MemoryLocation>("Location", outputMemoryLocation);
        linkNewOperationsReplacementScale(parentOpIt, depthwiseConv, om, opIt, false);
    }
}

void averageAsDepthWiseFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model)
{

    MV_PROFILED_FUNCTION(MV_PROFILE_PASS)

    mv::OpModel om(model);
    mv::DataModel dm(model);

    auto averagePoolOps = om.getOps("AveragePool");

    for (auto& opIt : averagePoolOps)
    {
        pass.log(mv::Logger::MessageType::Debug, "Found AveragePool op " + opIt->getName());

        auto sourceTensor = opIt->getInputTensor(0);
        auto outputMemoryLocation = opIt->getOutputTensor(0)->get<mv::Tensor::MemoryLocation>("Location");

        auto parentOpIt = om.getSourceOp(sourceTensor);

        auto inputShape = sourceTensor->getShape();
        std::array<unsigned short, 2> kSize = opIt->get<std::array<unsigned short, 2>>("kSize");
        std::array<unsigned short, 2> stride = opIt->get<std::array<unsigned short, 2>>("stride");
        std::array<unsigned short, 4> padding = opIt->get<std::array<unsigned short, 4>>("padding");

        unsigned int total_shape = 1 * inputShape[mv::IO_CHANNEL_DIMENSION] * kSize[1] * kSize[0];
        double scaleValue = 1/double(kSize[0] * kSize[1]);

        unsigned short channel_multiplier = 1;

        auto name = opIt->getName();
        mv::Data::TensorIterator weights;
        std::vector<int64_t> zp = { 0 };
        std::vector<double> min = { 1 };
        std::vector<double> max = { 1 };

        std::vector<double> scale(1, scaleValue);
        mv::QuantizationParams weightsQuantParams(zp, scale, min, max);

        if (sourceTensor->isDoubleType())
        {
            double weightsValue = 1;
            std::vector<double> weightsData(total_shape, weightsValue);
            //NOTE: Weights have to be 1 and scale division, cause of better hardware accuracy
            pass.log(mv::Logger::MessageType::Debug, "Input tensor not quantized, generating non-quantized weights");
            weights = om.constant(weightsData,
                                {kSize[0], kSize[1], inputShape[mv::IO_CHANNEL_DIMENSION], channel_multiplier},
                                sourceTensor->getDType(),
                                mv::Order(mv::Order::getRowMajorID(4)), weightsQuantParams);
        }
        else
        {
            int64_t weightsValue = 1;
            std::vector<int64_t> weightsData(total_shape, weightsValue);
            pass.log(mv::Logger::MessageType::Debug, "Input tensor quantized, generating quantized weights");
            // If the input model is quantized, then the replacement pass needs to create
            // quantization params for the weights parameter of the depthwise convolution.
            weights = om.constantInt(weightsData,
                                {kSize[0], kSize[1], inputShape[mv::IO_CHANNEL_DIMENSION], channel_multiplier},
                                sourceTensor->getDType(),
                                mv::Order(mv::Order::getRowMajorID(4)),
                                weightsQuantParams);
        }

        //Check the last argument name!!!
        mv::Data::TensorIterator depthwise_conv;
        if (sourceTensor->isQuantized())
        {
            pass.log(mv::Logger::MessageType::Debug, "Passing quantization params from input to output");
            auto quantParams = opIt->get<mv::QuantizationParams>("quantParams");
            // use default dilation factor
            depthwise_conv = om.depthwiseConv(sourceTensor, weights, stride, padding, 1, mv::DType("Default"), quantParams, name + "_DepthwiseConv");
        }
        else
        {
            pass.log(mv::Logger::MessageType::Debug, "No need for quantization params, since input is of a floating point type");
            mv::QuantizationParams emptyQuantParams({{}, {}, {}, {}});
            depthwise_conv = om.depthwiseConv(sourceTensor, weights, stride, padding, 1, mv::DType("Default"), emptyQuantParams, name + "_DepthwiseConv");
        }

        auto depthwiseConvOp = om.getSourceOp(depthwise_conv);
        auto weightsOp = om.getSourceOp(weights);

        if(opIt->hasAttr("opId"))
        {
            unsigned currentOpId = opIt->get<unsigned>("opId");
            weightsOp->set<unsigned>("opId", currentOpId);
            depthwiseConvOp->set<unsigned>("opId", currentOpId);
        }
        pass.log(mv::Logger::MessageType::Info, "Replaced AveragePool op " + opIt->getName() + " with " + depthwise_conv->getName());
        depthwise_conv->set<mv::Tensor::MemoryLocation>("Location", outputMemoryLocation);
        linkNewOperationsReplacementAverage(parentOpIt, depthwise_conv, om, opIt);
    }
}
