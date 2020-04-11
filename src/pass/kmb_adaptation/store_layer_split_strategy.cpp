#include "include/mcm/pass/pass_registry.hpp"
#include "include/mcm/pass/pass_utils.hpp"
#include "include/mcm/op_model.hpp"
#include "include/mcm/computation/model/control_model.hpp"
#include "include/mcm/computation/model/data_model.hpp"
#include "include/mcm/op_model.hpp"
#include <regex>

static void storeLayerSplitStrategyFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);
static void storeTensorPlacementFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);
static void storeLayerSparsityStrategyFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);

namespace mv
{

    namespace pass
    {

        MV_REGISTER_PASS(StoreLayerSplitStrategy)
        .setFunc(storeLayerSplitStrategyFcn)
        .setDescription(
            "This pass applies layer splitting strategies."
        );

        MV_REGISTER_PASS(StoreLayerSparsityStrategy)
        .setFunc(storeLayerSparsityStrategyFcn)
        .setDescription(
            "This pass applies layer sparsity strategies."
        );

        MV_REGISTER_PASS(StoreTensorPlacement)
        .setFunc(storeTensorPlacementFcn)
        .setDescription(
            "This pass applies the memory location overrides for the Tensors from the JSON file."
        );
    }
}

void storeStrategy(mv::Data::OpListIterator& opIt, std::vector<mv::Element>& strategyList)
{
    for (auto s: strategyList)
    {
        std::string& name_filter = s.get<std::string>("name_filter");
        std::regex exp(name_filter);
        if (std::regex_match(opIt->getName(), exp))
        {
            auto strategy = s.get<std::string>("strategy");
            opIt->set<std::string>("splitStrategy", strategy);
            if(strategy == "SplitOverK" || strategy == "HKSwitch")
                opIt->set<bool>("multiCast", true);
            else
                opIt->set<bool>("multiCast", false);
        }
    }
}

void storeLayerSplitStrategyFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&)
{

    MV_PROFILED_FUNCTION(MV_PROFILE_PASS)
    auto globalParams = model.getGlobalConfigParams();

    if (!globalParams->hasAttr("split_strategy"))
    {
        pass.log(mv::Logger::MessageType::Debug, "No custom splitting strategy provided, exiting...");
        return;
    }

    auto strategyList = globalParams->get<std::vector<mv::Element>>("split_strategy");

    mv::OpModel om(model);

    for (auto opIt = om.opBegin(); opIt != om.opEnd(); ++opIt)
    {
        auto opType = opIt->getOpType();
        if (!(opType == "Output"))
            storeStrategy(opIt, strategyList);
    }

    pass.log(mv::Logger::MessageType::Debug, "----splitting strategies for individual layers----");
    for (auto opIt = om.opBegin(); opIt != om.opEnd(); ++opIt)
    {
        if (opIt->hasAttr("splitStrategy"))
        {
            pass.log(mv::Logger::MessageType::Debug, "op: " + opIt->getName() +
                        " | strategy = " + opIt->get<std::string>("splitStrategy"));
        }
    }
}

void storeLayerSparsityStrategyFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&)
{

    MV_PROFILED_FUNCTION(MV_PROFILE_PASS)
    auto globalParams = model.getGlobalConfigParams();

    if (!globalParams->hasAttr("sparsity_strategy"))
    {
        pass.log(mv::Logger::MessageType::Debug, "No custom sparsity strategy provided, exiting...");
        return;
    }

    auto strategyList = globalParams->get<std::vector<mv::Element>>("sparsity_strategy");

    mv::OpModel om(model);

    for (auto opIt = om.opBegin(); opIt != om.opEnd(); ++opIt)
    {
        auto opType = opIt->getOpType();
        bool strategyFound = false;
        if (opType != "Output" && opType != "Input")
        {
            for (auto s: strategyList)
            {
                std::string& name_filter = s.get<std::string>("name_filter");
                std::regex exp(name_filter);
                if (std::regex_match(opIt->getName(), exp))
                {
                    bool inputActivationSparsity, outputActivationSparsity, weightsSparsity = false;
                    if(s.hasAttr("inputActivationSparsity"))
                        inputActivationSparsity = s.get<bool>("inputActivationSparsity");
                    if(s.hasAttr("outputActivationSparsity"))
                        outputActivationSparsity = s.get<bool>("outputActivationSparsity");
                    if(s.hasAttr("weightsSparsity"))
                        weightsSparsity = s.get<bool>("weightsSparsity");

                    opIt->set<bool>("inputActivationSparsity", inputActivationSparsity);
                    opIt->set<bool>("outputActivationSparsity", outputActivationSparsity);
                    opIt->set<bool>("weightsSparsity", weightsSparsity);
                    strategyFound = true;
                }
            }
            if(!strategyFound)
            {
                opIt->set<bool>("inputActivationSparsity", false);
                opIt->set<bool>("outputActivationSparsity", false);
                opIt->set<bool>("weightsSparsity", false);
            }
            // Check need for activation sparsity
            mv::DataModel dm(model);
            auto flows = opIt->getOutputTensor(0)->get<std::set<std::string>>("flows");
            for(auto& flowStr: flows)
            {
                auto flow = dm.getDataFlow(flowStr);
                if(checkA0SOHSparsityBug(flow))
                {
                    //opIt->getOutputTensor(0)->setSparse();
                    opIt->getOutputTensor(0)->set<bool>("needs_sparse", true);
                    if (om.getSourceOp(opIt->getInputTensor(0))->getOpType() == "Input")
                        opIt->getInputTensor(0)->set<bool>("needs_splits_aligned", true);
                    else
                        //opIt->getInputTensor(0)->setSparse();
                        opIt->getInputTensor(0)->set<bool>("needs_sparse", true);
                    break;
                }
            }
/*
            if(source->getOpType() != "DPUTask" ||
               source->get<std::string>("splitStrategy") == "SplitOverK" ||
               sink->getOpType() != "DPUTask" ||
               sink->get<std::string>("taskOp") != "Conv" ||
               sink->get<std::string>("splitStrategy") == "SplitOverK")
            {
                tensorSparsifiable = false;
                break;
            }
*/
        }
    }
}

