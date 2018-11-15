#include "include/mcm/pass/pass_registry.hpp"

namespace mv
{

    MV_DEFINE_REGISTRY(std::string, mv::pass::PassEntry)

}

mv::pass::PassRegistry& mv::pass::PassRegistry::instance()
{
    
    return static_cast<PassRegistry&>(Registry<std::string, PassEntry>::instance());

}

void mv::pass::PassRegistry::run(std::string name, ComputationModel& model, TargetDescriptor& targetDescriptor, json::Object& compDescriptor, json::Object& output)
{   
    PassEntry* const passPtr = find(name);
    if (passPtr)
        passPtr->run(model, targetDescriptor, compDescriptor, output);
    else
        throw MasterError("PassRegistry", "Invokation of unregistered pass " + name);
}