//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/computation/op/op_registry.hpp"

namespace mv
{

    namespace op_reorg_yolo
    {

        static std::function<std::pair<bool, std::size_t>(const std::vector<Data::TensorIterator>&,
            const std::map<std::string, Attribute>&, std::string&)> inputCheckFcn =
            [](const std::vector<Data::TensorIterator>& inputs, const std::map<std::string, Attribute>& args,
            std::string& errMsg) -> std::pair<bool, std::size_t>
        {
            auto stride = args.at("stride").get<unsigned>();
            if (stride == 0)
            {
                errMsg = "Stride must be positive (non-zero)";
                return {false, 0};
            }

            auto input = inputs[0];
            if (inputs.size() != 1)
            {
                std::stringstream err;
                err << "Too many inputs (must be 1): " << inputs.size();
                errMsg = err.str();
                return {false, 0};
            }

            auto order_str = input->getOrder().toString();
            auto C_idx = order_str.find("C");
            auto H_idx = order_str.find("H");
            auto W_idx = order_str.find("W");
            if (C_idx == std::string::npos ||
                H_idx == std::string::npos ||
                W_idx == std::string::npos)
            {
                errMsg = "Wrong input tensor (must have C, H, W dimensions): order=" + order_str;
                return {false, 0};
            }

            auto shape = input->getShape();
            auto ndims = shape.ndims();
            auto height = shape[(ndims - 1) - H_idx];
            auto width  = shape[(ndims - 1) - W_idx];
            if (height % stride ||
                width  % stride)
            {
                std::stringstream err;
                err << "Stride must evenly divide H, W dimensions: stride=" << stride
                    << ", height=" << height << ", width=" << width;
                errMsg = err.str();
                return {false, 0};
            }

            return {true, 0};
        };
                
        static std::function<void(const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>&, 
            std::vector<Tensor>&)> outputDefFcn =
            [](const std::vector<Data::TensorIterator>& inputs, const std::map<std::string, Attribute>& args, std::vector<Tensor>& outputs)
        {
            auto stride = args.at("stride").get<unsigned>();

            auto input = inputs[0];
            auto order = input->getOrder();
            auto in_shape = input->getShape();

            auto width    = in_shape[IO_WIDTH_DIMENSION];
            auto height   = in_shape[IO_HEIGHT_DIMENSION];
            auto channels = in_shape[IO_CHANNEL_DIMENSION];

            mv::Shape out_shape(in_shape);
            out_shape = {width/stride, height/stride, channels * (stride * stride), 1};

            outputs.emplace_back(":0",  out_shape, inputs[0]->getDType(), order);
        };


    }

    namespace op {
        // Reorg Yolo reorganizes tensor like,
        // e.g. NxCxHxW into Nx(C*4)x(H/2)x(W/2)

        MV_REGISTER_OP(ReorgYolo)
        .setInputs({"data"})
        .setOutputs({"output"})
        .setArg<unsigned>("stride")
        .setInputCheck(op_reorg_yolo::inputCheckFcn)
        .setOutputDef(op_reorg_yolo::outputDefFcn)
        .setTypeTrait({"executable", "exposed"});

    }
}
