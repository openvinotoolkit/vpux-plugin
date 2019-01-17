#include "include/mcm/tensor/dtype/dtype_registry.hpp"
#include "include/mcm/tensor/dtype/dtype.hpp"

namespace mv
{

    static std::function<BinaryData(const std::vector<double>&)> toBinaryFunc =
    [](const std::vector<double> & vals)->mv::BinaryData
    {
        std::vector<int8_t> res(vals.begin(), vals.end());
        mv::BinaryData bdata(mv::DTypeType::Int8);
        bdata.setI8(std::move(res));
        return bdata;
    };

    MV_REGISTER_DTYPE("Int8").setToBinaryFunc(toBinaryFunc);
}
