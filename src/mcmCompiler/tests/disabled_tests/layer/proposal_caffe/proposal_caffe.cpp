//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/compiler/compilation_unit.hpp"
#include <iostream>
#include <fstream>
int main()
{
    mv::CompilationUnit unit("ProposalModel");
    mv::OpModel& om = unit.model();

    // Define Params
    int base_size = 16;
    int pre_nms_topn = 6000;
    int post_nms_topn = 300;
    double nms_thresh = 0.7;
    int feat_stride = 16;
    int min_size = 16;
    float pre_nms_thresh = 0.5;
    bool clip_before_nms = true;
    bool clip_after_nms = false;
    bool normalize = false;
    float box_size_scale = 1.0;
    float box_coordinate_scale = 1.0;
    std::string framework = "CAFFE";
    bool for_deformable = false;
    std::vector<float> scale({8.0,16.0,32.0});
    std::vector<float> ratio({0.5,1.0,2.0});
    std::vector<double> weightsData(14*14*36);
    std::vector<double> imInfoData(4);

    // Load weights from file
    std::string Weights_filename(mv::utils::projectRootPath() + "/tests/layer/proposal_caffe/proposal_caffe.in2");
    std::ifstream w_file;
    w_file.open(Weights_filename, std::fstream::in | std::fstream::binary);
    w_file.read((char*)(weightsData.data()), 14*14*36 * sizeof(uint16_t));
    std::vector<int64_t> weightsData_converted(14*14*36);
    for(unsigned i = 0; i < weightsData.size(); ++i)
        weightsData_converted[i] = weightsData[i];

    // Load imInfo from file
    std::string imInfo_filename(mv::utils::projectRootPath() + "/tests/layer/proposal_caffe/proposal_caffe.in3");
    std::ifstream i_file;
    i_file.open(imInfo_filename, std::fstream::in | std::fstream::binary);
    i_file.read((char*)(imInfoData.data()), 4 * sizeof(uint16_t));
    std::vector<int64_t> imInfoData_converted(4);
    for(unsigned i = 0; i < imInfoData.size(); ++i)
        imInfoData_converted[i] = imInfoData[i];

    auto cls_pred = om.input("cls_pred0", {14,14,18,1}, mv::DType("Float16"), mv::Order::getZMajorID(4));
    auto weights = om.constantInt("weights", weightsData_converted, {14, 14, 36, 1}, mv::DType("Float16"), mv::Order::getZMajorID(4));
    auto im_info = om.constantInt("im_info0" ,imInfoData_converted, {1,4,1,1}, mv::DType("Float16"), mv::Order::getZMajorID(4));
    cls_pred->setQuantParams({{0},{1.0},{},{}});
    weights->setQuantParams({{0},{1.0},{},{}});
    im_info->setQuantParams({{0},{1.0},{},{}});
    // Build inputs vector
    std::vector<mv::Data::TensorIterator> inputs;
    inputs.push_back(cls_pred);
    inputs.push_back(weights);
    inputs.push_back(im_info);
    // Build Model
    auto proposal = om.proposal("", inputs, scale, ratio, base_size, pre_nms_topn, post_nms_topn, nms_thresh, feat_stride, min_size, pre_nms_thresh, clip_before_nms, clip_after_nms, normalize, box_size_scale, box_coordinate_scale, framework, for_deformable);
    om.output("", proposal);
    std::string compDescPath = mv::utils::projectRootPath() + "/config/compilation/release_kmb.json";
    unit.loadCompilationDescriptor(compDescPath);
    unit.compilationDescriptor().remove("adapt", "PostTrainingQuantize");
    unit.loadTargetDescriptor(mv::Target::ma2490);
    unit.initialize();
    unit.run();

    return 0;
}
