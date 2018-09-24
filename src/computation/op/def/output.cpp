#include "include/mcm/computation/op/def/output.hpp"

mv::op::Output::Output(const std::string &name) : 
ComputationOp(OpType::Output, name),
SinkOp(OpType::Output, 1, name)
{
    set<bool>("executable", false);
}


bool mv::op::Output::setInputTensor(Data::TensorIterator &tensor, std::size_t idx)
{

    bool result = SinkOp::setInputTensor(tensor, idx);
    if (result)
        set<Shape>("shape",  tensor->getShape());
    return result;

}

mv::Tensor mv::op::Output::getOutputDef(std::size_t)
{
    throw(OpError(*this, "Attempt of obtaining the output tensor"));
}


bool mv::op::Output::isHardwarizeable(json::Object&)
{
    return false;
}
