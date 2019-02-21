#include "include/mcm/target/target_descriptor.hpp"

std::string mv::TargetDescriptor::toString(Target target)
{
    switch (target)
    {

        case Target::ma2480:
            return "ma2480";

        case Target::ma2490:
            return "ma2490";

        default:
            return "unknown";

    }
}

mv::Target mv::TargetDescriptor::toTarget(const std::string& str)
{
    if (str == "ma2480")
        return Target::ma2480;

    if (str == "ma2490")
        return Target::ma2490;

    return Target::Unknown;
}

mv::TargetDescriptor::TargetDescriptor(const std::string& filePath) :
target_(Target::Unknown),
globalDType_("Float16")
{

    if (!filePath.empty())
        if (!load(filePath))
            throw ArgumentError(*this, "filePath", filePath,
                "Unable to parse target descriptor - error reading or invalid");

}

void mv::TargetDescriptor::reset()
{
    target_ = Target::Unknown;
    globalDType_ = DType("Float16");
    ops_.clear();
    memoryDefs_.clear();
}

bool mv::TargetDescriptor::load(const std::string& filePath)
{

    JSONTextParser parser(jsonParserBufferLenght_);
    json::Object jsonDescriptor;

    try
    {

        json::Value jsonRoot;
        if (!parser.parseFile(filePath, jsonRoot))
        {
            throw ArgumentError(*this, "filePath", filePath,
                "Unable to parse target descriptor - error reading");
        }
        if (jsonRoot.valueType() != json::JSONType::Object)
            return false;
        else
            jsonDescriptor = jsonRoot.get<json::Object>();

    }
    catch (ParsingError& e)
    {
        return false;
    }

    if (!jsonDescriptor.hasKey("target"))
        return false;
    else
    {
        target_ = toTarget(jsonDescriptor["target"].get<std::string>());
        if (target_ == Target::Unknown)
            return false;
    }

    if (!jsonDescriptor.hasKey("dtype"))
    {
        reset();
        return false;
    }

    if (jsonDescriptor["dtype"].valueType() != json::JSONType::Object)
    {
        reset();
        return false;
    }
    else
    {
        if (!jsonDescriptor["dtype"].hasKey("global"))
        {
            reset();
            return false;
        }
        else
        {
            globalDType_ = DType(jsonDescriptor["dtype"]["global"].get<std::string>());
        }

    }

    if (jsonDescriptor["ops"].valueType() != json::JSONType::Object)
    {
        reset();
        return false;
    }
    else
    {

        std::vector<std::string> keys = jsonDescriptor["ops"].getKeys();

        for (unsigned i = 0; i < keys.size(); ++i)
        {
            std::string opStr = keys.at(i);
            if (!op::OpRegistry::checkOpType(opStr))
            {
                reset();
                return false;
            }
            ops_.insert(opStr);
            mv::Element e(opStr);

            for (unsigned j = 0; j < jsonDescriptor["ops"][opStr].size(); ++j) // Resource
            {
                std::vector<std::string> resource_keys = jsonDescriptor["ops"][opStr].getKeys();
                std::string platform_name = resource_keys[j];

                std::vector<std::string> serial_list;
                for (unsigned k = 0; k < jsonDescriptor["ops"][opStr][platform_name]["serial_description"].size(); ++k) // Resource
                {
                    std::string v = jsonDescriptor["ops"][opStr][platform_name]["serial_description"][k].get<std::string>();
                    serial_list.push_back(v);
                }

                e.set<std::vector<std::string>>("serial_view", serial_list);

                serialDescriptions_.insert(std::make_pair(opStr+":"+platform_name, e));

            }
        }
    }

    if (jsonDescriptor["postOps"].valueType() != json::JSONType::Object)
    {
        reset();
        return false;
    }
    else
    {

        std::vector<std::string> keys = jsonDescriptor["postOps"].getKeys();

        for (unsigned i = 0; i < keys.size(); ++i)
        {
            std::string opStr = keys.at(i);
            if (!op::OpRegistry::checkOpType(opStr))
            {
                reset();
                return false;
            }
            postOps_.insert(opStr);
            mv::Element e(opStr);

            for (unsigned j = 0; j < jsonDescriptor["ops"][opStr].size(); ++j) // Resource
            {
                std::vector<std::string> resource_keys = jsonDescriptor["ops"][opStr].getKeys();
                std::string platform_name = resource_keys[j];

                std::vector<std::string> serial_list;
                for (unsigned k = 0; k < jsonDescriptor["ops"][opStr][platform_name]["serial_description"].size(); ++k) // Resource
                {
                    std::string v = jsonDescriptor["ops"][opStr][platform_name]["serial_description"][k].get<std::string>();
                    serial_list.push_back(v);
                }

                e.set<std::vector<std::string>>("serial_view", serial_list);

                serialDescriptions_.insert(std::make_pair(opStr+":"+platform_name, e));

            }
        }
    }

    if (jsonDescriptor["resources"].valueType() != json::JSONType::Object)
    {
        reset();
        return false;
    }
    else
    {
        if (jsonDescriptor["resources"]["memory"].valueType() != json::JSONType::Array)
        {
            reset();
            return false;
        }
        else
        {

            for (std::size_t i = 0; i < jsonDescriptor["resources"]["memory"].size(); ++i)
            {

                std::string name;
                long long size;
                std::size_t alignment;
                std::size_t dataTypeSize;

                if (!jsonDescriptor["resources"]["memory"][i].hasKey("name") ||
                    !jsonDescriptor["resources"]["memory"][i].hasKey("size") ||
                    !jsonDescriptor["resources"]["memory"][i].hasKey("alignment") ||
                    !jsonDescriptor["resources"]["memory"][i].hasKey("dataTypeSize"))
                {
                    reset();
                    return false;
                }

                if (jsonDescriptor["resources"]["memory"][i]["name"].valueType() != json::JSONType::String ||
                    jsonDescriptor["resources"]["memory"][i]["size"].valueType() != json::JSONType::NumberInteger ||
                    jsonDescriptor["resources"]["memory"][i]["alignment"].valueType() != json::JSONType::NumberInteger ||
                    jsonDescriptor["resources"]["memory"][i]["dataTypeSize"].valueType() != json::JSONType::NumberInteger)
                {
                    reset();
                    return false;
                }

                name = jsonDescriptor["resources"]["memory"][i]["name"].get<std::string>();
                size = jsonDescriptor["resources"]["memory"][i]["size"].get<long long>();
                alignment = jsonDescriptor["resources"]["memory"][i]["alignment"].get<long long>();
                dataTypeSize = jsonDescriptor["resources"]["memory"][i]["dataTypeSize"].get<long long>();

                if (size < 0)
                {
                    reset();
                    return false;
                }

                memoryDefs_[name] = {size, alignment, dataTypeSize};

            }

        }

    }

    return true;

}

