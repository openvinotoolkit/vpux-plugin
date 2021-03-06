//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/computation/op/op_registry.hpp"
#include "include/mcm/tensor/tiling.hpp"

namespace mv
{

    namespace op_max_pool
    {

        static std::function<std::pair<bool, std::size_t>(const std::vector<Data::TensorIterator>&,
            const std::map<std::string, Attribute>&, std::string&)> inputCheckFcn =
            [](const std::vector<Data::TensorIterator>& inputs, const std::map<std::string, Attribute>& args,
            std::string& errMsg) -> std::pair<bool, std::size_t>
        {
            auto inputShape = inputs[0]->getShape();

            if (inputShape.ndims() != 4)
            {
                errMsg = "Invalid shape of the input tensor (input 0) - must have a dimensionality of 4, "
                    " has " + std::to_string(inputShape.ndims());
                return {false, 0};
            }

            auto padding = args.at("padding").get<std::array<unsigned short, 4>>();
            auto kSize = args.at("kSize").get<std::array<unsigned short, 2>>();

            if (inputShape[IO_WIDTH_DIMENSION] + padding[0] + padding[1] < kSize[0])
            {
                errMsg = "Filter kernel width (" + std::to_string(kSize[0]) + ") exceeds the padded input width ("
                    + std::to_string(inputShape[IO_WIDTH_DIMENSION] + padding[0] + padding[1]) + ")";
                return {false, 0};
            }

            if (inputShape[IO_HEIGHT_DIMENSION] + padding[2] + padding[3] < kSize[1])
            {
                errMsg = "Filter kernel height (" + std::to_string(kSize[1]) + ") exceeds the padded input height ("
                    + std::to_string(inputShape[IO_HEIGHT_DIMENSION] + padding[2] + padding[3]) + ")";
                return {false, 0};
            }

            return {true, 0};

        };

        static std::function<void(const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>&,
            std::vector<Tensor>&)> outputDefFcn =
            [](const std::vector<Data::TensorIterator>& inputs, const std::map<std::string, Attribute>& args, std::vector<Tensor>& outputs)
        {

            auto inputShape = inputs[0]->getShape();
            auto padding = args.at("padding").get<std::array<unsigned short, 4>>();
            auto stride = args.at("stride").get<std::array<unsigned short, 2>>();
            auto kSize = args.at("kSize").get<std::array<unsigned short, 2>>();

            size_t W = Tiling::inferOutputSize(inputShape[IO_WIDTH_DIMENSION], padding[0], padding[1], kSize[0], stride[0]);
            size_t H = Tiling::inferOutputSize(inputShape[IO_HEIGHT_DIMENSION], padding[2], padding[3], kSize[1], stride[1]);
            size_t C = inputShape[IO_CHANNEL_DIMENSION];
            size_t N = inputShape[IO_BATCH_DIMENSION];

            Shape outputShape({W, H, C, N});

            outputs.emplace_back(":0", outputShape, inputs[0]->getDType(), inputs[0]->getOrder());
        };


    }


    namespace op {
        MV_REGISTER_OP(MaxPool)
        .setInputs({"data"})
        .setOutputs({"output"})
        .setArg<std::array<unsigned short, 2>>("kSize")
        .setArg<std::array<unsigned short, 2>>("stride")
        .setArg<std::array<unsigned short, 4>>("padding")
        .setOptionalArg<bool>("exclude_pad", true)
        .setInputCheck(op_max_pool::inputCheckFcn)
        .setOutputDef(op_max_pool::outputDefFcn)
        .setTypeTrait({"executable", "exposed", "optimizable"});

    }

}
