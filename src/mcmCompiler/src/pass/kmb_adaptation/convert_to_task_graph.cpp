//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/pass/pass_registry.hpp"
#include "include/mcm/op_model.hpp"
#include "include/mcm/computation/model/control_model.hpp"
#include "include/mcm/computation/model/data_model.hpp"
#include "include/mcm/target/kmb/ppe_task.hpp"
#include "include/mcm/tensor/quantization_params.hpp"
#include "include/mcm/utils/custom_strings.hpp"
#include "include/mcm/pass/pass_utils.hpp"
#include "include/mcm/base/exception/runtime_error.hpp"
#include "include/mcm/utils/ops_conversion_utils.hpp"

static void convertOpsToTasksFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);
static void setUpPPETasksFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);

// TODO
// Check whether this function will be used in the future
#if defined(DISABLE_UNUSED_FUNCTIONS_REMOVAL)
static void convert_chw_to_index(std::string order, std::vector<unsigned>& permute_order);
static void calculate_xyz_from_permutation(std::vector<unsigned>& permute_order_xyz, std::vector<unsigned>& permute_order);
#endif

//KMB default: HW PRELU MULT is I8, so 7 precision bits are available
static void addPpeTask(mv::Data::OpListIterator &opIt, const std::vector<std::string> &ppeTaskType, double leakyHack = 1.0, unsigned bits = 7);
int32_t computeClampHigh(mv::Data::OpListIterator &opIt, bool flex);
int32_t computeClampLow(mv::Data::OpListIterator &opIt, bool flex);

namespace mv
{
    namespace pass
    {
        MV_REGISTER_PASS(ConvertOpsToTasks)
            .setFunc(convertOpsToTasksFcn)
            .setDescription(
                "Replace all operations with Hw compatible tasks.");

        MV_REGISTER_PASS(SetUpPPETasks)
            .setFunc(setUpPPETasksFcn)
            .setDescription(
                "Set up PPE Tasks for DPU Tasks");
    }
}

void setUpPPETasksFcn(const mv::pass::PassEntry& , mv::ComputationModel& model, mv::TargetDescriptor& td, mv::Element&, mv::Element&)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_PASS)
    mv::OpModel om(model);
    double leakyReluHack = 1.0;

    auto returnedParams = model.getGlobalConfigParams();
    if (returnedParams->hasAttr("LeakyReluHack"))
        leakyReluHack = returnedParams->get<double>("LeakyReluHack");
    unsigned bits = td.generalTargetConfigs().leakyAccuracyBits;
    auto dpuTasks = om.getOps("DPUTask");
    for(auto& dpuTask : dpuTasks)
    {
        std::vector<std::string> postOps;
        if(dpuTask->hasAttr("postOpTypes"))
            postOps = dpuTask->get<std::vector<std::string>>("postOpTypes");

        addPpeTask(dpuTask, postOps, leakyReluHack, bits);
    }
}

mv::Data::TensorIterator convertEltwiseToTask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                const std::map<std::string, mv::Attribute>& attrs, const std::string& name,  bool software,
                                const mv::QuantizationParams& quantParams,
                                const mv::DType& outputTensorType,
                                const mv::Order& outputTensorOrder)
{
    auto eltwiseType = attrs.at("eltwiseType").get<std::string>();
    mv::Data::TensorIterator eltwiseTask;

    if (!software)
    {
        const std::array<unsigned short, 2> FAKE_KERNEL = {1,1};
        const std::array<unsigned short, 2> FAKE_STRIDE = {1,1};

        auto dpuElementWise = om.dPUTaskEltwise(mv::createDPUTaskName(name), inputs, eltwiseType);
        dpuElementWise->setDType(outputTensorType);
        dpuElementWise->setQuantParams(quantParams);
        dpuElementWise->setOrder(outputTensorOrder);
        auto dpuElementWiseOp = om.getSourceOp(dpuElementWise);
        dpuElementWiseOp->set<std::array<unsigned short, 2>>("kSize", FAKE_KERNEL);
        dpuElementWiseOp->set<std::array<unsigned short, 2>>("stride", FAKE_STRIDE);
        dpuElementWiseOp->set<bool>("hasWeights", false);

        std::vector<std::string> postOps;
        if(attrs.find("postOpTypes") != attrs.end())
            postOps = attrs.at("postOpTypes").get<std::vector<std::string>>();
        postOps.push_back(eltwiseType);
        dpuElementWiseOp->set<std::vector<std::string>>("postOpTypes", postOps);
        eltwiseTask = dpuElementWise;
    }
    else
    {
        //Note: Re-write maybe DPU tasks changed them
        eltwiseTask = om.uPATaskEltwise(mv::createDPUTaskName(name), inputs, eltwiseType);
        eltwiseTask->setDType(mv::DType("Float16"));
        eltwiseTask->setQuantParams(quantParams);
        eltwiseTask->setOrder(outputTensorOrder);
    }
    return eltwiseTask;
}

mv::Data::TensorIterator convertHwConvertToDPUTask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                const std::map<std::string, mv::Attribute>& attrs, const std::string& name,
                                bool /*software*/,
                                const mv::QuantizationParams& quantParams,
                                const mv::DType& outputTensorType,
                                const mv::Order& outputTensorOrder)
{
    const std::array<unsigned short, 2> FAKE_KERNEL = {1,1};
    const std::array<unsigned short, 2> FAKE_STRIDE = {1,1};

    auto dpuHwConvert = om.dPUTaskHwConvert(mv::createDPUTaskName(name), inputs, attrs.at("dType").get<mv::DType>());
    dpuHwConvert->setDType(outputTensorType);
    dpuHwConvert->setQuantParams(quantParams);
    dpuHwConvert->setOrder(outputTensorOrder);
    auto dpuHwConvertOp = om.getSourceOp(dpuHwConvert);
    dpuHwConvertOp->set<std::array<unsigned short, 2>>("kSize", FAKE_KERNEL);
    dpuHwConvertOp->set<std::array<unsigned short, 2>>("stride", FAKE_STRIDE);
    dpuHwConvertOp->set<bool>("hasWeights", false);

    std::vector<std::string> postOps;
    if(attrs.find("postOpTypes") != attrs.end())
        postOps = attrs.at("postOpTypes").get<std::vector<std::string>>();
    postOps.push_back("And"); // HwConvert by runtime_model will be converted to Eltwise And operation
    dpuHwConvertOp->set<std::vector<std::string>>("postOpTypes", postOps);

    return dpuHwConvert;
}

mv::Data::TensorIterator convertLReluToUPATask(mv::OpModel& om,
                                               const std::vector<mv::Data::TensorIterator>& inputs,
                                               const std::map<std::string, mv::Attribute>& attrs,
                                               const std::string& name,
                                               bool /*software*/,
                                               const mv::QuantizationParams& quantParams,
                                               const mv::DType& outputTensorType,
                                               const mv::Order& /*outputTensorOrder*/)
{

    auto alpha = attrs.at("alpha").get<double>();
    mv::Data::TensorIterator lreluTask;

    lreluTask = om.uPATaskLeakyRelu(mv::createDPUTaskName(name), inputs, alpha);
    lreluTask->setQuantParams(quantParams);
    lreluTask->setDType(outputTensorType);

    return lreluTask;
}

mv::Data::TensorIterator convertMaxPoolToDPUTask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name,  bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto strides = attrs.at("stride").get<std::array<unsigned short, 2>>();
    auto padding = attrs.at("padding").get<std::array<unsigned short, 4>>();
    auto kernelSize = attrs.at("kSize").get<std::array<unsigned short, 2>>();
    auto exclude_pad = attrs.at("exclude_pad").get<bool>();

    auto dpuPool = om.dPUTaskMaxPool(mv::createDPUTaskName(name), inputs,
                        kernelSize, strides, padding, exclude_pad);
    dpuPool->setDType(outputTensorType);
    dpuPool->setQuantParams(quantParams);
    dpuPool->setOrder(outputTensorOrder);

    om.getSourceOp(dpuPool)->set<bool>("hasWeights", false);
    return dpuPool;
}

mv::Data::TensorIterator convertDepthwiseConvolutionToDPUTask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name,  bool /*software*/,
                    const mv::QuantizationParams& quantParams,
                    const mv::DType& outputTensorType,
                    const mv::Order& outputTensorOrder)
{
    auto strides = attrs.at("stride").get<std::array<unsigned short, 2>>();
    auto padding = attrs.at("padding").get<std::array<unsigned short, 4>>();
    auto dilationFactor = attrs.at("dilationFactor").get<unsigned>();

    auto dpuConv = om.dPUTaskDepthwiseConv(mv::createDPUTaskName(name), inputs, strides, padding, dilationFactor);
    dpuConv->setDType(outputTensorType);
    dpuConv->setQuantParams(quantParams);
    dpuConv->setOrder(outputTensorOrder);

    if(attrs.find("asymmetricKernel") != attrs.end())
        dpuConv->set<unsigned>("asymmetricKernel", attrs.at("asymmetricKernel").get<unsigned>());
    auto dpuConvOp = om.getSourceOp(dpuConv);
    dpuConvOp->set<bool>("hasWeights", true);

    return dpuConv;
}

