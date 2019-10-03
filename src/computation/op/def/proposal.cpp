#include "include/mcm/computation/op/op_registry.hpp"

namespace mv
{

    namespace op
    {

        static std::function<std::pair<bool, std::size_t>(const std::vector<Data::TensorIterator>&,
            const std::map<std::string, Attribute>&, std::string&)> inputCheckFcn =
            [](const std::vector<Data::TensorIterator>& inputs, const std::map<std::string, Attribute>& args,
            std::string& errMsg) -> std::pair<bool, std::size_t>
        {

            auto input = inputs[0];
            if (inputs.size() != 5)
            {
                std::stringstream err;
                err << "Incorrect number of inputs (must be 5): " << inputs.size();
                errMsg = err.str();
                return {false, 0};
            }

            return {true, 0};

        };

        static std::function<void(const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>&,
            std::vector<Tensor>&)> outputDefFcn =
            [](const std::vector<Data::TensorIterator>& inputs, const std::map<std::string, Attribute>& args, std::vector<Tensor>& outputs)
        {

            auto input = inputs[0];
            auto outputOrder = input->getOrder();
            auto inputShape = input->getShape();
            auto ndims = inputShape.ndims();
            mv::Shape outputShape(ndims);

            outputs.push_back(mv::Tensor(":0", input->getShape(), input->getDType(), input->getOrder()));

        };

        MV_REGISTER_OP(Proposal)
        .setInputs({"inputs"})
        .setOutputs({"output"})
        .setArg<unsigned>("base_size")
        .setArg<unsigned>("pre_nms_topn")
        .setArg<unsigned>("post_nms_topn")
        .setArg<double>("nms_thresh")
        .setArg<unsigned>("feat_stride")
        .setArg<unsigned>("min_size")
        .setOptionalArg<double>("pre_nms_thresh", 0.0)
        .setOptionalArg<bool>("clip_before_nms", true)
        .setOptionalArg<bool>("clip_after_nms", false)
        .setOptionalArg<bool>("normalize", false)
        .setOptionalArg<double>("box_size_scale", 1.0)
        .setOptionalArg<double>("box_coordinate_scale", 1.0)
        .setOptionalArg<std::string>("framework", std::string("TENSORFLOW"))
        .setOptionalArg<bool>("for_deformable", false)
        .setOptionalArg<mv::DType>("dType", mv::DType("Default"))
        .setOptionalArg<mv::QuantizationParams>("quantParams", mv::QuantizationParams({},{},{},{}))
        .setInputCheck(inputCheckFcn)
        .setOutputDef(outputDefFcn)
        .setTypeTrait({"executable", "exposed"})
        .setVariableInputNum(true);
    }

}
