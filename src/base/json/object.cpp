#include "include/mcm/base/json/object.hpp"

mv::json::Object::Object() :
Value()
{
    
}

mv::json::Object::Object(Object& owner, const std::string& key) :
Value(owner, key, JSONType::Object)
{
    
}

bool mv::json::Object::emplace(const std::string& key, float value)
{
    std::unique_ptr<NumberFloat> ptr(new NumberFloat(*this, key, value));
    members_.emplace(key, std::move(ptr));
    return true;
}

bool mv::json::Object::emplace(const std::string& key, int value)
{
    std::unique_ptr<NumberInteger> ptr(new NumberInteger(*this, key, value));
    members_.emplace(key, std::move(ptr));
    return true;
}

bool mv::json::Object::emplace(const std::string& key, const std::string& value)
{
    std::unique_ptr<String> ptr(new String(*this, key, value));
    members_.emplace(key, std::move(ptr));
    return true;
}

bool mv::json::Object::emplace(const std::string& key, bool value)
{
    std::unique_ptr<Bool> ptr(new Bool(*this, key, value));
    members_.emplace(key, std::move(ptr));
    return true;
}

bool mv::json::Object::emplace(const std::string& key)
{
    std::unique_ptr<Null> ptr(new Null(*this, key));
    members_.emplace(key, std::move(ptr));
    return true;
}

void mv::json::Object::erase(const std::string& key)
{
    members_.erase(key);
}

unsigned mv::json::Object::size() const
{
    return members_.size();
}

std::string mv::json::Object::stringify() const
{

    std::string output = "{";

    auto it = members_.begin();

    if (it != members_.end())
    {

        auto str = [&it](){ return "\"" + it->first + "\":" + it->second->stringify(); };
        output += str();
        ++it;

        for (; it != members_.end(); ++it)
            output += "," + str();

    }

    output += "}";
    return output;
    
}

mv::json::Value& mv::json::Object::operator[](const std::string& key)
{

    if (members_.find(key) == members_.end())
    {
        emplace(key);
    }

    return *members_[key].get();

}