#include "include/mcm/pass/pass_registry.hpp"
#include "include/mcm/op_model.hpp"
#include "include/mcm/computation/model/control_model.hpp"


static void kmbQuantizeConversionFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);

namespace mv
{

    namespace pass
    {

        MV_REGISTER_PASS(KMBQuantizeConversion)
        .setFunc(kmbQuantizeConversionFcn)
        .setDescription(
            "This pass inserts Quantize conversion layers between DPUTask-to-UPATask transitions (& vice-versa)."
        );

    }

}

void addQuantizationLayers(mv::OpModel om, std::vector<mv::Data::OpListIterator>& tasks, const mv::DType& dtypeNeededInInput)
{
    for(auto& task : tasks)
    {
        if (task->hasAttr("taskOp") && task->get<std::string>("taskOp") == "Quantize")
        {
            auto quantParams = task->get<mv::QuantizationParams>("quantParams");
            auto output = task->getOutputTensor(0);
            output->set<mv::QuantizationParams>("quantParams", quantParams);
            continue;
        }

        auto inputFlow = task.leftmostInput();
        auto outputDType = task->getOutputTensor(0)->getDType();
        std::size_t id = 0;
        while(inputFlow != om.flowEnd())
        {
            auto tensor = inputFlow->getTensor();
            auto tensorDType = tensor->getDType();

            // NOTE: Maybe here a check for mixed precision should be added
            if(!tensor->isPopulated() && tensorDType != dtypeNeededInInput)
            {
                auto quantize = om.uPATaskQuantize({tensor}, outputDType,
                            tensor->get<mv::QuantizationParams>("quantParams"), "Quantize" + task->getName() + std::to_string(id));
                quantize->set<std::string>("splitStrategy",
                            tensor->get<std::string>("splitStrategy"));
                auto quantizeOp = om.getSourceOp(quantize);
                quantizeOp->set<unsigned>("opId", task->get<unsigned>("opId"));
                auto backup = inputFlow;
                auto slot = backup->get<size_t>("sinkInput");
                ++inputFlow;
                om.undefineFlow(backup);
                task->setInputTensor(quantize, slot, false);
                om.defineFlow(quantize, task, slot);
                id++;
            }
            else
                ++inputFlow;
        }
    }
}

void addSliceQuantizationLayer(mv::OpModel om, std::vector<mv::Data::OpListIterator>& slices, const mv::DType& dtypeNeededInInput)
{
    std::vector <mv::Data::TensorIterator> sliceInputs = {};
    std::map <std::string, std::vector<mv::Data::OpListIterator>> sliceLeafs;
    std::map <std::string, std::vector<mv::Data::FlowListIterator>> sliceFlows;
    for (auto& slice : slices)
    {
        auto it = std::find (sliceInputs.begin(), sliceInputs.end(), slice->getInputTensor()[0]);
        if (it != sliceInputs.end())
        {
            sliceLeafs[slice->getInputTensor()[0]->getName()].push_back(slice);
            slices.erase(std::remove(slices.begin(), slices.end(), slice), slices.end());
        }
        else
        {
            sliceInputs.push_back(slice->getInputTensor()[0]);
            auto previousOpIt = om.getSourceOp(slice->getInputTensor(0));
            for (auto sinkFlow = previousOpIt.leftmostOutput(); sinkFlow != om.flowEnd(); ++sinkFlow)
            {
                sliceFlows[slice->getInputTensor()[0]->getName()].push_back(sinkFlow);
            }
        }
    }
    for(auto& slice : slices)
    {
        auto inputFlow = slice.leftmostInput();
        auto outputDType = slice->getOutputTensor(0)->getDType();
        std::size_t id = 0;
        while(inputFlow != om.flowEnd())
        {
            auto tensor = inputFlow->getTensor();
            auto tensorDType = tensor->getDType();

            // NOTE: Maybe here a check for mixed precision should be added
            if(!tensor->isPopulated() && tensorDType != dtypeNeededInInput)
            {
                auto quantize = om.uPATaskQuantize({tensor}, outputDType,
                            tensor->get<mv::QuantizationParams>("quantParams"), "Quantize" + slice->getName() + std::to_string(id));
                quantize->set<std::string>("splitStrategy",
                            tensor->get<std::string>("splitStrategy"));
                auto quantizeOp = om.getSourceOp(quantize);
                quantizeOp->set<unsigned>("opId", slice->get<unsigned>("opId"));
                auto backup = inputFlow;
                auto slot = backup->get<size_t>("sinkInput");
                ++inputFlow;
                for (auto flow:sliceFlows[tensor->getName()])
                {
                    om.undefineFlow(flow);
                }
                slice->setInputTensor(quantize, slot, false);
                om.defineFlow(quantize, slice, slot);
                for (auto op:sliceLeafs[tensor->getName()])
                {
                    op->setInputTensor(quantize, slot, false);
                    om.defineFlow(quantize, op, slot);
                }
                id++;
            }
            else
                ++inputFlow;
        }
    }
}

static void kmbQuantizeConversionFcn(const mv::pass::PassEntry&, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_PASS)

    mv::OpModel om(model);

    auto dpuTasks = om.getOps("DPUTask");
    auto upaTasks = om.getOps("UPATask");

    // NOTE: At this moment in the model, all the concats are implicit
    auto implicitConcats = om.getOps("ImplicitConcat");
    // NOTE: For now only operations with U8/DPU Tasks are streamed
    auto slices = om.getOps("Slice");

    auto U8 = mv::DType("UInt8");
    auto FP16 = mv::DType("Float16");

    std::shared_ptr<mv::Element> globalParams = model.getGlobalConfigParams();
    addQuantizationLayers(om, upaTasks, FP16);

    bool DPUTasksinSW = globalParams->hasAttr("DPUTasksinFloat") ? globalParams->get<bool>("DPUTasksinFloat") : false;
    if (!DPUTasksinSW)
    {
        addQuantizationLayers(om, dpuTasks, U8);
        addSliceQuantizationLayer(om, slices, U8);
    }
    // NOTE: Concat have the extra requirement that output tensor and input tensor have to match their DType, so
    // we split them in two vectors

    std::vector<mv::Data::OpListIterator> implicitConcatsU8;
    std::vector<mv::Data::OpListIterator> implicitConcatsFP16;

    for(auto& implicitConcat: implicitConcats)
    {
        auto outputDType = implicitConcat->getOutputTensor(0)->getDType();
        if(outputDType == U8)
            implicitConcatsU8.push_back(implicitConcat);
        else if(outputDType == FP16)
            implicitConcatsFP16.push_back(implicitConcat);
    }

    addQuantizationLayers(om, implicitConcatsU8, U8);
    addQuantizationLayers(om, implicitConcatsFP16, FP16);
}
