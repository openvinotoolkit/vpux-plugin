//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/computation/op/op_registry.hpp"

namespace mv
{

    namespace op_psroi_pooling
    {

        static std::function<std::pair<bool, std::size_t>(const std::vector<Data::TensorIterator>&,
            const std::map<std::string, Attribute>&, std::string&)> inputCheckFcn =
            [](const std::vector<Data::TensorIterator>& inputs, const std::map<std::string, Attribute>&,
            std::string& errMsg) -> std::pair<bool, std::size_t>
        {
            auto input = inputs[0];
            if (inputs.size() != 2)
            {
                std::stringstream err;
                err << "Incorrect number of inputs (must be 2): " << inputs.size();
                errMsg = err.str();
                return {false, 0};
            }

            return {true, 0};
        };

        static std::function<void(const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>& ,
            std::vector<Tensor>&)> outputDefFcn =
            [](const std::vector<Data::TensorIterator>& inputs, const std::map<std::string, Attribute>& args, std::vector<Tensor>& outputs)
        {
            auto input = inputs[0];
            auto outputOrder = input->getOrder();
            auto inputShape  = input->getShape();

            auto N = inputs[1]->getShape()[3];
            auto C = args.at("output_dim").get<std::size_t>();
            auto H = args.at("pooled_h").get<std::size_t>();
            auto W = args.at("pooled_w").get<std::size_t>();

            mv::Shape outputShape({W, H, C, N});;
            outputs.emplace_back(":0", outputShape, inputs[0]->getDType(), outputOrder);
        };
    }
    namespace op
    {
        MV_REGISTER_OP(PSROIPooling)
        .setInputs({"inputs"})
        .setOutputs({"output"})
        .setArg<std::size_t>("output_dim")
        .setArg<std::size_t>("group_size")
        .setArg<double>("spatial_scale")
        .setArg<std::size_t>("pooled_w")
        .setArg<std::size_t>("pooled_h")
        .setArg<std::size_t>("spatial_bin_x")
        .setArg<std::size_t>("spatial_bin_y")
        .setArg<std::string>("mode")
        .setInputCheck(op_psroi_pooling::inputCheckFcn)
        .setOutputDef(op_psroi_pooling::outputDefFcn)
        .setTypeTrait({"executable", "exposed"})
        .setVariableInputNum(true);
    }

}

