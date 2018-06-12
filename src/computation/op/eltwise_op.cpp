#include "include/fathom/computation/op/eltwise_op.hpp"

mv::EltwiseOp::EltwiseOp(OpType eltwiseType, const string &name) :
ComputationOp(eltwiseType, name),
SourceOp(eltwiseType, 1, name),
SinkOp(eltwiseType, 2, name)
{

}

mv::EltwiseOp::~EltwiseOp()
{

}

mv::Tensor mv::EltwiseOp::getOutputDef(byte_type idx)
{

    if (idx > 0)
        return Tensor();

    if (!validOutputDef_())
        return Tensor();

    auto input0 = getInput(0);
    auto input0Shape = input0->getShape();
    auto input1Shape = getInput(1)->getShape();

    if (input0Shape != input1Shape)
    {
        logger_.log(Logger::MessageType::MessageError, "Unable to define output tensor for '" + name_ +
            "'because of inconsistent input 0 shape " + input0Shape.toString() + " and input 1 shape " + input1Shape.toString());
        return Tensor();
    }

    return Tensor(name_ + ":0", input0Shape, input0->getDType(), input0->getOrder());

}