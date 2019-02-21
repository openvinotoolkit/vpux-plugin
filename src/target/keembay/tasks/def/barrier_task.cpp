#include "include/mcm/computation/op/op_registry.hpp"
#include "include/mcm/computation/op/op.hpp"

namespace mv
{

    namespace op
    {

        static std::function<std::pair<bool, std::size_t>(const std::vector<Data::TensorIterator>&,
            const std::map<std::string, Attribute>&, std::string&)> inputCheckFcn =
            [](const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>&,
            std::string&) -> std::pair<bool, std::size_t>
        {
            return {true, 0};
        };
                
        static std::function<void(const std::vector<Data::TensorIterator>&, const std::map<std::string, Attribute>&,
            std::vector<Tensor>&)> outputDefFcn =
            [](const std::vector<Data::TensorIterator>& inputs, const std::map<std::string, Attribute>&, std::vector<Tensor>& outputs)
        {

        };

        MV_REGISTER_OP(BarrierTask)
        .setInputCheck(inputCheckFcn)
        .setOutputDef(outputDefFcn)
        .setArg<int>("group")
        .setArg<int>("index")
        .setArg<int>("numProducers")
        .setArg<int>("numConsumers")
        .setArg<int>("wait")
        .setTypeTrait({"executable"});

    }

}
