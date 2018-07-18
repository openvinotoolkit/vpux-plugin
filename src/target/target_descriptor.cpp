#include "include/mcm/target/target_descriptor.hpp"

std::string mv::TargetDescriptor::toString(Target target)
{
    switch (target)
    {

        case Target::ma2480:
            return "ma2480";
        
        default:
            return "unknown";

    }
}

mv::Target mv::TargetDescriptor::toTarget(const std::string& str)
{
    if (str == "ma2480")
        return Target::ma2480;
    
    return Target::Unknown;
}  

mv::DType mv::TargetDescriptor::toDType(const std::string& str)
{
    if (str == "fp16")
        return DType::Float;
    
    return DType::Unknown;
}

mv::Order mv::TargetDescriptor::toOrder(const std::string& str)
{
    // TODO update
    if (str == "planar")
        return Order::LastDimMajor;
    
    return Order::Unknown;
}

mv::TargetDescriptor::TargetDescriptor(const std::string& filePath) :
target_(Target::Unknown),
globalDType_(DType::Unknown),
globalOrder_(Order::Unknown)
{

    if (!filePath.empty())
        if (!load(filePath))
            throw ArgumentError("filePath", filePath, 
                "Unable to parse target descriptor - error reading or invalid");

}

void mv::TargetDescriptor::reset()
{
    target_ = Target::Unknown;
    globalDType_ = DType::Unknown;
    globalOrder_ = Order::Unknown;
    adaptationPasses_.clear();
    optimizationPasses_.clear();
    finalizationPasses_.clear();
    serializationPasses_.clear();
    validationPasses_.clear();
}

