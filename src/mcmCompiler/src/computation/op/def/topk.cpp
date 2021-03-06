//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/computation/op/op_registry.hpp"

namespace mv
{

    namespace op_topk
    {

        const std::vector<std::string> MODES = {"min", "max"};
        const std::vector<std::string> SORT_TYPES = {"value", "index", "none"};

        static std::function<std::pair<bool, std::size_t>(const std::vector<Data::TensorIterator>&,
            const std::map<std::string, Attribute>&, std::string&)> inputCheckFcn =
            [](const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>& args,
            std::string& errMsg) -> std::pair<bool, std::size_t>
        {

            auto mode = args.at("mode").get<std::string>();
            if(std::find(MODES.begin(), MODES.end(), mode) == MODES.end())
            {
                errMsg = "Unsupported topk mode";
                return {false, 1};
            }
            auto sort = args.at("sort").get<std::string>();
            if(std::find(SORT_TYPES.begin(), SORT_TYPES.end(), sort) == SORT_TYPES.end())
            {
                errMsg = "Unsupported topk sort";
                return {false, 1};
            }
            // Check for valid axis value
            auto axis = args.at("axis").get<int64_t>();
            if (!((axis >= -3) && (axis <= 3)) && (axis != 99))
            {
                std::stringstream err;
                err << "Invalid axis value (must be -3 to 3, or 99 for no axis): " << axis;
                errMsg = err.str();
                return {false, 0};
            }

            return {true, 0};

        };

        static std::function<void(const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>&,
            std::vector<Tensor>&)> outputDefFcn =
            [](const std::vector<Data::TensorIterator>& inputs, const std::map<std::string, Attribute>& args, std::vector<Tensor>& outputs)
        {
            // Axis is based off NCHW channel ordering, i.e. 0 = N, W=3
            // This is consistent with the TensorReference ordering of dimensions
            // 99 to signal nothing specified, since -3..3 including 0 are valid values
            auto outputShape = inputs[0]->getShape();
            auto axis = args.at("axis").get<int64_t>();

            // Handle negative axis
            if (axis < 0)
            {
                axis = 4 + axis;
            }

            // Modify outputShape based on axis
            if (axis == 99)
            {
                outputShape[3] = 1;
                outputShape[2] = 1;
                outputShape[1] = 1;
                outputShape[0] = 1;
            }
            else if (axis == 0)
            {
                outputShape[3] = 1;
                outputShape[2] = 1;
                outputShape[1] = 1;
            }
            else if (axis == 1)
            {
                outputShape[3] = 1;
                outputShape[2] = 1;
            }
            else if (axis == 2)
            {
                outputShape[3] = 1;
            }

            outputs.emplace_back(":0",  outputShape, inputs[0]->getDType(), inputs[0]->getOrder());

            //TODO, the second output is the indices, dtype is supposed to be unsigned int
            outputs.emplace_back(":1",  outputShape, inputs[0]->getDType(), inputs[0]->getOrder());
        };

    }

    namespace op {

        MV_REGISTER_OP(TopK)
        .setInputs({"data"})
        .setOutputs({"output"})
        .setArg<std::string>("sort")
        .setArg<std::string>("mode")
        .setArg<int64_t>("top_k")
        .setOptionalArg<int64_t>("axis", 99)
        .setInputCheck(op_topk::inputCheckFcn)
        .setOutputDef(op_topk::outputDefFcn)
        .setTypeTrait({"executable", "exposed"});

    }

}