mv::Data::TensorIterator convertConvolutionToDPUTask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name, bool /*software*/,
                    const mv::QuantizationParams& quantParams,
                    const mv::DType& outputTensorType,
                    const mv::Order& outputTensorOrder)
{
    auto strides = attrs.at("stride").get<std::array<unsigned short, 2>>();
    auto padding = attrs.at("padding").get<std::array<unsigned short, 4>>();
    auto dilationFactor = attrs.at("dilationFactor").get<unsigned>();
    auto globalParams = om.getGlobalConfigParams();
    bool enableChannelMajor = globalParams->get<bool>("enable_channel_major_conv");
    unsigned group = attrs.at("group").get<unsigned>();

    auto dpuConv = om.dPUTaskConv(mv::createDPUTaskName(name), inputs, strides, padding, dilationFactor, group);
    dpuConv->setDType(outputTensorType);
    dpuConv->setQuantParams(quantParams);
    dpuConv->setOrder(outputTensorOrder);

    auto dpuConvOp = om.getSourceOp(dpuConv);
    dpuConvOp->set<bool>("hasWeights", true);

    //    NOTE: Thanks to proper padding handling we don't need this anymore
    //    Leaving it here as an historical note... and now it's back as an option
    //    Check if the dpuConvop is DW, if DW, it can't be channel major conv
    bool notDW = true;
    if(dpuConvOp->hasAttr("taskOp"))
    {
        if (dpuConvOp->get<std::string>("taskOp") == "DepthwiseConv")
            notDW = false;
    }

    if(attrs.find("supportsCM") != attrs.end())
        dpuConvOp->set<bool>("supportsCM", attrs.at("supportsCM").get<bool>());

    if (enableChannelMajor && dpuConvOp->supportsCMConv() && notDW)
    {
        dpuConvOp->erase("taskOp");
        dpuConvOp->set<std::string>("taskOp", "ChannelMajorConvolution");
    }

    if(attrs.find("asymmetricKernel") != attrs.end())
        dpuConv->set<unsigned>("asymmetricKernel", attrs.at("asymmetricKernel").get<unsigned>());

    if (attrs.find("verticalFusionOutputOverlap") != attrs.end())
        dpuConv->set<std::size_t>("verticalFusionOutputOverlap", attrs.at("verticalFusionOutputOverlap").get<std::size_t>());

    return dpuConv;
}

mv::Data::TensorIterator convertIdentityToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                const std::map<std::string, mv::Attribute>& attrs, const std::string& name,  bool /*software*/,
                                const mv::QuantizationParams& quantParams,
                                const mv::DType& outputTensorType,
                                const mv::Order& outputTensorOrder)
{
    auto identity = om.uPATaskIdentity(name, inputs);
    identity->setDType(outputTensorType);
    identity->setQuantParams(quantParams);
    identity->setOrder(outputTensorOrder);
    if (attrs.find("forceShape") != attrs.end())
        identity->setShape(attrs.at("forceShape").get<mv::Shape>());

    return identity;
}

mv::Data::TensorIterator convertSoftmaxToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                const std::map<std::string, mv::Attribute>& attrs, const std::string& name,  bool /*software*/,
                                const mv::QuantizationParams& quantParams,
                                const mv::DType& outputTensorType,
                                const mv::Order& outputTensorOrder)
{
    auto axis = attrs.at("axis").get<std::string>();

    auto softmax = om.uPATaskSoftmax(name, inputs, axis);
    softmax->setDType(outputTensorType);
    softmax->setQuantParams(quantParams);
    softmax->setOrder(outputTensorOrder);
    return softmax;
}

mv::Data::TensorIterator convertSigmoidToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                const std::map<std::string, mv::Attribute>& /*attrs*/, const std::string& name,  bool /*software*/,
                                const mv::QuantizationParams& quantParams,
                                const mv::DType& /*outputTensorType*/,
                                const mv::Order& outputTensorOrder)
{
    auto sigmoid = om.uPATaskSigmoid(name, inputs);
    sigmoid->setDType(mv::DType("Float16"));
    sigmoid->setQuantParams(quantParams);
    sigmoid->setOrder(outputTensorOrder);
    return sigmoid;
}

mv::Data::TensorIterator convertProposalToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                const std::map<std::string, mv::Attribute>& attrs, const std::string& name,  bool /*software*/,
                                const mv::QuantizationParams& quantParams,
                                const mv::DType& outputTensorType,
                                const mv::Order& outputTensorOrder)
{
    // Required params
    auto scale = attrs.at("scale").get<std::vector<float>>();
    auto ratio = attrs.at("ratio").get<std::vector<float>>();
    auto base_size = attrs.at("base_size").get<unsigned>();
    auto pre_nms_topn = attrs.at("pre_nms_topn").get<unsigned>();
    auto post_nms_topn = attrs.at("post_nms_topn").get<unsigned>();
    auto nms_thresh = attrs.at("nms_thresh").get<double>();
    auto feat_stride = attrs.at("feat_stride").get<unsigned>();
    auto min_size = attrs.at("min_size").get<unsigned>();

    // Optional params
    auto pre_nms_thresh = attrs.at("pre_nms_thresh").get<double>();
    auto clip_before_nms = attrs.at("clip_before_nms").get<bool>();
    auto clip_after_nms = attrs.at("clip_after_nms").get<bool>();
    auto normalize = attrs.at("normalize").get<bool>();
    auto box_size_scale = attrs.at("box_size_scale").get<double>();
    auto box_coordinate_scale = attrs.at("box_coordinate_scale").get<double>();
    auto framework = attrs.at("framework").get<std::string>();
    auto for_deformable = attrs.at("for_deformable").get<bool>();

    auto proposal = om.uPATaskProposal(name, inputs, scale, ratio, base_size, pre_nms_topn, post_nms_topn, nms_thresh, feat_stride, min_size,
                                       pre_nms_thresh, clip_before_nms, clip_after_nms, normalize, box_size_scale, box_coordinate_scale, framework, for_deformable);
    proposal->setDType(outputTensorType);
    proposal->setQuantParams(quantParams);
    proposal->setOrder(outputTensorOrder);
    return proposal;
}

mv::Data::TensorIterator convertROIPoolingToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name,  bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto pooled_w = attrs.at("pooled_w").get<unsigned>();
    auto pooled_h = attrs.at("pooled_h").get<unsigned>();
    auto spatial_scale = attrs.at("spatial_scale").get<double>();
    auto roi_pooling_method = attrs.at("roi_pooling_method").get<unsigned>();
    auto num_rois = attrs.at("num_rois").get<unsigned>();

    auto pooling = om.uPATaskROIPooling(name, inputs, pooled_w, pooled_h, spatial_scale, roi_pooling_method, num_rois);
    pooling->setDType(outputTensorType);
    pooling->setQuantParams(quantParams);
    pooling->setOrder(outputTensorOrder);
    return pooling;
}

mv::Data::TensorIterator convertPSROIPoolingToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name, bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto output_dim    = attrs.at("output_dim").get<std::size_t>();
    auto group_size    = attrs.at("group_size").get<std::size_t>();
    auto spatial_scale = attrs.at("spatial_scale").get<double>();
    auto pooled_w      = attrs.at("pooled_w").get<std::size_t>();
    auto pooled_h      = attrs.at("pooled_h").get<std::size_t>();
    auto spatial_bin_x = attrs.at("spatial_bin_x").get<std::size_t>();
    auto spatial_bin_y = attrs.at("spatial_bin_y").get<std::size_t>();
    auto mode          = attrs.at("mode").get<std::string>();

    auto psroiPooling = om.uPATaskPSROIPooling(name, inputs, output_dim, group_size, spatial_scale, pooled_w, pooled_h,
                                  spatial_bin_x, spatial_bin_y, mode);
    psroiPooling->setDType(outputTensorType);
    psroiPooling->setQuantParams(quantParams);
    psroiPooling->setOrder(outputTensorOrder);
    return psroiPooling;
}

mv::Data::TensorIterator convertQuantizeToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& /*attrs*/, const std::string& name,  bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto quantize = om.uPATaskQuantize(name, inputs);
    quantize->setDType(outputTensorType);
    quantize->setQuantParams(quantParams);
    quantize->setOrder(outputTensorOrder);
    return quantize;
}

mv::Data::TensorIterator convertResampleToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name,  bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto interpolation = attrs.at("interpolation").get<std::string>();
    auto antialias = attrs.at("antialias").get<bool>();
    auto output_shape = attrs.at("output_shape").get<mv::Shape>();

    auto resample = om.uPATaskResample(name, inputs, interpolation, antialias, output_shape);
    resample->setDType(outputTensorType);
    resample->setQuantParams(quantParams);
    resample->setOrder(outputTensorOrder);
    return resample;
}

