//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/computation/op/op_registry.hpp"

namespace mv
{

    namespace op_hswish
    {

        static std::function<std::pair<bool, std::size_t>(const std::vector<Data::TensorIterator>&,
            const std::map<std::string, Attribute>&, std::string&)> inputCheckFcn =
            [](const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>&,
            std::string& ) -> std::pair<bool, std::size_t>
        {

            return {true, 0};

        };

        static std::function<void(const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>&,
            std::vector<Tensor>&)> outputDefFcn =
            [](const std::vector<Data::TensorIterator>& inputs, const std::map<std::string, Attribute>&, std::vector<Tensor>& outputs)
        {

            outputs.push_back(mv::Tensor(":0", inputs[0]->getShape(), inputs[0]->getDType(), inputs[0]->getOrder()));

        };

    }

    namespace op {
        MV_REGISTER_OP(HSwish)
        .setInputs({"data"})
        .setOutputs({"output"})
        .setInputCheck(op_hswish::inputCheckFcn)
        .setOutputDef(op_hswish::outputDefFcn)
        .setTypeTrait({"executable", "exposed"});
    }

}