bool mv::TargetDescriptor::load(const std::string& filePath)
{

    JSONTextParser parser(jsonParserBufferLenght_);

    json::Object jsonDescriptor;

    try 
    {

        json::Value jsonRoot;
        if (!parser.parseFile(filePath, jsonRoot))
            return false;
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
            globalDType_ = toDType(jsonDescriptor["dtype"]["global"].get<std::string>());
            if (globalDType_ == DType::Unknown)
            {
                reset();
                return false;
            }

        }

    }

    if (jsonDescriptor["order"].valueType() != json::JSONType::Object)
    {
        reset();
        return false;
    }
    else
    {
        if (!jsonDescriptor["order"].hasKey("global"))
        {
            reset();
            return false;
        }
        else
        {
            globalOrder_ = toOrder(jsonDescriptor["order"]["global"].get<std::string>());
            if (globalOrder_ == Order::Unknown)
            {
                reset();
                return false;
            }

        }

    }

    if (jsonDescriptor.hasKey("pass"))
    {

        if (jsonDescriptor["pass"].valueType() != json::JSONType::Object)
        {
            reset();
            return false;
        }

        std::vector<std::string> keys = jsonDescriptor["pass"].getKeys();

        for (unsigned i = 0; i < keys.size(); ++i)
        {
            std::vector<std::string> *passCollection = nullptr;
            if (keys[i] == "adapt")
                passCollection = &adaptationPasses_;
            else if (keys[i] == "optimize")
                passCollection = &optimizationPasses_;
            else if (keys[i] == "finalize")
                passCollection = &finalizationPasses_;
            else if (keys[i] == "serialize")
                passCollection = &serializationPasses_;
            else if (keys[i] == "validate")
                passCollection = &validationPasses_;
            else
            {
                reset();
                return false;
            }

            if (jsonDescriptor["pass"][keys[i]].valueType() != json::JSONType::Array)
            {
                reset();
                return false;
            }

            for (unsigned j = 0; j < jsonDescriptor["pass"][keys[i]].size(); ++j)
            {

                if (jsonDescriptor["pass"][keys[i]][j].valueType() != json::JSONType::String)
                {
                    reset();
                    return false;
                }

                passCollection->push_back(jsonDescriptor["pass"][keys[i]][j].get<std::string>());

            }

        }

    }

    if (jsonDescriptor["ops"].valueType() != json::JSONType::Array)
    {
        reset();
        return false;
    }
    else
    {

        for (unsigned i = 0; i < jsonDescriptor["ops"].size(); ++i)
        {

            if (jsonDescriptor["pass"]["ops"][i].valueType() != json::JSONType::String)
            {
                reset();
                return false;
            }

            std::string opStr = jsonDescriptor["pass"]["ops"][i].get<std::string>();
            auto it = opsStrings.begin();
            for (; it != opsStrings.end(); ++it)
                if (it->second == opStr)
                    ops_.insert(it->first);
            
            if (it == opsStrings.end())
            {
                reset();
                return false;
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
    root["order"] = Printable::toString(globalOrder_);
    root["dtype"] = Printable::toString(globalDType_);
    root["ops"] = json::Array();

    for (auto it = ops_.begin(); it != ops_.end(); ++it)
        root["ops"].append(opsStrings.at(*it));

    root["pass"]["adapt"] = json::Array();
    root["pass"]["optimize"] = json::Array();
    root["pass"]["finalize"] = json::Array();
    root["pass"]["serialize"] = json::Array();
    root["pass"]["validate"] = json::Array();

    for (auto it = adaptationPasses_.begin(); it != adaptationPasses_.end(); ++it)
        root["pass"]["adapt"].append(*it);

    for (auto it = optimizationPasses_.begin(); it != optimizationPasses_.end(); ++it)
        root["pass"]["optimize"].append(*it);

    for (auto it = finalizationPasses_.begin(); it != finalizationPasses_.end(); ++it)
        root["pass"]["finalize"].append(*it);

    for (auto it = serializationPasses_.begin(); it != serializationPasses_.end(); ++it)
        root["pass"]["serialize"].append(*it);

    for (auto it = validationPasses_.begin(); it != validationPasses_.end(); ++it)
        root["pass"]["validate"].append(*it);

    descFile << root.stringifyPretty();
    descFile.close();

    return true;

}

void mv::TargetDescriptor::setTarget(Target target)
{
    if (target == Target::Unknown)
        throw ArgumentError("target", "unknown", "Defining target as unknown is illegal");
    target_ = target;
}

void mv::TargetDescriptor::setDType(DType dType)
{
    if (dType == DType::Unknown)
        throw ArgumentError("dType", "unknown", "Defining dType as unknown is illegal");
    globalDType_ = dType;
}

void mv::TargetDescriptor::setOrder(Order order)
{
    if (order == Order::Unknown)
        throw ArgumentError("order", "unknown", "Defining order as unknown is illegal");
    globalOrder_ = order;
}

bool mv::TargetDescriptor::appendAdaptPass(const std::string& pass, int pos)
{
    if (pos > (int)adaptationPasses_.size() || pos < -1)
        return false;

    if (pos == (int)adaptationPasses_.size() || pos == -1)
    {
        adaptationPasses_.push_back(pass);
        return true;
    }

    auto result = adaptationPasses_.insert(adaptationPasses_.begin() + pos, pass);

    if (result != adaptationPasses_.end())
        return true;

    return false;
}

bool mv::TargetDescriptor::appendOptPass(const std::string& pass, int pos)
{
    if (pos > (int)optimizationPasses_.size() || pos < -1)
        return false;

    if (pos == (int)optimizationPasses_.size() || pos == -1)
    {
        optimizationPasses_.push_back(pass);
        return true;
    }

    auto result = optimizationPasses_.insert(optimizationPasses_.begin() + pos, pass);

    if (result != optimizationPasses_.end())
        return true;

    return false;
}

bool mv::TargetDescriptor::appendFinalPass(const std::string& pass, int pos)
{
    if (pos > (int)finalizationPasses_.size() || pos < -1)
        return false;

    if (pos == (int)finalizationPasses_.size() || pos == -1)
    {
        finalizationPasses_.push_back(pass);
        return true;
    }

    auto result = finalizationPasses_.insert(finalizationPasses_.begin() + pos, pass);

    if (result != finalizationPasses_.end())
        return true;

    return false;
}

bool mv::TargetDescriptor::appendSerialPass(const std::string& pass, int pos)
{
    if (pos > (int)serializationPasses_.size() || pos < -1)
        return false;

    if (pos == (int)serializationPasses_.size() || pos == -1)
    {
        serializationPasses_.push_back(pass);
        return true;
    }

    auto result = serializationPasses_.insert(serializationPasses_.begin() + pos, pass);

    if (result != serializationPasses_.end())
        return true;

    return false;
}

bool mv::TargetDescriptor::appendValidPass(const std::string& pass, int pos)
{
    if (pos > (int)validationPasses_.size() || pos < -1)
        return false;

    if (pos == (int)validationPasses_.size() || pos == -1)
    {
        validationPasses_.push_back(pass);
        return true;
    }

    auto result = validationPasses_.insert(validationPasses_.begin() + pos, pass);

    if (result != validationPasses_.end())
        return true;

    return false;
}

bool mv::TargetDescriptor::removeAdaptPass(const std::string& pass)
{
    auto passIt = std::find(adaptationPasses_.begin(), adaptationPasses_.end(), pass);
    if (passIt != adaptationPasses_.end())
    {
        adaptationPasses_.erase(passIt);
        return true;
    }
    return false;
}

bool mv::TargetDescriptor::removeOptPass(const std::string& pass)
{
    auto passIt = std::find(optimizationPasses_.begin(), optimizationPasses_.end(), pass);
    if (passIt != optimizationPasses_.end())
    {
        optimizationPasses_.erase(passIt);
        return true;
    }
    return false;
}

bool mv::TargetDescriptor::removeFinalPass(const std::string& pass)
{
    auto passIt = std::find(finalizationPasses_.begin(), finalizationPasses_.end(), pass);
    if (passIt != finalizationPasses_.end())
    {
        finalizationPasses_.erase(passIt);
        return true;
    }
    return false;
}

bool mv::TargetDescriptor::removeSerialPass(const std::string& pass)
{
    auto passIt = std::find(serializationPasses_.begin(), serializationPasses_.end(), pass);
    if (passIt != serializationPasses_.end())
    {
        serializationPasses_.erase(passIt);
        return true;
    }
    return false;
}

bool mv::TargetDescriptor::removeValidPass(const std::string& pass)
{
    auto passIt = std::find(validationPasses_.begin(), validationPasses_.end(), pass);
    if (passIt != validationPasses_.end())
    {
        validationPasses_.erase(passIt);
        return true;
    }
    return false;
}

bool mv::TargetDescriptor::defineOp(OpType op)
{
    if (ops_.find(op) == ops_.end())
    {
        ops_.insert(op);
        return true;
    }
    return false;
}

bool mv::TargetDescriptor::undefineOp(OpType op)
{
    auto opIt = ops_.find(op);
    if (opIt != ops_.end())
    {
        ops_.erase(op);
        return true;
    }
    return false;
}

bool mv::TargetDescriptor::opSupported(OpType op) const
{
    if (ops_.find(op) != ops_.end())
        return true;
    return false;
}

std::size_t mv::TargetDescriptor::adaptPassesCount() const
{
    return adaptationPasses_.size();
}

std::size_t mv::TargetDescriptor::optPassesCount() const
{
    return optimizationPasses_.size();
}

std::size_t mv::TargetDescriptor::finalPassesCount() const
{
    return finalizationPasses_.size();
}

std::size_t mv::TargetDescriptor::serialPassesCount() const
{
    return serializationPasses_.size();
}

std::size_t mv::TargetDescriptor::validPassesCount() const
{
    return validationPasses_.size();
}

const std::vector<std::string>& mv::TargetDescriptor::adaptPasses() const
{
    return adaptationPasses_;
}

const std::vector<std::string>& mv::TargetDescriptor::optPasses() const
{
    return optimizationPasses_;
}

const std::vector<std::string>& mv::TargetDescriptor::finalPasses() const
{
    return finalizationPasses_;
}

const std::vector<std::string>& mv::TargetDescriptor::serialPasses() const
{
    return serializationPasses_;
}

const std::vector<std::string>& mv::TargetDescriptor::validPasses() const
{
    return validationPasses_;
}

mv::Target mv::TargetDescriptor::getTarget() const
{
    return target_;
}

mv::DType mv::TargetDescriptor::getDType() const
{
    return globalDType_;
}

mv::Order mv::TargetDescriptor::getOrder() const
{
    return globalOrder_;
}