mv::Data::TensorIterator convertInterpolateToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name,  bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto interpolation = attrs.at("mode").get<std::string>();
    auto coord_transform_mode = attrs.at("coordinate_transformation_mode").get<std::string>();
    auto nearest_mode = attrs.at("nearest_mode").get<std::string>();
    auto antialias = attrs.at("antialias").get<bool>();
    auto align_corners = attrs.at("coordinate_transformation_mode").get<std::string>() == "align_corners";
    auto output_shape = attrs.at("output_shape").get<mv::Shape>();

    auto interpolate = om.uPATaskInterpolate(name, inputs, output_shape, interpolation, nearest_mode, coord_transform_mode, align_corners, antialias);

    interpolate->setDType(outputTensorType);
    interpolate->setQuantParams(quantParams);
    interpolate->setOrder(outputTensorOrder);

    return interpolate;
}

mv::Data::TensorIterator convertReverseSequenceToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name,  bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto seqAxis = attrs.at("seq_axis").get<int64_t>();
    auto batchAxis = attrs.at("batch_axis").get<int64_t>();

    auto reverse = om.uPATaskReverseSequence(name, inputs, seqAxis, batchAxis);

    reverse->setDType(outputTensorType);
    reverse->setQuantParams(quantParams);
    reverse->setOrder(outputTensorOrder);

    return reverse;
}

mv::Data::TensorIterator convertReshapeToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name,  bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto shape = attrs.at("shape").get<mv::Shape>();

    auto reshape = om.uPATaskReshape(name, inputs, shape);
    reshape->setDType(outputTensorType);
    reshape->setQuantParams(quantParams);
    reshape->setOrder(outputTensorOrder);
    return reshape;
}

mv::Data::TensorIterator convertRegionYoloToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name,  bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto coords = attrs.at("coords").get<unsigned>();
    auto classes = attrs.at("classes").get<unsigned>();
    auto do_softmax = attrs.at("do_softmax").get<bool>();
    auto num = attrs.at("num").get<unsigned>();
    auto mask = attrs.at("mask").get<std::vector<unsigned>>();

    auto regionYolo = om.uPATaskRegionYolo(name, inputs, coords, classes, do_softmax, num, mask);
    regionYolo->setDType(outputTensorType);
    regionYolo->setQuantParams(quantParams);
    regionYolo->setOrder(outputTensorOrder);
    return regionYolo;
}

mv::Data::TensorIterator convertReorgYoloToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name,  bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto stride = attrs.at("stride").get<unsigned>();

    auto reorgYolo = om.uPATaskReorgYolo(name, inputs, stride);
    reorgYolo->setDType(outputTensorType);
    reorgYolo->setQuantParams(quantParams);
    reorgYolo->setOrder(outputTensorOrder);
    return reorgYolo;
}

mv::Data::TensorIterator convertNormalizeToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name,  bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto eps = attrs.at("eps").get<double>();
    auto across_spatial = attrs.at("across_spatial").get<unsigned>();
    auto channel_shared = attrs.at("channel_shared").get<unsigned>();

    auto normalize = om.uPATaskNormalize(name, inputs, eps, across_spatial, channel_shared);
    normalize->setDType(outputTensorType);
    normalize->setQuantParams(quantParams);
    normalize->setOrder(outputTensorOrder);
    return normalize;
}

mv::Data::TensorIterator convertDetectionOutputToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& /*name*/,  bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto num_classes = attrs.at("num_classes").get<int64_t>();
    auto keep_top_k = attrs.at("keep_top_k").get<int64_t>();
    auto nms_threshold = attrs.at("nms_threshold").get<double>();
    auto background_label_id = attrs.at("background_label_id").get<int64_t>();
    auto top_k = attrs.at("top_k").get<int64_t>();
    auto variance_encoded_in_target = attrs.at("variance_encoded_in_target").get<bool>();
    auto code_type = attrs.at("code_type").get<std::string>();
    auto share_location = attrs.at("share_location").get<bool>();
    auto confidence_threshold = attrs.at("confidence_threshold").get<double>();
    auto clip_before_nms = attrs.at("clip_before_nms").get<bool>();
    auto clip_after_nms = attrs.at("clip_after_nms").get<bool>();
    auto decrease_label_id = attrs.at("decrease_label_id").get<int64_t>();
    auto normalized = attrs.at("normalized").get<bool>();
    auto input_height = attrs.at("input_height").get<int64_t>();
    auto input_width = attrs.at("input_width").get<int64_t>();
    auto objectness_score = attrs.at("objectness_score").get<double>();

    auto detectionOutput =  om.uPATaskDetectionOutput("", inputs, num_classes, keep_top_k, nms_threshold, background_label_id, top_k, variance_encoded_in_target,
                                     code_type, share_location, confidence_threshold, clip_before_nms, clip_after_nms,
                                     decrease_label_id, normalized, input_height, input_width, objectness_score);
    detectionOutput->setDType(outputTensorType);
    detectionOutput->setQuantParams(quantParams);
    detectionOutput->setOrder(outputTensorOrder);
    return detectionOutput;
}

mv::Data::TensorIterator convertPriorboxToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& /*name*/,  bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto flip = attrs.at("flip").get<unsigned>();
    auto clip = attrs.at("clip").get<unsigned>();
    auto step_w = attrs.at("step_w").get<double>();
    auto step_h = attrs.at("step_h").get<double>();
    auto offset = attrs.at("offset").get<double>();

    auto priorbox = om.uPATaskPriorbox("", inputs, flip, clip, step_w, step_h, offset);
    priorbox->setDType(outputTensorType);
    priorbox->setQuantParams(quantParams);
    priorbox->setOrder(outputTensorOrder);
    return priorbox;
}

mv::Data::TensorIterator convertArgmaxToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& /*name*/,  bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto out_max_val = attrs.at("out_max_val").get<int64_t>();
    auto top_k = attrs.at("top_k").get<int64_t>();
    auto axis = attrs.at("axis").get<int64_t>();

    auto argmax = om.uPATaskArgmax("", inputs, out_max_val, top_k, axis);
    argmax->setDType(outputTensorType);
    argmax->setQuantParams(quantParams);
    argmax->setOrder(outputTensorOrder);
    return argmax;
}

mv::Data::TensorIterator convertPermuteNDToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name,  bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto order = attrs.at("perm_order").get<std::vector<int64_t>>();

    auto upaPermute = om.uPATaskPermuteND(name, inputs, order);
    upaPermute->setDType(outputTensorType);
    upaPermute->setQuantParams(quantParams);
    upaPermute->setOrder(outputTensorOrder);
    auto upaPermuteOp = om.getSourceOp(upaPermute);

    upaPermuteOp->set<std::vector<int64_t>>("perm_order", order);

    return upaPermute;
}

mv::Data::TensorIterator convertInterpToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name,  bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    // Required params
    auto factor = attrs.at("factor").get<double>();
    auto pad_beg = attrs.at("pad_beg").get<unsigned>();
    auto pad_end = attrs.at("pad_end").get<unsigned>();

    // Optional params
    auto height = attrs.at("height").get<unsigned>();
    auto width = attrs.at("width").get<unsigned>();
    auto align_corners = attrs.at("align_corners").get<bool>();

    auto interp = om.uPATaskInterp(name, inputs, factor, pad_beg, pad_end, height, width, align_corners);
    interp->setDType(outputTensorType);
    interp->setQuantParams(quantParams);
    interp->setOrder(outputTensorOrder);
    return interp;
}

mv::Data::TensorIterator convertNormToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name,  bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    // Required params
    auto alpha = attrs.at("alpha").get<double>();
    auto beta = attrs.at("beta").get<double>();
    auto region = attrs.at("region").get<std::string>();
    auto local_size = attrs.at("local_size").get<unsigned>();

    auto norm = om.uPATaskNorm(name, inputs, alpha, beta, region, local_size);
    norm->setDType(outputTensorType);
    norm->setQuantParams(quantParams);
    norm->setOrder(outputTensorOrder);
    return norm;
}

mv::Data::TensorIterator convertPadToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                             const std::map<std::string, mv::Attribute>& attrs, const std::string& name, bool /*software*/,
                                             const mv::QuantizationParams& quantParams,
                                             const mv::DType& outputTensorType,
                                             const mv::Order& outputTensorOrder)
{
    auto pads_begin = attrs.at("pads_begin").get<std::array<unsigned short, 4>>();
    auto pads_end = attrs.at("pads_end").get<std::array<unsigned short, 4>>();
    auto pad_mode = attrs.at("pad_mode").get<std::string>();
    auto pad_value = attrs.at("pad_value").get<double>();

    auto pad = om.uPATaskPad(name, inputs, pads_begin, pads_end, pad_mode, pad_value);
    pad->setDType(outputTensorType);
    pad->setQuantParams(quantParams);
    pad->setOrder(outputTensorOrder);
    return pad;
}

