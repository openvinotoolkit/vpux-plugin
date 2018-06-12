#include "include/fathom/computation/op/sink_op.hpp"

mv::SinkOp::SinkOp(OpType opType, const string &name) :
ComputationOp(opType, name)
{
    addAttr("inputs", AttrType::ByteType, (byte_type)1);
}

mv::SinkOp::~SinkOp()
{

}

bool mv::SinkOp::setInput(DataContext::TensorIterator &tensor, byte_type idx)
{
    if (idx > 1)
        return false;

    input_ = tensor;
    addAttr("input0", AttrType::StringType, tensor->getName());
    logger_.log(Logger::MessageType::MessageDebug, "Set input 0 for " + toString() + " as " + tensor->toString());
    return true;
}

mv::DataContext::TensorIterator mv::SinkOp::getInput(byte_type idx)
{
    if (idx > 1)
        return DataContext::TensorIterator();

    return input_;

}

bool mv::SinkOp::hasInputDef()
{

    DataContext::TensorIterator emptyIt;
    
    if (input_ == emptyIt)
        return false;
    
    return true;

}

mv::byte_type mv::SinkOp::inputSlots()
{
    return 1;
}