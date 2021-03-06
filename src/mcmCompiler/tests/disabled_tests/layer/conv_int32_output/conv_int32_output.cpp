//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/compiler/compilation_unit.hpp"
#include <iostream>
#include <fstream>

int main()
{

    mv::CompilationUnit unit("ConvLeakyReluModel");
    mv::OpModel& om = unit.model();

    auto input0 = om.input("input#170", {2,1,16,1}, mv::DType("UInt8"), mv::Order::getZMajorID(4));
    input0->set<mv::QuantizationParams>("quantParmas", {{0},{0.011764705882352941},{},{}});

    std::vector<int64_t> weightsData0 = mv::utils::generateSequence<int64_t> (8, 255, 0);
    std::vector<int64_t> weightsData1 = mv::utils::generateSequence<int64_t> (8, 255, 0);
    weightsData0.insert(weightsData0.end(), weightsData1.begin(), weightsData1.end());

    auto weights0 = om.constantInt("", weightsData0, {1,1,16,1}, mv::DType("UInt8"), mv::Order::getZMajorID(4));
    weights0->set<mv::QuantizationParams>("quantParmas", {{0},{0.00392156862745098},{},{}});
    auto conv0 = om.conv("conv", input0, weights0, {1, 1}, {0, 0, 0, 0}, 1, 1);
    conv0->setQuantParams({{0},{1},{},{}});
    om.output("output", conv0, mv::DType("Int32"), true);

    std::string compDescPath = mv::utils::projectRootPath() + "/config/compilation/release_kmb.json";
    unit.loadCompilationDescriptor(compDescPath);
    mv::CompilationDescriptor &compDesc = unit.compilationDescriptor();

    unit.loadTargetDescriptor(mv::Target::ma2490);
    unit.initialize();
    unit.run();

}