mv::Data::TensorIterator convertCustomOclToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name, bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder) {
    auto custom = om.uPATaskCustomOcl(name, inputs,
                                      attrs.at("kernelData").get<std::vector<uint8_t>>(),
                                      attrs.at("paramData").get<std::vector<uint8_t>>(),
                                      attrs.at("outputsInfo").get<std::vector<mv::TensorInfo>>());
    custom->setDType(outputTensorType);
    custom->setQuantParams(quantParams);
    custom->setOrder(outputTensorOrder);
    return custom;
}

mv::Data::TensorIterator convertCustomCppToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                                   const std::map<std::string, mv::Attribute>& attrs, const std::string& name, bool /*software*/,
                                                   const mv::QuantizationParams& quantParams,
                                                   const mv::DType& outputTensorType,
                                                   const mv::Order& outputTensorOrder)
{
    auto custom = om.uPATaskCustomCpp(name, inputs,
                                      attrs.at("kernelData").get<std::vector<uint8_t>>(),
                                      attrs.at("paramData").get<std::vector<uint8_t>>(),
                                      attrs.at("outputsInfo").get<std::vector<mv::TensorInfo>>());
    custom->setDType(outputTensorType);
    custom->setQuantParams(quantParams);
    custom->setOrder(outputTensorOrder);
    return custom;
}

mv::Data::TensorIterator convertDeconvToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name, bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto strides = attrs.at("stride").get<std::array<unsigned short, 2>>();
    auto padding = attrs.at("padding").get<std::array<unsigned short, 4>>();
    auto dilationFactor = attrs.at("dilationFactor").get<unsigned>();
    auto group = attrs.at("group").get<unsigned>();
    auto is_depthwise = attrs.at("is_depthwise").get<bool>();

    auto globalParams = om.getGlobalConfigParams();

    auto upaDeconv = om.uPATaskDeconv(name, inputs, strides, padding, dilationFactor, group, is_depthwise);
    upaDeconv->setDType(outputTensorType);
    upaDeconv->setQuantParams(quantParams);
    upaDeconv->setOrder(outputTensorOrder);

    auto upaDeconvOp = om.getSourceOp(upaDeconv);
    upaDeconvOp->set<bool>("hasWeights", true);

    return upaDeconv;
}

mv::Data::TensorIterator convertTileToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name, bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto axis = attrs.at("axis").get<unsigned>();
    auto tiles = attrs.at("tiles").get<unsigned>();

    auto tile = om.uPATaskTile(name, inputs, axis, tiles);
    tile->setDType(outputTensorType);
    tile->setQuantParams(quantParams);
    tile->setOrder(outputTensorOrder);
    return tile;
}

mv::Data::TensorIterator convertCTCDecoderToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs, const std::string& name, bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    auto merge_repeated = attrs.at("ctc_merge_repeated").get<bool>();

    auto ctcDecoder = om.uPATaskCTCDecoder(name, inputs, merge_repeated);
    ctcDecoder->setDType(outputTensorType);
    ctcDecoder->setQuantParams(quantParams);
    ctcDecoder->setOrder(outputTensorOrder);
    return ctcDecoder;
}

mv::Data::TensorIterator convertRefConvToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                    const std::map<std::string, mv::Attribute>& attrs,
                                    const std::string& name, bool /*software*/,
                                    const mv::QuantizationParams& quantParams,
                                    const mv::DType& outputTensorType,
                                    const mv::Order& outputTensorOrder)
{
    const auto strides = attrs.at("stride").get<std::array<unsigned short, 2>>();
    const auto padding = attrs.at("padding").get<std::array<unsigned short, 4>>();
    const auto dilationFactor = attrs.at("dilationFactor").get<unsigned>();
    const auto group = attrs.at("group").get<unsigned>();

    auto refConv = om.uPATaskRefConv(name, inputs, strides, padding, dilationFactor, group);
    refConv->setDType(outputTensorType);
    refConv->setQuantParams(quantParams);
    refConv->setOrder(outputTensorOrder);
    return refConv;
}

mv::Data::TensorIterator convertGatherToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                                const std::map<std::string, mv::Attribute>& attrs,
                                                const std::string& name, bool /*software*/,
                                                const mv::QuantizationParams& quantParams,
                                                const mv::DType& outputTensorType,
                                                const mv::Order& outputTensorOrder)
{
    auto axis = attrs.at("axis").get<unsigned>();

    auto gather = om.uPATaskGather(name, inputs, axis);
    gather->setDType(outputTensorType);
    gather->setQuantParams(quantParams);
    gather->setOrder(outputTensorOrder);
    return gather;
}

mv::Data::TensorIterator convertFakeQuantizeToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                                const std::map<std::string, mv::Attribute>& attrs,
                                                const std::string& name, bool /*software*/,
                                                const mv::QuantizationParams& quantParams,
                                                const mv::DType& /*outputTensorType*/,
                                                const mv::Order& outputTensorOrder)
{
    const auto levels = attrs.at("levels").get<unsigned>();

    auto fakeQuantize = om.uPATaskFakeQuantize(name, inputs, levels);
    fakeQuantize->setQuantParams(quantParams);
    fakeQuantize->setOrder(outputTensorOrder);
    return fakeQuantize;
}

mv::Data::TensorIterator convertHSwishToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                                const std::map<std::string, mv::Attribute>& /*attrs*/,
                                                const std::string& name, bool /*software*/,
                                                const mv::QuantizationParams& quantParams,
                                                const mv::DType& outputTensorType,
                                                const mv::Order& outputTensorOrder)
{
    auto hswish = om.uPATaskHSwish(name, inputs);
    hswish->setDType(outputTensorType);
    hswish->setQuantParams(quantParams);
    hswish->setOrder(outputTensorOrder);
    return hswish;
}

mv::Data::TensorIterator convertSwishToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                               const std::map<std::string, mv::Attribute>& attrs,
                                               const std::string& name, bool /*software*/,
                                               const mv::QuantizationParams& quantParams,
                                               const mv::DType& outputTensorType,
                                               const mv::Order& outputTensorOrder)
{
    const auto beta = attrs.at("beta").get<double>();
    auto swish = om.uPATaskSwish(name, inputs, beta);
    swish->setDType(outputTensorType);
    swish->setOrder(outputTensorOrder);
    swish->setQuantParams(quantParams);
    return swish;
}

mv::Data::TensorIterator convertTanhToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                                const std::map<std::string, mv::Attribute>& /*attrs*/,
                                                const std::string& name, bool /*software*/,
                                                const mv::QuantizationParams& quantParams,
                                                const mv::DType& outputTensorType,
                                                const mv::Order& outputTensorOrder)
{
    auto tanh = om.uPATaskTanh(name, inputs);
    tanh->setDType(outputTensorType);
    tanh->setQuantParams(quantParams);
    tanh->setOrder(outputTensorOrder);
    return tanh;
}

mv::Data::TensorIterator convertEluToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                              const std::map<std::string, mv::Attribute>& attrs,
                                              const std::string& name, bool /*software*/,
                                              const mv::QuantizationParams& quantParams,
                                              const mv::DType& outputTensorType,
                                              const mv::Order& outputTensorOrder)
{
    const auto alpha = attrs.at("alpha").get<double>();

    auto elu = om.uPATaskElu(name, inputs, alpha);
    elu->setDType(outputTensorType);
    elu->setQuantParams(quantParams);
    elu->setOrder(outputTensorOrder);
    return elu;
}

mv::Data::TensorIterator convertMishToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                              const std::map<std::string, mv::Attribute>& /*attrs*/,
                                              const std::string& name, bool /*software*/,
                                              const mv::QuantizationParams& quantParams,
                                              const mv::DType& outputTensorType,
                                              const mv::Order& outputTensorOrder)
{
    auto mish = om.uPATaskMish(name, inputs);
    mish->setDType(outputTensorType);
    mish->setQuantParams(quantParams);
    mish->setOrder(outputTensorOrder);
    return mish;
}

mv::Data::TensorIterator convertFloorToUPATask(mv::OpModel& om,
                                               const std::vector<mv::Data::TensorIterator>& inputs,
                                               const std::map<std::string, mv::Attribute>& /*attrs*/,
                                               const std::string& name,
                                               bool /*software*/,
                                               const mv::QuantizationParams& quantParams,
                                               const mv::DType& outputTensorType,
                                               const mv::Order& outputTensorOrder)
{
    auto op = om.uPATaskFloor(name, inputs);
    op->setDType(outputTensorType);
    op->setQuantParams(quantParams);
    op->setOrder(outputTensorOrder);
    return op;
}

