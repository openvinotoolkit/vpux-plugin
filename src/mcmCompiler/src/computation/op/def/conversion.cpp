//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/computation/op/op_registry.hpp"
#include "include/mcm/pass/pass_dtype_utils.hpp"

namespace mv
{

    namespace op_conversion
    {
        bool isConversionSupported(const DType& inDType, const DType& outDType, const std::string& opName, std::string& errMsg) {
            const auto dTypeIter = supportedConversions.find(std::make_pair(inDType, outDType));

            if (dTypeIter == supportedConversions.end())
            {
                errMsg = "Unsupported conversion (" + opName
                       + "): inDType=" +  inDType.toString()
                       + ", outDType=" + outDType.toString()
                       + ". Supported combinations are:";

                for (const auto& item : supportedConversions) {
                    errMsg += " ";
                    errMsg += item.first.toString();
                    errMsg += "->";
                    errMsg += item.second.toString();
                }

                return false;
            }

            return true;
        }

        static std::function<std::pair<bool, std::size_t>(const std::vector<Data::TensorIterator>&,
            const std::map<std::string, Attribute>&, std::string&)> inputCheckFcn =
            [](const std::vector<Data::TensorIterator>& inputs, const std::map<std::string, Attribute>& args,
            std::string& errMsg) -> std::pair<bool, std::size_t>
        {
            if (inputs.size() != 1) {
                errMsg = "Invalid number of inputs: must be 1, has " + std::to_string(inputs.size());
                return {false, 0};
            }

            const auto  inDType = inputs[0]->getDType();
            const auto outDType = args.at("dType").get<mv::DType>();
            const std::string opName = "mv::op_conversion";
            if (!isConversionSupported(inDType, outDType, opName, errMsg))
            {
                return {false, 0};
            }

            return {true, 0};
        };

        static std::function<void(const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>&,
            std::vector<Tensor>&)> outputDefFcn =
            [](const std::vector<Data::TensorIterator>& inputs, const std::map<std::string, Attribute>& args, std::vector<Tensor>& outputs)
        {

            outputs.emplace_back(":0", inputs[0]->getShape(), args.at("dType").get<mv::DType>(), inputs[0]->getOrder());

        };

    }

    namespace op {
        MV_REGISTER_OP(Conversion)
        .setInputs({"data"})
        .setOutputs({"output"})
        .setArg<mv::DType>("dType")
        .setInputCheck(op_conversion::inputCheckFcn)
        .setOutputDef(op_conversion::outputDefFcn)
        .setTypeTrait({"executable"});
    }

}