void storeTensorPlacementFcn(const mv::pass::PassEntry& pass,
                                mv::ComputationModel& model,
                                mv::TargetDescriptor&,
                                mv::Element&,
                                mv::Element&)
{

    MV_PROFILED_FUNCTION(MV_PROFILE_PASS)
    //mv::Logger::setVerboseLevel(mv::VerboseLevel::Debug);

    auto globalParams = model.getGlobalConfigParams();
    mv::OpModel om(model);

    if (globalParams->hasAttr("tensor_placement_override"))
    {
        auto placementOverrideList = globalParams->get<std::vector<mv::Element>>("tensor_placement_override");


        for (auto tensorIt = om.tensorBegin() ; tensorIt != om.tensorEnd() ; ++tensorIt)
        {
            auto parentOp = om.getSourceOp(tensorIt);

            if(parentOp != om.opEnd())
                if(parentOp->getOpType() == "Input")
                {
                    continue;
                }

            bool found = false;
            for( auto s : placementOverrideList )
            {
                std::string& nameFilter = s.get<std::string>("name_filter");
                std::string& memLocation = s.get<std::string>("mem_location");
                bool forced = s.hasAttr("force");

                std::regex exp(nameFilter);
                if (std::regex_match(tensorIt->getName(),exp))
                {
                    found = true;
                    mv::Tensor::MemoryLocation location(memLocation,forced);
                    tensorIt->set<mv::Tensor::MemoryLocation>("Location",location);
                    pass.log(mv::Logger::MessageType::Debug,"setting tensor " +
                                tensorIt->getName() + " as " + location.toString());
                }
            }

            if((not found) and (not tensorIt->hasAttr("Location")))
            {
                tensorIt->set<mv::Tensor::MemoryLocation>("Location",mv::Tensor::MemoryLocation::DEFAULT);
                pass.log(mv::Logger::MessageType::Debug,"tensor " + tensorIt->getName() + "not found. setting to DEFAULT");
            }
        }
    }
    else
    {
        pass.log(mv::Logger::MessageType::Debug, "No tensor placement override provided, exiting...");

    }

    auto input = om.getInput();
    auto inputTensors = input->getOutputTensor();
    for (auto tensor : inputTensors)
    {
        if(!tensor->get<mv::Tensor::MemoryLocation>("Location").isDefault())
        {
            pass.log(mv::Logger::MessageType::Debug, "Found InputTensor " +
                        tensor->getName() + " description location in JSON. Will override with INPUT");
        }
        pass.log(mv::Logger::MessageType::Debug,"Found OutputTensor " +
                        tensor->getName() + " current location is " + tensor->get<mv::Tensor::MemoryLocation>("Location").toString() + " override with OUTPUT");
        //mark location forced so any adaptation pass will inheret opIt
        tensor->set<mv::Tensor::MemoryLocation>("Location", mv::Tensor::MemoryLocation("INPUT", true));
    }

    auto output = om.getOutput();
    auto outputTensors = output->getInputTensor();

    for ( auto tensor : outputTensors)
    {
        if(!tensor->get<mv::Tensor::MemoryLocation>("Location").isDefault())
        {
            pass.log(mv::Logger::MessageType::Debug,"Found OutputTensor " +
                        tensor->getName() + " description location in JSON. Will override with OUTPUT");
        }
        pass.log(mv::Logger::MessageType::Debug,"Found OutputTensor " +
                        tensor->getName() + " current location is " + tensor->get<mv::Tensor::MemoryLocation>("Location").toString() + " override with OUTPUT");
        //mark location forced so any adaptation pass will inheret opIt
        tensor->set<mv::Tensor::MemoryLocation>("Location", mv::Tensor::MemoryLocation("OUTPUT", true));
    }

    //if JSON wants us to override default location for tensors:
    if(globalParams->hasAttr("default_tensor_placement"))
    {
        std::string& defaultPlace = globalParams->get<std::string>("default_tensor_placement");
        for (auto tensorIt = om.tensorBegin() ; tensorIt != om.tensorEnd() ; ++tensorIt)
        {
            if(tensorIt->get<mv::Tensor::MemoryLocation>("Location").isDefault())
            {
                tensorIt->get<mv::Tensor::MemoryLocation>("Location").relocate(defaultPlace);
                pass.log(mv::Logger::MessageType::Debug,"Found OutputTensor " +
                        tensorIt->getName() + " overriding all to default placement " + defaultPlace);
            }
        }
    }
    //mv::Logger::setVerboseLevel(mv::VerboseLevel::Warning);
}