mv::Data::TensorIterator convertRoundToUPATask(mv::OpModel& om,
                                               const std::vector<mv::Data::TensorIterator>& inputs,
                                               const std::map<std::string, mv::Attribute>& attrs,
                                               const std::string& name,
                                               bool /*software*/,
                                               const mv::QuantizationParams& quantParams,
                                               const mv::DType& outputTensorType,
                                               const mv::Order& outputTensorOrder)
{
    const auto mode = attrs.at("mode").get<std::string>();
    auto op = om.uPATaskRound(name, inputs, mode);
    op->setDType(outputTensorType);
    op->setQuantParams(quantParams);
    op->setOrder(outputTensorOrder);
    return op;
}

mv::Data::TensorIterator convertCeilingToUPATask(mv::OpModel& om,
                                                 const std::vector<mv::Data::TensorIterator>& inputs,
                                                 const std::map<std::string, mv::Attribute>& /*attrs*/,
                                                 const std::string& name,
                                                 bool /*software*/,
                                                 const mv::QuantizationParams& quantParams,
                                                 const mv::DType& outputTensorType,
                                                 const mv::Order& outputTensorOrder)
{
    auto op = om.uPATaskCeiling(name, inputs);
    op->setDType(outputTensorType);
    op->setQuantParams(quantParams);
    op->setOrder(outputTensorOrder);
    return op;
}

mv::Data::TensorIterator convertErfToUPATask(mv::OpModel& om,
                                             const std::vector<mv::Data::TensorIterator>& inputs,
                                             const std::map<std::string, mv::Attribute>& /*attrs*/,
                                             const std::string& name,
                                             bool /*software*/,
                                             const mv::QuantizationParams& quantParams,
                                             const mv::DType& outputTensorType,
                                             const mv::Order& outputTensorOrder)
{
    auto op = om.uPATaskErf(name, inputs);
    op->setDType(outputTensorType);
    op->setQuantParams(quantParams);
    op->setOrder(outputTensorOrder);
    return op;
}

mv::Data::TensorIterator convertGeluToUPATask(mv::OpModel& om,
                                             const std::vector<mv::Data::TensorIterator>& inputs,
                                             const std::map<std::string, mv::Attribute>& /*attrs*/,
                                             const std::string& name,
                                             bool /*software*/,
                                             const mv::QuantizationParams& quantParams,
                                             const mv::DType& outputTensorType,
                                             const mv::Order& outputTensorOrder)
{
    auto op = om.uPATaskGelu(name, inputs);
    op->setDType(outputTensorType);
    op->setQuantParams(quantParams);
    op->setOrder(outputTensorOrder);
    return op;
}

mv::Data::TensorIterator convertExpToUPATask(mv::OpModel& om,
                                             const std::vector<mv::Data::TensorIterator>& inputs,
                                             const std::map<std::string, mv::Attribute>& /*attrs*/,
                                             const std::string& name,
                                             bool /*software*/,
                                             const mv::QuantizationParams& quantParams,
                                             const mv::DType& outputTensorType,
                                             const mv::Order& outputTensorOrder)
{
    auto op = om.uPATaskExp(name, inputs);
    op->setDType(outputTensorType);
    op->setQuantParams(quantParams);
    op->setOrder(outputTensorOrder);
    return op;
}

mv::Data::TensorIterator convertLogToUPATask(mv::OpModel& om,
                                             const std::vector<mv::Data::TensorIterator>& inputs,
                                             const std::map<std::string, mv::Attribute>& /*attrs*/,
                                             const std::string& name,
                                             bool /*software*/,
                                             const mv::QuantizationParams& quantParams,
                                             const mv::DType& outputTensorType,
                                             const mv::Order& outputTensorOrder)
{
    auto op = om.uPATaskLog(name, inputs);
    op->setDType(outputTensorType);
    op->setQuantParams(quantParams);
    op->setOrder(outputTensorOrder);
    return op;
}

mv::Data::TensorIterator convertConversionToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                                const std::map<std::string, mv::Attribute>& attrs,
                                                const std::string& name, bool /*software*/,
                                                const mv::QuantizationParams& quantParams,
                                                const mv::DType& outputTensorType,
                                                const mv::Order& outputTensorOrder)
{
    const auto dType = attrs.at("dType").get<mv::DType>();
    if (dType != outputTensorType)
    {
        throw mv::RuntimeError("Inconsistent DType between Conversion layer attribute (", dType.toString() +
            ") and Conversion layer output (" + outputTensorType.toString() + ") for " + name);
    }
    auto convert = om.uPATaskConversion(name, inputs, dType);
    convert->setDType(outputTensorType);
    convert->setQuantParams(quantParams);
    convert->setOrder(outputTensorOrder);
    return convert;
}

mv::Data::TensorIterator convertReluToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                                const std::map<std::string, mv::Attribute>& /*attrs*/,
                                                const std::string& name, bool /*software*/,
                                                const mv::QuantizationParams& quantParams,
                                                const mv::DType& outputTensorType,
                                                const mv::Order& outputTensorOrder)
{
    auto relu =  om.uPATaskRelu(name, inputs);
    relu->setDType(outputTensorType);
    relu->setQuantParams(quantParams);
    relu->setOrder(outputTensorOrder);

    return relu;
}

mv::Data::TensorIterator convertSoftPlusToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                                const std::map<std::string, mv::Attribute>& /*attrs*/,
                                                const std::string& name, bool /*software*/,
                                                const mv::QuantizationParams& quantParams,
                                                const mv::DType& outputTensorType,
                                                const mv::Order& outputTensorOrder)
{
    auto softplus = om.uPATaskSoftPlus(name, inputs);
    softplus->setDType(outputTensorType);
    softplus->setQuantParams(quantParams);
    softplus->setOrder(outputTensorOrder);
    return softplus;
}

mv::Data::TensorIterator convertMVNToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                                const std::map<std::string, mv::Attribute>& attrs,
                                                const std::string& name,  bool /*software*/,
                                                const mv::QuantizationParams& quantParams,
                                                const mv::DType& outputTensorType,
                                                const mv::Order& outputTensorOrder)
{
    auto across_channels = attrs.at("across_channels").get<bool>();
    auto normalize_variance = attrs.at("normalize_variance").get<bool>();
    auto eps = attrs.at("eps").get<double>();

    auto mvn = om.uPATaskMVN(name, inputs, across_channels, normalize_variance, eps);
    mvn->setDType(outputTensorType);
    mvn->setQuantParams(quantParams);
    mvn->setOrder(outputTensorOrder);
    return mvn;
}

mv::Data::TensorIterator convertSpaceToDepthToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                                        const std::map<std::string, mv::Attribute>& attrs,
                                                        const std::string& name,  bool /*software*/,
                                                        const mv::QuantizationParams& quantParams,
                                                        const mv::DType& outputTensorType,
                                                        const mv::Order& outputTensorOrder)
{
    auto mode = attrs.at("mode").get<std::string>();
    auto block_size = attrs.at("block_size").get<uint32_t>();

    auto spaceToDepth = om.uPATaskSpaceToDepth(name, inputs, block_size, mode);
    spaceToDepth->setDType(outputTensorType);
    spaceToDepth->setQuantParams(quantParams);
    spaceToDepth->setOrder(outputTensorOrder);
    return spaceToDepth;
}

mv::Data::TensorIterator convertCTCGreedyDecoderSeqLenToUPATask(
        mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
        const std::map<std::string, mv::Attribute>& attrs,
        const std::string& name, bool /*software*/,
        const mv::QuantizationParams& quantParams,
        const mv::DType& outputTensorType,
        const mv::Order& outputTensorOrder)
{
    auto merge_repeated = attrs.at("merge_repeated").get<bool>();

    auto ctc = om.uPATaskCTCGreedyDecoderSeqLen(name, inputs, merge_repeated);
    ctc->setDType(outputTensorType);
    ctc->setQuantParams(quantParams);
    ctc->setOrder(outputTensorOrder);
    return ctc;
}

mv::Data::TensorIterator convertPreluToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                                const std::map<std::string, mv::Attribute>& /*attrs*/,
                                                const std::string& name, bool /*software*/,
                                                const mv::QuantizationParams& quantParams,
                                                const mv::DType& outputTensorType,
                                                const mv::Order& outputTensorOrder)
{
    auto prelu =  om.uPATaskPrelu(name, inputs);
    prelu->setDType(outputTensorType);
    prelu->setQuantParams(quantParams);
    prelu->setOrder(outputTensorOrder);

    return prelu;
}
uint32_t maskToInt(mv::Shape mask) {
    uint32_t result = 0;

    for (size_t i = 0; i < mask.ndims(); i++) {
        switch (mask[i]) {
        case 1:
            result |= (0x1 << i);
            break;
        case 0:
            break;
        default:
            throw mv::RuntimeError("Convert to UPATask", "StridedSlice has wrong layer arguments");
        }
    }

    return result;
}

