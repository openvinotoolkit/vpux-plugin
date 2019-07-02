#include "include/mcm/pass/pass_registry.hpp"
#include "meta/include/mcm/op_model.hpp"
#include "include/mcm/computation/model/data_model.hpp"
#include "include/mcm/tensor/math.hpp"
#include "include/mcm/utils/custom_strings.hpp"

static void fuseBatchNormFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::json::Object&);
static void fuseBiasFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::json::Object&);
static void fuseReluFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::json::Object&);
static void fuseScaleFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::json::Object&);

namespace mv
{

    namespace pass
    {

        MV_REGISTER_PASS(FuseBatchNorm)
        .setFunc(fuseBatchNormFcn)
        .setDescription(
            "Replaces a batchnorm op with eltwise ops or their 1d equivalents. "
            "Following cases are handled:\n"
            " - batchnorm parameters tensors are n-dimensional - the replacement is a chain of multiply and add (eltwise)\n"
            " - batchnorm parameters tensors are 1-dimensional and parent op is not of type conv2d - the replacement is chain of scale and bias\n"
            " - batchnorm parameters tensors are 1-dimensional and parent op is of type conv2d - the replacement is a bias and weights of conv2d are modified]\n"
        );

        MV_REGISTER_PASS(FuseBias)
        .setFunc(fuseBiasFcn)
        .setDescription(
            "Fuses a bias op to the parent op if this op is of type conv2d. "
            "Bias op is removed from the model, the biases tensor "
            " (second input of bias op) is appended to parent op as an attribute "
            " of type double vector of name 'bias'."
        );

        MV_REGISTER_PASS(FuseRelu)
        .setFunc(fuseReluFcn)
        .setDescription(
            "Fuses a relu op to the parent op."
            "Relu op is removed from the model, and a new attribute of type OpType and value relu is defined for parent Op."
        );

        MV_REGISTER_PASS(FuseScale)
        .setFunc(fuseScaleFcn)
        .setDescription(
            "Fuses a scale op to the parent op is this op is of type conv2d. Scale op is removed from the model and conv2d weights are rescaled. "
            "If the conv2d has a 'bias' attribute of type double vector it is rescaled as well. In this case the length of scale op parameters tensor and 'bias' attribute "
            "has to match."
        );

    }

}

mv::Data::OpListIterator linkNewOperationsFuse(mv::Data::OpListIterator parentOpIt, mv::Data::TensorIterator sourceTensor, mv::OpModel om, mv::Data::OpListIterator opIt)
{
    //Important: do not change the order of this ops
    std::vector<mv::Data::OpListIterator> opsToLink;
    std::vector<std::size_t> inputSlots;
    for (mv::Data::FlowSiblingIterator sinkFlow(opIt.leftmostOutput()); sinkFlow != om.flowEnd(); ++sinkFlow)
    {
        opsToLink.push_back(sinkFlow.sink());
        inputSlots.push_back(sinkFlow->get<std::size_t>("sinkInput"));
    }

    while(opIt.parentsSize() > 1)
    {
        auto paramOp = opIt.leftmostParent();
        ++paramOp;
        om.removeOp(paramOp);
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

void fuseBiasFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::json::Object&)
{
    

    using namespace mv;

    OpModel om(model);
    DataModel dm(model);

    for (auto opIt = om.getInput(); opIt != om.opEnd(); ++opIt)
    {

        if (opIt->getOpType() == "Bias")
        {            
            pass.log(Logger::MessageType::Debug, "Found Bias op " + opIt->getName());

            auto parentOpIt = om.getSourceOp(opIt->getInputTensor(0));
            
            if (parentOpIt->getOpType() == "Conv" ||
                parentOpIt->getOpType() == "FullyConnected" ||
                parentOpIt->getOpType() == "DepthwiseConv")
            {

                auto bias = *opIt->getInputTensor(1);
                auto biasOutputMemoryLocation = opIt->getOutputTensor(0)->get<mv::Tensor::MemoryLocation>("Location");

                if (parentOpIt->hasAttr("bias"))
                {
                    auto biasTensor = dm.getTensor(parentOpIt->get<std::string>("bias"));
                    biasTensor->add(bias);
                    pass.log(Logger::MessageType::Info, "Accumulatively fused bias op " + opIt->getName() + " into " + parentOpIt->getName());
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
                    pass.log(Logger::MessageType::Info, "Fused bias op " + opIt->getName() + " into " + parentOpIt->getName());
                }

                auto sourceTensor = parentOpIt->getOutputTensor(0);

                opIt = linkNewOperationsFuse(parentOpIt, sourceTensor, om, opIt);
                if (biasOutputMemoryLocation.isForced())
                {
                    opIt->getOutputTensor(0)->set<mv::Tensor::MemoryLocation>("Location", biasOutputMemoryLocation);
                }
            }

        }

    }

}

void fuseScaleFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::json::Object&)
{

    using namespace mv;
    OpModel om(model);
    DataModel dm(model);

    for (auto opIt = om.getInput(); opIt != om.opEnd(); ++opIt)
    {

        if (opIt->getOpType() == "Scale")
        {            
            pass.log(Logger::MessageType::Debug, "Found Scale op " + opIt->getName());

            auto parentOpIt = om.getSourceOp(opIt->getInputTensor(0));
            auto scaleOutputMemoryLocation = opIt->getOutputTensor(0)->get<mv::Tensor::MemoryLocation>("Location");

            if (parentOpIt->getOpType() == "Conv")
            {

                auto scale = *opIt->getInputTensor(1);
                parentOpIt->getInputTensor(1)->multiply(scale);

                pass.log(Logger::MessageType::Info, "Fused scale op " + opIt->getName() + " into " + parentOpIt->getName());

                if (parentOpIt->hasAttr("bias"))
                {
                    auto biasTensor = dm.getTensor(parentOpIt->get<std::string>("bias"));
                    biasTensor->multiply(scale);
                    pass.log(Logger::MessageType::Info, "Fused scale op " + opIt->getName() + " into " + 
                        parentOpIt->getName() + " bias attribute");

                }

                auto sourceTensor = parentOpIt->getOutputTensor(0);

                opIt = linkNewOperationsFuse(parentOpIt, sourceTensor, om, opIt);
                if (scaleOutputMemoryLocation.isForced())
                {
                    opIt->getOutputTensor(0)->set<mv::Tensor::MemoryLocation>("Location", scaleOutputMemoryLocation);
                }
            }

        }

    }

}

void fuseReluFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::json::Object&)
{

    using namespace mv;
    OpModel om(model);

    for (auto opIt = om.getInput(); opIt != om.opEnd(); ++opIt)
    {

        if (opIt->getOpType() == "Relu")
        {
            auto reluOutputMemoryLocation = opIt->getOutputTensor(0)->get<mv::Tensor::MemoryLocation>("Location");

            pass.log(Logger::MessageType::Debug, "Found ReLU op " + opIt->getName());

            auto parentOpIt = om.getSourceOp(opIt->getInputTensor(0));
            parentOpIt->set<std::string>("postOpType", "Relu");

            pass.log(Logger::MessageType::Info, "Fused ReLU op " + opIt->getName() + " into " + parentOpIt->getName());

            auto sourceTensor = parentOpIt->getOutputTensor(0);

            opIt = linkNewOperationsFuse(parentOpIt, sourceTensor, om, opIt);
            if (reluOutputMemoryLocation.isForced())
            {
                opIt->getOutputTensor(0)->set<mv::Tensor::MemoryLocation>("Location", reluOutputMemoryLocation);
            }
        }

    }

}

void fuseBatchNormFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::json::Object&)
{
    std::cout << "Fusing batch norm" << std::endl;
    using namespace mv;
    OpModel om(model);

    for (auto opIt = om.getInput(); opIt != om.opEnd(); ++opIt)
    {

        if (opIt->getOpType() == "BatchNormalization")
        {
            pass.log(Logger::MessageType::Debug, "Found BatchNorm op " + opIt->getName());
            auto outputMemoryLocation = opIt->getOutputTensor(0)->get<mv::Tensor::MemoryLocation>("Location");

            auto batchNormName = opIt->getName();
            auto parentOpIt = om.getSourceOp(opIt->getInputTensor(0));

            auto bnMean = *opIt->getInputTensor(1);
            auto bnVar = *opIt->getInputTensor(2);
            auto bnOffset = *opIt->getInputTensor(3);
            auto bnScale = *opIt->getInputTensor(4);
            double bnEps = opIt->get<double>("eps");

            auto scaleParam = math::divide(bnScale, math::sqrt(math::add(bnVar, bnEps)));
            auto offsetParam = math::subtract(bnOffset, math::multiply(bnMean, scaleParam));
            auto offset = om.constantDataElement(offsetParam.getData(), offsetParam.getShape(), offsetParam.getDType(),
                offsetParam.getOrder(),{{},{},{},{}}, batchNormName + "_offset");

            Data::TensorIterator sourceTensor;

            if (bnMean.getShape().ndims() == 1)
            {
                if (parentOpIt->getOpType() == "Conv")
                {
                    parentOpIt->getInputTensor(1)->multiply(scaleParam);
                    sourceTensor = parentOpIt->getOutputTensor(0);
                    pass.log(Logger::MessageType::Info, "Fused multiplicative term of BatchNorm op " + opIt->getName() + 
                        " into " + parentOpIt->getName());
                }
                else
                {
                    auto scale = om.constantDataElement(scaleParam.getData(), scaleParam.getShape(), scaleParam.getDType(), scaleParam.getOrder());

                    sourceTensor = om.scale(opIt->getInputTensor(0), scale);
                    parentOpIt = om.getSourceOp(sourceTensor);
                    pass.log(Logger::MessageType::Info, "Replaced multiplicative term of BatchNorm op " + opIt->getName() + 
                        " with " + parentOpIt->getName());
                }
            }
            else
            {
                auto scale = om.constantDataElement(scaleParam.getData(), scaleParam.getShape(), scaleParam.getDType(), scaleParam.getOrder());

                sourceTensor = om.multiply(opIt->getInputTensor(0), scale);
                parentOpIt = om.getSourceOp(sourceTensor);
                pass.log(Logger::MessageType::Info, "Replaced multiplicative term of BatchNorm op " + opIt->getName() + 
                    " with " + parentOpIt->getName());

            }

            if (offsetParam.getShape().ndims() == 1)
                sourceTensor = om.bias(sourceTensor, offset);  

            else
                sourceTensor = om.add(sourceTensor, offset);
            pass.log(Logger::MessageType::Info, "Replaced additive term of BatchNorm op " + opIt->getName() + 
                " with " + om.getSourceOp(sourceTensor)->getName());

            opIt = linkNewOperationsFuse(parentOpIt, sourceTensor, om, opIt);
            if (outputMemoryLocation.isForced())
            {
                opIt->getOutputTensor(0)->set<mv::Tensor::MemoryLocation>("Location", outputMemoryLocation);
            }
        }

    }

}
