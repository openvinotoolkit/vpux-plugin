#include "include/mcm/compiler/compilation_unit.hpp"
#include "include/mcm/utils/data_generator.hpp"
#include "include/mcm/utils/serializer/Fp16Convert.h"
#include "meta/include/mcm/op_model.hpp"
#include "include/mcm/utils/hardware_tests.hpp"

#include <iostream>
#include <fstream>

int main()
{
    mv::CompilationUnit unit("testModel");
    mv::OpModel& test_cm = unit.model();
    mv::ControlModel cm(test_cm);

    auto input1 = test_cm.input({16, 16, 16, 1}, mv::DType("Float16"), mv::Order("NCHW"));
    auto input1dmaIN = test_cm.dMATask(input1, mv::DmaDirectionEnum::DDR2CMX);
    test_cm.deallocate(input1dmaIN);
    std::vector<double> weights1Data = mv::utils::generateSequence<double>(1*1*16*16);
    auto weights1 = test_cm.constant(weights1Data, {1, 1, 16, 16}, mv::DType("Float16"), mv::Order("NCWH"));
    auto dmaINweights1 = test_cm.dMATask(weights1, mv::DmaDirectionEnum::DDR2CMX);
    test_cm.deallocate(dmaINweights1);
    auto dpuconv1 = test_cm.dPUTaskConv({input1dmaIN, dmaINweights1}, {1,1}, {0,0,0,0});
    auto dmaOutput = test_cm.dMATask(dpuconv1, mv::DmaDirectionEnum::CMX2DDR);
    test_cm.output(dmaOutput);

    std::string outputName("dpu_task");

    std::string compDescPath = mv::utils::projectRootPath() + "/config/compilation/debug_ma2490.json";
    unit.loadCompilationDescriptor(compDescPath);
    mv::CompilationDescriptor &compDesc = unit.compilationDescriptor();

    compDesc.setPassArg("GenerateDot", "output", std::string(outputName + ".dot"));
    compDesc.setPassArg("GenerateDot", "scope", std::string("OpControlModel"));
    compDesc.setPassArg("GenerateDot", "content", std::string("full"));
    compDesc.setPassArg("GenerateDot", "html", true);

    compDesc.remove("serialize");

    unit.loadTargetDescriptor(mv::Target::ma2490);
    unit.initialize();
    // unit.passManager().disablePass(mv::PassGenre::Serialization);
    unit.run();


    system("dot dpu_task.dot       -o dpu_task.png       -Tpng");
//  system("dot dpu_task_adapt.dot -o dpu_task_adapt.png -Tpng");
    system("dot dpu_task_final.dot -o dpu_task_final.png -Tpng");
}