mv::Data::TensorIterator convertStridedSliceToUPATask(
        mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
        const std::map<std::string, mv::Attribute>& attrs,
        const std::string& name, bool /*software*/,
        const mv::QuantizationParams& quantParams,
        const mv::DType& outputTensorType,
        const mv::Order& outputTensorOrder)
{
    auto begins = attrs.at("begins").get<std::vector<unsigned>>();
    auto ends = attrs.at("ends").get<std::vector<unsigned>>();
    auto strides = attrs.at("strides").get<std::vector<unsigned>>();
    auto out_shape = attrs.at("out_shape").get<std::vector<unsigned>>();

    auto stridedSlice = om.uPATaskStridedSlice(name, inputs, begins, ends, strides, out_shape);

    stridedSlice->setDType(outputTensorType);
    stridedSlice->setQuantParams(quantParams);
    stridedSlice->setOrder(outputTensorOrder);
    return stridedSlice;
}

mv::Data::TensorIterator convertDepthToSpaceToUPATask(mv::OpModel& om, const std::vector<mv::Data::TensorIterator>& inputs,
                                                        const std::map<std::string, mv::Attribute>& attrs,
                                                        const std::string& name,  bool /*software*/,
                                                        const mv::QuantizationParams& quantParams,
                                                        const mv::DType& outputTensorType,
                                                        const mv::Order& outputTensorOrder)
{
    auto mode = attrs.at("mode").get<std::string>();
    auto block_size = attrs.at("block_size").get<uint32_t>();

    auto depthToSpace = om.uPATaskDepthToSpace(name, inputs, block_size, mode);
    depthToSpace->setDType(outputTensorType);
    depthToSpace->setQuantParams(quantParams);
    depthToSpace->setOrder(outputTensorOrder);
    return depthToSpace;
}

void convertOpsToTasksFcn(const mv::pass::PassEntry& pass,
                          mv::ComputationModel& model,
                          mv::TargetDescriptor&,
                          mv::Element&,
                          mv::Element&)
{

    MV_PROFILED_FUNCTION(MV_PROFILE_PASS)
    mv::OpModel om(model);
    mv::ControlModel cm(model);
    std::shared_ptr<mv::Element> globalParams = model.getGlobalConfigParams();
    //Note: Eltwise might be UPA might be DPU task...
    std::vector<std::string> opsTypesToConvert = {"Conv", "DepthwiseConv", "MaxPool", "Eltwise", "HwConvert", "LeakyRelu"};
    std::vector<std::string> opsTypesToConvertToUPA = {"Argmax", "Identity", "Softmax", "Proposal", "ROIPooling", "PSROIPooling",
                                                       "Quantize", "Resample", "Reshape", "RegionYolo", "ReorgYolo",
                                                       "Normalize", "DetectionOutput", "Priorbox", "Permute", "Interp",
                                                       "Norm", "FakeQuantize", "CustomOcl", "CustomCpp", "Sigmoid", "Deconv", "Tile", "CTCDecoder",
                                                       "RefConv", "Gather", "HSwish", "Swish", "Conversion", "Relu", "Tanh", "SoftPlus", "Elu",
                                                       "PermuteND", "Mish", "Floor", "Round", "Erf", "Gelu", "Pad", "Interpolate", "MVN", "Ceiling",
                                                       "Exp", "SpaceToDepth", "CTCGreedyDecoderSeqLen", "Log", "Prelu", "DepthToSpace", "ReverseSequence",
                                                       "StridedSlice"};

    opsTypesToConvert.insert(opsTypesToConvert.end(), opsTypesToConvertToUPA.begin(), opsTypesToConvertToUPA.end());
    auto opsToConvert = om.getOpsOfTypes(opsTypesToConvert);

    std::unordered_map<std::string,
            std::function<mv::Data::TensorIterator(mv::OpModel&, const std::vector<mv::Data::TensorIterator>&,
            const std::map<std::string, mv::Attribute>&, const std::string&, bool&,
            const mv::QuantizationParams&, const mv::DType&, const mv::Order&)>> opsFunctors = {
        {"Conv", convertConvolutionToDPUTask},
        {"DepthwiseConv", convertDepthwiseConvolutionToDPUTask},
        {"MaxPool", convertMaxPoolToDPUTask},
        {"HwConvert", convertHwConvertToDPUTask},
        {"Eltwise", convertEltwiseToTask},
        {"LeakyRelu", convertLReluToUPATask},
        {"Identity", convertIdentityToUPATask},
        {"Softmax", convertSoftmaxToUPATask},
        {"Proposal", convertProposalToUPATask},
        {"ROIPooling", convertROIPoolingToUPATask},
        {"PSROIPooling", convertPSROIPoolingToUPATask},
        {"Quantize", convertQuantizeToUPATask},
        {"Resample", convertResampleToUPATask},
        {"Reshape", convertReshapeToUPATask},
        {"RegionYolo", convertRegionYoloToUPATask},
        {"ReorgYolo", convertReorgYoloToUPATask},
        {"Normalize", convertNormalizeToUPATask},
        {"DetectionOutput", convertDetectionOutputToUPATask},
        {"Interp", convertInterpToUPATask},
        {"Norm", convertNormToUPATask},
        {"Priorbox", convertPriorboxToUPATask},
        {"Argmax", convertArgmaxToUPATask},
        {"Permute", mv::convertPermuteToUPATask},
        {"PermuteND", convertPermuteNDToUPATask},
        {"CustomOcl", convertCustomOclToUPATask},
        {"CustomCpp", convertCustomCppToUPATask},
        {"Sigmoid", convertSigmoidToUPATask},
        {"Deconv", convertDeconvToUPATask},
        {"Tile", convertTileToUPATask},
        {"CTCDecoder", convertCTCDecoderToUPATask},
        {"RefConv", convertRefConvToUPATask},
        {"FakeQuantize", convertFakeQuantizeToUPATask},
        {"Gather", convertGatherToUPATask},
        {"HSwish", convertHSwishToUPATask},
        {"Swish", convertSwishToUPATask},
        {"Mish", convertMishToUPATask},
        {"Floor", convertFloorToUPATask},
        {"Round", convertRoundToUPATask},
        {"Ceiling", convertCeilingToUPATask},
        {"Erf", convertErfToUPATask},
        {"Gelu", convertGeluToUPATask},
        {"Exp", convertExpToUPATask},
        {"Log", convertLogToUPATask},
        {"Conversion", convertConversionToUPATask},
        {"Relu", convertReluToUPATask},
        {"Tanh", convertTanhToUPATask},
        {"SoftPlus", convertSoftPlusToUPATask},
        {"Pad", convertPadToUPATask},
        {"Elu", convertEluToUPATask},
        {"Interpolate", convertInterpolateToUPATask},
        {"MVN", convertMVNToUPATask},
        {"SpaceToDepth", convertSpaceToDepthToUPATask},
        {"CTCGreedyDecoderSeqLen", convertCTCGreedyDecoderSeqLenToUPATask},
        {"Prelu", convertPreluToUPATask},
        {"DepthToSpace", convertDepthToSpaceToUPATask},
        {"ReverseSequence", convertReverseSequenceToUPATask},
        {"StridedSlice", convertStridedSliceToUPATask}
    };

    // Layer types that given current compiler state, it's
    // better to not propagate dtype through them
    const std::vector<std::string> dataTypeBarrierLayers = {"Slice"};

    for(auto& opType: opsTypesToConvert)
    {
        auto ops = opsToConvert[opType];
        for(auto& opIt: ops)
        {
            bool software = false;
            bool placed = false;
            //Note: That condition is coming due to limitations like add with different scales
            if (opIt->hasAttr("softwareExecuted") && opIt->get<bool>("softwareExecuted"))
                software = true;
            auto name = opIt->getName();
            auto attrsToCopy = opIt->getAttrs();
            auto inputs = opIt->getInputTensor();
            auto outputTensor = opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT);
            auto quantParams = outputTensor->getQuantParams();
            auto dType = outputTensor->getDType();
            auto order = outputTensor->getOrder();
            auto outputMemoryLocation = outputTensor->get<mv::Tensor::MemoryLocation>("Location");
            bool tensorResamplePlaced = false;
            mv::Shape tensorMasterDim;
            bool fusedConcatReshape = false;
            std::size_t numberOfConvsForAsymmetricalStride = 0;
            std::size_t asymmetricConvIndex = 0;

            if (outputTensor->hasAttr("placed") && outputTensor->hasAttr("masterDim"))
            {
                placed = true;
                tensorResamplePlaced = outputTensor->get<bool>("placed");
                tensorMasterDim =  outputTensor->get<mv::Shape>("masterDim");
            }
            if (outputTensor->hasAttr("fusedConcatReshape") && outputTensor->get<bool>("fusedConcatReshape"))
            {
                fusedConcatReshape = true;
                numberOfConvsForAsymmetricalStride = outputTensor->get<std::size_t>("numberOfConvsForAsymmetricalStride");
                asymmetricConvIndex = outputTensor->get<std::size_t>("asymmetricConvIndex");
            }
            auto hasLeadingOffset = outputTensor->hasAttr("leadingOffset");

            uint64_t leadingOffset = 0;
            if (hasLeadingOffset)
                leadingOffset =  outputTensor->get<uint64_t>("leadingOffset");
            auto inputControlFlows = mv::getInputControlFlow(cm, cm.switchContext(opIt));
            auto outputControlFlows = mv::getOutputControlFlow(cm, cm.switchContext(opIt));
            auto outputDataFlows = mv::getOutputDataFlow(om, opIt);
            mv::Data::TensorIterator newTensor;
            newTensor = opsFunctors[opType](om, inputs, attrsToCopy, name, software, quantParams, dType, order);

            newTensor->set<mv::Tensor::MemoryLocation>("Location", outputMemoryLocation);
            if (placed)
                newTensor->setPlaced(tensorResamplePlaced, tensorMasterDim);

            if (fusedConcatReshape)
            {
                newTensor->set<bool>("fusedConcatReshape", true);
                newTensor->set<std::size_t>("numberOfConvsForAsymmetricalStride", numberOfConvsForAsymmetricalStride);
                newTensor->set<std::size_t>("asymmetricConvIndex", asymmetricConvIndex);
            }

            auto newTensorOp = om.getSourceOp(newTensor);
            newTensorOp->setAttrs(attrsToCopy);
            auto newOpType = newTensorOp->getOpType();
            auto newOpTaskType = newTensorOp->get<std::string>("taskOp");

            if (newOpType == "DPUTask")
            {
                 if (hasLeadingOffset)
                    newTensor->set<uint64_t>("leadingOffset", leadingOffset);
            }
            else if (newOpTaskType == "Quantize" ||
                     newOpTaskType == "Conversion")
            {
                // Skip case for explicitly added Quantize operation
                // Conversion task will have properly configured DType and no need to override
                // it to FP16 similar as for other UPATask
            }
            else if (newOpType == "UPATask")
                newTensor->setDType(mv::DType("Float16"));

            setOutputDataFlow(om, newTensor, outputDataFlows);
            setInputControlFlow(cm, cm.switchContext(newTensorOp), inputControlFlows);
            setOutputControlFlow(cm, cm.switchContext(newTensorOp), outputControlFlows);

            // Handle dtype for implicit ops following a UPATask
            if(newOpType == "UPATask")
            {
                std::vector<mv::Data::OpListIterator> implicit_ops;
                std::queue<mv::Data::OpListIterator> op_itr_bfs;
                op_itr_bfs.push(newTensorOp);
                // BFS the implicit ops subtree to propagate the dtype change
                while (!op_itr_bfs.empty()) {
                    auto current_op_itr = op_itr_bfs.front();
                    for(auto outputFlow = current_op_itr.leftmostOutput();
                        outputFlow != om.flowEnd(); ++outputFlow) {
                        if (outputFlow.sink()->isImplicit() &&
                            std::find(dataTypeBarrierLayers.cbegin(),
                                dataTypeBarrierLayers.cend(), outputFlow.sink()->getOpType())
                                == dataTypeBarrierLayers.cend()) {
                            implicit_ops.push_back(outputFlow.sink());
                            op_itr_bfs.push(outputFlow.sink());
                        }
                    }
                    op_itr_bfs.pop();
                }
                for (auto implOp : implicit_ops)
                    for (auto implOutputTensor : implOp->getOutputTensor())
                        implOutputTensor->setDType(mv::DType("Float16"));
            }
        }
    }

    // Correct concat output dtype when all inputs & output are UPATasks
    auto concats = om.getOps("ImplicitConcat");
    for(auto& concatOp : concats)
    {
        auto all_inputs_are_fp16 = true;
        auto inputs = concatOp->getInputTensor();
        for (auto& input : inputs)
        {
            if (!((om.getSourceOp(input)->getOpType() == "UPATask" || om.getSourceOp(input)->getOpType() == "ImplicitReshape")  &&
                (input->getDType() == mv::DType("Float16"))))
                all_inputs_are_fp16 = false;
        }
        if (all_inputs_are_fp16)
        {
            auto outputs = concatOp->getOutputTensor();
            for (auto& output : outputs)
            {
                if (output->getDType() != mv::DType("Float16"))
                {
                    output->setDType(mv::DType("Float16"));
                }
            }
        }
    }

    // special logic for DetectionOutput Optimization
    auto upaOps = om.getOps("UPATask");
    for (auto &opIt : upaOps) {
        if (opIt->get<std::string>("taskOp") == "DetectionOutput") {
            opIt->getInputTensor(1)->setOrder(mv::Order("NCHW"));
        }
    }

    // Force output to UInt8 if specified
    for (auto &opIt : om.getOps()) {
        if (opIt->hasAttr("forceU8") && opIt->get<bool>("forceU8")) {
            pass.log(mv::Logger::MessageType::Debug, "Force output tensor to U8: " + opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT)->getName());
            opIt->getOutputTensor(mv::IO_TENSOR_OUTPUT)->setDType(mv::DType("UInt8"));
        }
    }
}

