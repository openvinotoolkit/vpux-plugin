//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/computation/op/op_registry.hpp"

namespace mv
{

    namespace op_constant
    {

        // NOTE: InputCheckFcn has to be different from ConstantInt and Constant
        // This is because ConstantInt cannot accept non integer DTypes
        static std::function<std::pair<bool, std::size_t>(const std::vector<Data::TensorIterator>&,
            const std::map<std::string, Attribute>&, std::string&)> inputCheckFcn =
            [](const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>& args,
            std::string&) -> std::pair<bool, std::size_t>
        {
            if(args.at("dType").get<mv::DType>() == mv::DType("Default"))
                return {false, 1};
            return {true, 0};

        };

        static std::function<std::pair<bool, std::size_t>(const std::vector<Data::TensorIterator>&,
            const std::map<std::string, Attribute>&, std::string&)> inputCheckFcnConstantInt =
            [](const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>& args,
            std::string&) -> std::pair<bool, std::size_t>
        {
            auto dType = args.at("dType").get<mv::DType>();
            if(dType == mv::DType("Default"))
                return {false, 1};
            if(dType.isDoubleType())
                return {false, 1};
            return {true, 0};

        };

        static std::function<void(const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>&,
            std::vector<Tensor>&)> outputDefFcn =
            [](const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>& args, std::vector<Tensor>& outputs)
        {
            outputs.emplace_back(":0", args.at("shape").get<mv::Shape>(), args.at("dType").get<mv::DType>(),
                    args.at("order").get<mv::Order>(), args.at("data").get<std::vector<double>>());
        };

        static std::function<void(const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>&,
            std::vector<Tensor>&)> outputIntDefFcn =
            [](const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>& args, std::vector<Tensor>& outputs)
        {
            outputs.emplace_back(":0", args.at("shape").get<mv::Shape>(), args.at("dType").get<mv::DType>(),
                    args.at("order").get<mv::Order>(), args.at("data").get<std::vector<int64_t>>());
        };

        static std::function<void(const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>&,
            std::vector<Tensor>&)> outputDataElementDefFcn =
            [](const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>& args, std::vector<Tensor>& outputs)
        {
            outputs.emplace_back(":0", args.at("shape").get<mv::Shape>(), args.at("dType").get<mv::DType>(),
                    args.at("order").get<mv::Order>(),  args.at("data").get<std::vector<mv::DataElement>>());
     };

    }

    namespace op {
        MV_REGISTER_OP(Constant)
        .setOutputs({"output"})
        .setArg<std::vector<double>>("data")
        .setArg<mv::Shape>("shape")
        .setArg<mv::DType>("dType")
        .setArg<mv::Order>("order")
        .setInputCheck(op_constant::inputCheckFcn)
        .setOutputDef(op_constant::outputDefFcn)
        .setTypeTrait({"exposed"});

        MV_REGISTER_OP(ConstantInt)
        .setOutputs({"output"})
        .setArg<std::vector<int64_t>>("data")
        .setArg<mv::Shape>("shape")
        .setArg<mv::DType>("dType")
        .setArg<mv::Order>("order")
        .setInputCheck(op_constant::inputCheckFcnConstantInt)
        .setOutputDef(op_constant::outputIntDefFcn)
        .setTypeTrait({"exposed"});

        MV_REGISTER_OP(ConstantDataElement)
        .setOutputs({"output"})
        .setArg<std::vector<mv::DataElement>>("data")
        .setArg<mv::Shape>("shape")
        .setArg<mv::DType>("dType")
        .setArg<mv::Order>("order")
        .setInputCheck(op_constant::inputCheckFcn)
        .setOutputDef(op_constant::outputDataElementDefFcn);


    }

}
