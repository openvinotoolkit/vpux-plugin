#include "include/mcm/base/attribute_registry.hpp"
#include "include/mcm/base/exception/attribute_error.hpp"
#include "include/mcm/base/attribute.hpp"
#include "include/mcm/target/keembay/ppe_task.hpp"

namespace mv
{

    namespace attr
    {

    static mv::json::Value toJSON(const Attribute& a)
    {
        auto d = a.get<PPETask>();
        return json::Value(d.toString());
    }

    static Attribute fromJSON(const json::Value& v)
    {
        if (v.valueType() != json::JSONType::String)
            throw AttributeError(v, "Unable to convert JSON value of type " + json::Value::typeName(v.valueType()) +
                " to mv::DType");

        //return PPETask(v.get<std::string>());
        return PPETask();
    }

    static std::string toString(const Attribute& a)
    {
        return "PPETask::" + a.get<PPETask>().toString();
    }

    MV_REGISTER_ATTR(PPETask)
        .setToJSONFunc(toJSON)
        .setFromJSONFunc(fromJSON)
        .setToStringFunc(toString);

    }

}