/**
 * @brief get the common shift and multipliers with the command shift.
 * @param scale the slope values of PReLU
 * @param num_bits the bit size for multiplier
 * @param mults the best multiplier with the common shift value
 * @param common_shift the common shift value
 */
template<typename MultDataT>
void get_best_mults_and_shift( const std::vector<double>& scale, unsigned num_bits, std::vector<MultDataT>& mults, uint8_t& common_shift )
{
    if( scale.size() == 0 ) return;

    // get the miminum shift value for the common shift value
    common_shift = 255; // set max
    for( auto s : scale )
    {
        double v = s;
        bool sign = std::signbit( v );
        if( sign ) v *= -1.0f;
        uint8_t num_shift = floor( num_bits - log2( v ) );
        if( common_shift > num_shift ) common_shift = num_shift;
    }

    // resize mults if the sizes mismatch
    if (scale.size() != mults.size())
        mults.resize(scale.size());

    // get multipliers with common shift
    for( size_t index = 0; index < scale.size(); ++index )
    {
        double v = scale[index];

        // get the sign bits
        bool sign = std::signbit( v );

        // if it is negative, then make it posive
        if( sign ) v *= -1.0f;

        MultDataT best_mult = round( pow( 2, common_shift ) * v );

        // get the sign bit back if it was a negative value
        if( sign ) best_mult *= -1;

        mults[index] = best_mult;
    }
}

void addPpeTask(mv::Data::OpListIterator &opIt, const std::vector<std::string>& ppeTaskTypes, double leakyReluHack, unsigned bits)
{
    auto ppeFixedFunction = mv::PPEFixedFunction();
    bool flexarbINT8 = false;
    if (std::find(ppeTaskTypes.begin(), ppeTaskTypes.end(), "FLEXARB") != ppeTaskTypes.end())
    {
        flexarbINT8= true;
    }
    //NOTE: the idea of the flex is that the post shift is not sign extendable so the clamps need to be like INT8
    ppeFixedFunction.setLowClamp(computeClampLow(opIt, flexarbINT8));
    ppeFixedFunction.setHighClamp(computeClampHigh(opIt, flexarbINT8));

    if (std::find(ppeTaskTypes.begin(), ppeTaskTypes.end(), "LeakyRelu") != ppeTaskTypes.end())
    {
        double leakyAlpha = 1.0;
        if (opIt->hasAttr("leakyAlpha"))
        {
            leakyAlpha = opIt->get<double>( "leakyAlpha" );
        }

        // NOTE: What are the default values here
        int32_t ppeMult=1;
        uint8_t ppeShift=0;
        if (leakyAlpha == 0.0)
        {
            ppeMult=0;
        }
        else if (leakyAlpha != 1.0)
        {
            int exponent;
            double mantissa;

            mantissa = std::frexp(leakyAlpha, &exponent);
            ppeShift = bits - exponent;
            ppeMult = (mantissa * pow(2, bits)) * leakyReluHack;
        }

        ppeFixedFunction.setLReluMult(ppeMult);
        ppeFixedFunction.setLReluShift(ppeShift);
    }
    else if (std::find(ppeTaskTypes.begin(), ppeTaskTypes.end(), "Prelu") != ppeTaskTypes.end())
    {
        if (opIt->hasAttr("slopes"))
        {
            const std::vector<double>& slopes = opIt->get<std::vector<double>>("slopes");
            std::vector<int32_t> mults( slopes.size() );
            uint8_t ppeShift = 0;

            get_best_mults_and_shift( slopes, bits, mults, ppeShift );

            ppeFixedFunction.setLReluMults( mults );
            ppeFixedFunction.setLReluShift( ppeShift );
        }
    }
    for(auto& ppeTaskType: ppeTaskTypes)
    {
        auto ppeLayerType = mv::PPELayerType(ppeTaskType);
        ppeFixedFunction.addLayer(ppeLayerType);
    }

    auto ppeTask = mv::PPETask(ppeFixedFunction);
    opIt->set<mv::PPETask>("PPETask", ppeTask);
}