bool mv::TargetDescriptor::save(const std::string& filePath)
{

    // open a file in read mode.
    std::ofstream descFile;
    descFile.open(filePath, std::ios::out | std::ios::trunc);

    if (!descFile.is_open())
        return false;

    json::Object root;
    root["target"] = toString(target_);
    root["dtype"] = globalDType_.toString();
    root["ops"] = json::Array();

    for (auto it = ops_.begin(); it != ops_.end(); ++it)
        root["ops"].append(*it);

    descFile << root.stringifyPretty();
    descFile.close();

    return true;

}

void mv::TargetDescriptor::setTarget(Target target)
{
    if (target == Target::Unknown)
        throw ArgumentError(*this, "target", "unknown", "Defining target as unknown is illegal");
    target_ = target;
}

void mv::TargetDescriptor::setDType(DType dType)
{
    globalDType_ = dType;
}

bool mv::TargetDescriptor::defineOp(const std::string& op)
{
    if (ops_.find(op) == ops_.end())
    {
        if (!op::OpRegistry::checkOpType(op))
            return false;

        ops_.insert(op);
        return true;
    }
    return false;
}

bool mv::TargetDescriptor::undefineOp(const std::string& op)
{
    auto opIt = ops_.find(op);
    if (opIt != ops_.end())
    {
        ops_.erase(op);
        return true;
    }
    return false;
}

bool mv::TargetDescriptor::opSupported(const std::string& op) const
{
    if (ops_.find(op) != ops_.end())
        return true;
    return false;
}

bool mv::TargetDescriptor::opSupportedAsPostOp(const std::string& op) const
{
    if (postOps_.find(op) != postOps_.end())
        return true;
    return false;
}


bool mv::TargetDescriptor::defineMemory(const std::string& name, long long size, std::size_t alignment, std::size_t dataTypeSize)
{
    if (size < 0)
        return false;

    if (memoryDefs_.find(name) == memoryDefs_.end())
    {
        memoryDefs_[name] = {size, alignment, dataTypeSize};
        return true;
    }
    return false;
}

bool mv::TargetDescriptor::undefineMemory(const std::string& name)
{
    auto defIt = memoryDefs_.find(name);
    if (defIt != memoryDefs_.end())
    {
        memoryDefs_.erase(defIt);
        return true;
    }
    return false;
}

mv::Target mv::TargetDescriptor::getTarget() const
{
    return target_;
}

mv::DType mv::TargetDescriptor::getDType() const
{
    return globalDType_;
}

mv::Element mv::TargetDescriptor::getSerialDefinition(std::string op_name, std::string platform_name) const
{
    return serialDescriptions_.at(op_name+":"+platform_name);
}

const std::map<std::string, mv::TargetDescriptor::MemoryDescriptor>& mv::TargetDescriptor::memoryDefs() const
{
    return memoryDefs_;
}

std::string mv::TargetDescriptor::getLogID() const
{
    return "TargetDescriptor";
}
