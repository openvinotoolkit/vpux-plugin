//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/compiler/compilation_unit.hpp"
#include <iostream>
#include <fstream>
#include <include/mcm/op_model.hpp>

#include <limits>
int main()
{
    mv::CompilationUnit unit("ConvDilation");
    mv::OpModel& om = unit.model();

    static const auto inf = std::numeric_limits<double>::infinity();

    auto data_0 = om.input("input", {16,16,1,1}, mv::DType("UInt8"), mv::Order::getZMajorID(4) /*NHWC*/);
    data_0->setQuantParams({{127},{0.007874016},{-1.000000000000000},{1.000000000000000},{0},{1}});

    //Kernel 3x3 - 1 input channel ; 16 output channels
    // 1 1 1
    // 1 1 1
    // 1 1 1
    uint8_t zeroPointWt =8;
    mv::Shape kernel = mv::Shape({3,3,1,16});
    std::vector<int64_t> weightsData0(kernel.totalSize(), zeroPointWt+zeroPointWt);

    auto weights0 = om.constantInt("weights_conv", weightsData0, kernel, mv::DType("UInt8"), mv::Order("NCHW"));
    weights0->setQuantParams({{zeroPointWt},{0.125},{-1.000000000000000},{1.000000000000000}});

    //the 2 is dilation factor
    auto conv0 = om.conv("conv", data_0, weights0, {1, 1}, {0, 0, 0, 0}, 2, 1);
    conv0->setQuantParams({{32},{4},{-inf},{inf},{0},{1}});
    om.output("", conv0);

    std::string compDescPath = mv::utils::projectRootPath() + "/config/compilation/release_kmb.json";
    unit.loadCompilationDescriptor(compDescPath);
    unit.compilationDescriptor().remove("adapt", "PostTrainingQuantize");
    unit.loadTargetDescriptor(mv::Target::ma2490);
    unit.initialize();
    unit.run();

    return 0;
}