// CLAMP FUNCTIONS FROM HERE

// ASSUMPTION:
// A model can give us clamp values either in I32 or FP32 or not clamp at all.
// The assumption is that in any case this clamp value IS NOT quantized.


// When outputDType is U8 or I8 we have to compute saturation clamp in every case
// U8 <- [-zp; 255 - zp]
// I8 <- [-128 - zp; +127 - zp] but ensure that zp is 0
// The clamp value is stored as is if compute type is U8. Otherwise it must be converted in S16.16
int32_t computeClampLow(mv::Data::OpListIterator &opIt, bool flex)
{
    auto computeDType = opIt->getInputTensor(0)->getDType();
    auto outputDType = opIt->getOutputTensor(0)->getDType();
    auto U8 = mv::DType("UInt8");
    auto I8 = mv::DType("Int8");
    auto FP16 = mv::DType("Float16");

    int32_t clamp = -2147483648;
    std::string taskOp = opIt->get<std::string>("taskOp");
    if (taskOp != "MaxPool")
    {
        if (outputDType == U8 || outputDType == I8)
        {
            // Saturation clamp has to be computed in this case
            mv::QuantizationParams outputQuantParams = opIt->getOutputTensor(0)->get<mv::QuantizationParams>("quantParams");
            int32_t saturationClamp = - outputQuantParams.getZeroPoint()[0];
            if(outputDType == I8)
                saturationClamp -= 128;

            clamp = saturationClamp;

            if(opIt->hasAttr("Maximum"))
            {
                double clampValue = opIt->get<double>("Maximum");
                double outputScale = outputQuantParams.getScale()[0];
                int32_t quantizedClampValue = static_cast<int32_t>(clampValue / outputScale);

                if(quantizedClampValue > clamp)
                    clamp = quantizedClampValue;
            }

            if(computeDType == FP16)
                clamp <<= 16;
        }

        else if (outputDType == FP16)
        {
            if(opIt->hasAttr("Maximum"))
            {
                double clampValue = opIt->get<double>("Maximum");

                if(computeDType == U8 || computeDType == I8)
                    clamp = static_cast<int32_t>(clampValue);
                else if (computeDType == FP16)
                    clamp = static_cast<int32_t>(clampValue * pow(2,16));
            }
            else if(opIt->hasAttr("postOpTypes"))
            {
                auto postOps = opIt->get<std::vector<std::string>>("postOpTypes");
                if (std::find(postOps.begin(), postOps.end(), "Relu") != postOps.end())
                    clamp = 0;
            }
        }
    }


    double alpha = 1.0;
    if (opIt->hasAttr("leakyAlpha"))
    {
        alpha = opIt->get<double>("leakyAlpha");

        if (alpha > 0.0)
        {
            clamp = static_cast<int32_t>(clamp / alpha);
        }
        else
        {
            // no negative values
            clamp = 0;
        }
    }
    else if (opIt->hasAttr("slopes"))
    {
        const std::vector<double>& slopes = opIt->get<std::vector<double>>( "slopes" );
        alpha = *std::min_element(slopes.begin(), slopes.end());
        if (alpha > 0.0)
        {
           clamp = static_cast<int32_t>(clamp / alpha);
        }
        else
        {
            // no negative values
            clamp = 0;
        }
    }

    // PWL activation runs immediately after clamp
    if(opIt->hasPWLActivation())
        clamp = computeDType == FP16 ? -4096
                  : std::max(-4096, static_cast<signed>(std::ceil(
            opIt->get<mv::QuantizationParams>("pwlQuantParams").getMin()[0] /
            opIt->get<mv::QuantizationParams>("pwlQuantParams").getScale()[0])));
    if (flex)
    {
        mv::QuantizationParams outputQuantParams = opIt->getOutputTensor(0)->get<mv::QuantizationParams>("quantParams");
        auto minimum = outputQuantParams.getMin()[0];
        if (alpha < 0.0)
        {
            minimum = 0; // no negative values
        }
        else if (alpha != 1.0)
        {
            minimum /= alpha;
        }

        clamp = round(minimum/outputQuantParams.getScale()[0]);
        clamp = std::max(clamp, -128);


        if(opIt->hasAttr("PWLClampLow")) {
            int clamp_plw_low = opIt->get<int>("PWLClampLow");
            clamp = std::max(clamp, clamp_plw_low);

            /* Remove unused attribute */
            opIt->erase("PWLClampLow");
        }
    }

    return clamp;
}

int32_t computeClampHigh(mv::Data::OpListIterator &opIt, bool flex)
{
    auto computeDType = opIt->getInputTensor(0)->getDType();
    auto outputDType = opIt->getOutputTensor(0)->getDType();
    auto U8 = mv::DType("UInt8");
    auto I8 = mv::DType("Int8");
    auto FP16 = mv::DType("Float16");

    int32_t clamp = 2147483647;
    std::string taskOp = opIt->get<std::string>("taskOp");
    if (taskOp != "MaxPool")
    {
        if (outputDType == U8 || outputDType == I8)
        {
            // Saturation clamp has to be computed in this case
            mv::QuantizationParams outputQuantParams = opIt->getOutputTensor(0)->get<mv::QuantizationParams>("quantParams");
            int32_t saturationClamp = - outputQuantParams.getZeroPoint()[0];
            if(outputDType == I8)
                saturationClamp += 127;
            else
                saturationClamp += 255;

            clamp = saturationClamp;

            if(opIt->hasAttr("Minimum"))
            {
                double clampValue = opIt->get<double>("Minimum");
                double outputScale = outputQuantParams.getScale()[0];
                int32_t quantizedClampValue = static_cast<int32_t>(clampValue / outputScale);

                if(quantizedClampValue < clamp)
                    clamp = quantizedClampValue;
            }

            if(computeDType == FP16)
                clamp <<= 16;
        }

        else if (outputDType == FP16)
        {
            if(opIt->hasAttr("Minimum"))
            {
                double clampValue = opIt->get<double>("Minimum");

                if(opIt->hasAttr("mixedToFloat") && opIt->get<bool>("mixedToFloat"))
                    clamp = static_cast<int32_t>(clampValue * pow(2,16));
                else if(computeDType == U8 || computeDType == I8)
                    clamp = static_cast<int32_t>(clampValue);
                else if (computeDType == FP16)
                    clamp = static_cast<int32_t>(clampValue * pow(2,16));
            }
        }
    }

    // PWL activation runs immediately after clamp
    if(opIt->hasPWLActivation())
        clamp = computeDType == FP16 ? 4095
                  : std::min(4095, static_cast<signed>(std::floor(
            opIt->get<mv::QuantizationParams>("pwlQuantParams").getMax()[0] /
            opIt->get<mv::QuantizationParams>("pwlQuantParams").getScale()[0])));
    if (flex)
    {
        mv::QuantizationParams outputQuantParams = opIt->getOutputTensor(0)->get<mv::QuantizationParams>("quantParams");
        auto maximum = outputQuantParams.getMax()[0];
        clamp = round(maximum/outputQuantParams.getScale()[0]);
        clamp = std::min(clamp, 127);

        if(opIt->hasAttr("PWLClampHigh")) {
            int clamp_plw_high = opIt->get<int>("PWLClampHigh");
            clamp = std::min(clamp, clamp_plw_high);

            /* Remove unused attribute */
            opIt->erase("PWLClampHigh");
        }
    }

    return clamp;
}

// TODO
// Check whether this function will be used in the future
#if defined(DISABLE_UNUSED_FUNCTIONS_REMOVAL)
// Calculate the permute_order
void convert_chw_to_index(std::string order, std::vector<unsigned>& permute_order)
{
    std::unordered_map<char, unsigned> chw_to_index = {
        {'C',2},
        {'H',1},
        {'W',0},
    };

    for (auto i = 0; i < 3; i++)
    {
        permute_order.at(i) = chw_to_index[order[i + 1]];
    }
}
#endif

// TODO
// Check whether the function is required as it is used nowhere
#if defined(DISABLE_UNUSED_FUNCTIONS_REMOVAL)
// Calculates positions of X, Y, & Z from permute_order
void calculate_xyz_from_permutation(std::vector<unsigned>& permute_order_xyz, std::vector<unsigned>& permute_order)
{
    for (unsigned i = 0; i < 3; i++)
    {
        for (unsigned j = 0; j < 3; j++)
        {
            if (permute_order.at(j) == i)
                permute_order_xyz.at(i) = j;
        }
    }
}
#endif
