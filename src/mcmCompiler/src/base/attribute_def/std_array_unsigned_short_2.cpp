//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/base/attribute_registry.hpp"
#include "include/mcm/base/exception/attribute_error.hpp"
#include "include/mcm/base/attribute.hpp"
#include <array>

namespace mv
{

    namespace attr_us2
    {

        static mv::json::Value toJSON(const Attribute& a)
        {
            json::Array output;
            auto arr = a.get<std::array<unsigned short, 2>>();
            output.append(static_cast<long long>(arr[0]));
            output.append(static_cast<long long>(arr[1]));
            return output;
        }

        static Attribute fromJSON(const json::Value& v)
        {
            if (v.valueType() != json::JSONType::Array)
                throw AttributeError(v, "Unable to convert JSON value of type " + json::Value::typeName(v.valueType()) + 
                    " to std::array<unsigned short, 2>");

            if (v.size() != 2)
                throw AttributeError(v, "Unable to convert json::Array of size " + std::to_string(v.size()) +  
                    " to std::array<unsigned short, 2>");
            
            std::array<unsigned short, 2> output;
            for (std::size_t i = 0; i < v.size(); ++i)
            {
                if (v[i].valueType() != json::JSONType::NumberInteger)
                    throw AttributeError(v, "Unable to convert JSON value of type " + json::Value::typeName(v[i].valueType()) + 
                    " to unsigned short (during the conversion to std::array<unsigned short, 2>)");
                output[i] = static_cast<unsigned short>(v[i].get<long long>());
            }

            return output;
        }

        static std::string toString(const Attribute& a)
        {
            auto arr = a.get<std::array<unsigned short, 2>>();
            std::string output = "{" + std::to_string(arr[0]) + ", " + std::to_string(arr[1]) + "}";
            return output;
        }

        static std::vector<uint8_t> toBinary(const Attribute& a)
        {
            union Tmp
            {
                unsigned short n;
                uint8_t bytes[sizeof(unsigned short)];
            };
            auto vec = a.get<std::array<unsigned short, 2>>();
            std::vector<uint8_t> toReturn(sizeof(unsigned short) * 2, 0);
            unsigned i = 0;
            for(auto v: vec)
            {
                Tmp tmp = {v};
                for(size_t j = 0; j < sizeof(unsigned short); ++j)
                    toReturn[i++] = tmp.bytes[j];
            }
            return toReturn;
        }


    }

    namespace attr {
        MV_REGISTER_ATTR(std::array<unsigned short COMMA 2>, InitArrayShort2)
            .setToJSONFunc(attr_us2::toJSON)
            .setFromJSONFunc(attr_us2::fromJSON)
            .setToStringFunc(attr_us2::toString)
            .setToBinaryFunc(attr_us2::toBinary);

    }

}
