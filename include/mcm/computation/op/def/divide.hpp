#ifndef DIVIDE_HPP_
#define DIVIDE_HPP_

#include "include/mcm/computation/op/eltwise_op.hpp"

namespace mv
{

    namespace op
    {

        class Divide : public EltwiseOp
        {

        public:

            Divide(const string &name);

        };

    }

}

#endif // MULTIPLY_HPP_