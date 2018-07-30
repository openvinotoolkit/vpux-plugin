#ifndef MV_JSON_NUMBER_INTEGER_HPP_
#define MV_JSON_NUMBER_INTEGER_HPP_

#include <sstream>
#include "include/mcm/base/json/value_content.hpp"

namespace mv
{

    namespace json
    {

        class NumberInteger : public detail::ValueContent
        {

            long long value_;

        public:

            NumberInteger(long long value);
            explicit operator long long&() override;
            std::string stringify() const override;

        };  

    }

}

#endif // MV_JSON_NUMBER_INTEGER_HPP_