//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/computation/op/op_registry.hpp"

namespace mv
{

    namespace op_ctcdecoder
    {

        static std::function<std::pair<bool, std::size_t>(const std::vector<Data::TensorIterator>&,
            const std::map<std::string, Attribute>&, std::string&)> inputCheckFcn =
            [](const std::vector<Data::TensorIterator>& inputs, const std::map<std::string, Attribute>&,
            std::string& errMsg) -> std::pair<bool, std::size_t>
        {

            auto input = inputs[0];
            auto inputShape = input->getShape();
            auto seq = inputs[1];
            auto seqShape = seq->getShape();

            if (seqShape.ndims() > 2)
            {
                // truncate seq tensor
                seqShape = {seqShape[seqShape.ndims() - 2], seqShape[seqShape.ndims() - 1]};
            } else if (seqShape.ndims() < 2) {
                errMsg = "Invalid shape of seq tensor (input 1)" + inputShape.toString();
                return {false, 0};
            }

            // check that sequence length is equal to logits tenstor length
            if (inputShape[mv::IO_CHANNEL_DIMENSION] != seqShape[1])
            {
                errMsg = "Invalid shape of seq tensor (input 1) - the dimension has to equal to the last dimension"
                    " of the input tensor which is " + std::to_string(inputShape[-1]);
                return {false, 0};
            }

            return {true, 0};

        };

        static std::function<void(const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>&,
            std::vector<Tensor>&)> outputDefFcn =
            [](const std::vector<Data::TensorIterator>& inputs, const std::map<std::string, Attribute>& /*args*/, std::vector<Tensor>& outputs)
        {
            assert(inputs.size() > 0);

            mv::Shape input0Shape = inputs[0]->getShape();

            mv::Shape outputShape = {1, 1, input0Shape[mv::IO_CHANNEL_DIMENSION], 1};
            outputs.emplace_back(":0", outputShape, inputs[0]->getDType(), inputs[0]->getOrder());
        };

    }

    namespace op {
        MV_REGISTER_OP(CTCDecoder)
        .setInputs({"data", "seq"})
        .setOutputs({"output"})
        .setArg<bool>("ctc_merge_repeated")
        .setInputCheck(op_ctcdecoder::inputCheckFcn)
        .setOutputDef(op_ctcdecoder::outputDefFcn)
        .setTypeTrait({"executable", "exposed"});
    }

}
