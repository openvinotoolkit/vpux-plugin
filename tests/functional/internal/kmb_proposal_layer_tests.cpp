//
// Copyright 2020 Intel Corporation.
//
// LEGAL NOTICE: Your use of this software and any required dependent software
// (the "Software Package") is subject to the terms and conditions of
// the Intel(R) OpenVINO(TM) Distribution License for the Software Package,
// which may also include notices, disclaimers, or license terms for
// third party or open source software included in or with the Software Package,
// and your use indicates your acceptance of all such terms. Please refer
// to the "third-party-programs.txt" or other similarly-named text file
// included with the Software Package for additional details.
//

#include "test_model/kmb_test_base.hpp"
#include "test_model/kmb_test_reshape_def.hpp"

#include <blob_factory.hpp>

struct ProposalTestParams final {
    SizeVector         cls_score_dims_;
    SizeVector         bbox_pred_dims_;
    std::vector<float> img_info_;
    ProposalParams     params_;

    ProposalTestParams& cls_score_dims(SizeVector cls_score_dims) {
        cls_score_dims_ = std::move(cls_score_dims);
        return *this;
    }

    ProposalTestParams& bbox_pred_dims(SizeVector bbox_pred_dims) {
        bbox_pred_dims_ = std::move(bbox_pred_dims);
        return *this;
    }

    ProposalTestParams& img_info(std::vector<float> img_info) {
        img_info_ = std::move(img_info);
        return *this;
    }

    ProposalTestParams& params(ProposalParams params) {
        params_ = std::move(params);
        return *this;
    }
};

std::ostream& operator<<(std::ostream& os, const ProposalTestParams& p) {
    vpux::printTo(os, "[cls_score_dims:{0}, bbox_pred_dims:{1}, img_info:{2}, params:{3}]",
                     p.cls_score_dims_, p.bbox_pred_dims_, p.img_info_, p.params_);
    return os;
}

class KmbProposalLayerTests : public KmbLayerTestBase,
                              public testing::WithParamInterface<ProposalTestParams> {};
TEST_P(KmbProposalLayerTests, AccuracyTest) {
    SKIP_INFER("bad results");

    const auto& p = GetParam();
    const auto precision = Precision::FP32;

    const auto cls_score_desc = TensorDesc(Precision::FP32, p.cls_score_dims_, Layout::NHWC);
    const auto bbox_pred_desc = TensorDesc(Precision::FP32, p.bbox_pred_dims_, Layout::NHWC);
    const auto img_info_desc = TensorDesc(Precision::FP32, {1, p.img_info_.size()}, Layout::NC);
    const auto shape_desc    = TensorDesc(Precision::I64,  {1                    }, Layout::C);
    const auto output_desc   = TensorDesc(Precision::FP32,                          Layout::NC);

    const auto range = std::make_pair(0.0f, 1.0f);
    const auto tolerance = 1e-3f;

    registerBlobGenerator(
        "input", cls_score_desc,
        [&](const TensorDesc& desc) {
            return genBlobUniform(desc, rd, range.first, range.second);
        }
    );

    registerBlobGenerator(
        "bbox_pred", bbox_pred_desc,
        [&](const TensorDesc& desc) {
            return genBlobUniform(desc, rd, range.first, range.second);
        }
    );

    registerBlobGenerator(
        "img_info", img_info_desc,
        [&](const TensorDesc& desc) {
            auto img_info_blob = make_shared_blob<float>(TensorDesc(Precision::FP32,
                                                                    desc.getDims(),
                                                                    desc.getLayout()));
            img_info_blob->allocate();
            CopyVectorToBlob(img_info_blob, p.img_info_);
            return img_info_blob;
        }
    );

    registerBlobGenerator(
        "shape", shape_desc,
        [&](const TensorDesc& desc) {
            auto blob = make_blob_with_precision(desc);
            blob->allocate();
            CopyVectorToBlob(blob, SizeVector{p.img_info_.size()});
            return blob;
        }
    );

    const auto builder = [&](TestNetwork& testNet) {
        testNet
            .setUserInput("input", cls_score_desc.getPrecision(), cls_score_desc.getLayout())
            .addNetInput("input", cls_score_desc.getDims(), precision)
            .setUserInput("bbox_pred", bbox_pred_desc.getPrecision(), bbox_pred_desc.getLayout())
            .addNetInput("bbox_pred" , bbox_pred_desc.getDims(), precision)
            .setUserInput("img_info" , img_info_desc.getPrecision(), img_info_desc.getLayout())
            .addNetInput("img_info"  , img_info_desc.getDims(), precision)
            .addConst("shape", getBlobByName("shape"))
            .addLayer<ReshapeLayerDef>("reshape")
                .input("img_info")
                .shape("shape")
                .build()
            .addLayer<ProposalLayerDef>("proposal", p.params_)
                .scores("input")
                .boxDeltas("bbox_pred")
                .imgInfo("reshape")
                .build()
            .addNetOutput(PortInfo("proposal"))
            .setUserOutput(PortInfo("proposal"), output_desc.getPrecision(), output_desc.getLayout())
            .finalize();
    };

    runTest(builder, tolerance, CompareMethod::Absolute);
}

const std::vector<ProposalTestParams> proposalParams {
    // Configuration from RFCN network
    ProposalTestParams()
        .cls_score_dims({1, 18, 14, 14})
        .bbox_pred_dims({1, 36, 14, 14})
        .img_info({224.f, 224.f, 1.f})
        .params(ProposalParams().feat_stride(16u)
                                .base_size(16u)
                                .min_size(16u)
                                .nms_thresh(0.7)
                                .normalize(false)
                                .post_nms_topn(1)
                                .pre_nms_topn(6000)
                                .for_deformable(false)
                                .framework("tensorflow")
                                .clip_before_nms(true)
                                .clip_after_nms(false)
                                .box_scale(1.0)
                                .box_coord_scale(1.0)
                                .ratio({0.5, 1, 2})
                                .scale({8, 16, 32})),

        // Configuration from mcmCompiler proposal layer test
        ProposalTestParams()
            .cls_score_dims({1, 24, 14, 14})
            .bbox_pred_dims({1, 48, 14, 14})
            .img_info({224.f, 224.f, 1.f})
            .params(ProposalParams().feat_stride(16u)
                                    .base_size(256u)
                                    .min_size(10u)
                                    .nms_thresh(0.7)
                                    .normalize(false)
                                    .post_nms_topn(1)
                                    .pre_nms_topn(2147483647)
                                    .for_deformable(false)
                                    .framework("tensorflow")
                                    .clip_before_nms(true)
                                    .clip_after_nms(false)
                                    .box_scale(5.0)
                                    .box_coord_scale(10.0)
                                    .ratio({0.5, 1, 2})
                                    .scale({0.25, 0.5, 1, 2})),
        ProposalTestParams()
            .cls_score_dims({1, 24, 14, 14})
            .bbox_pred_dims({1, 48, 14, 14})
            .img_info({224.f, 224.f, 1.f, 1.f})
            .params(ProposalParams().feat_stride(16u)
                                    .base_size(16u)
                                    .min_size(10u)
                                    .nms_thresh(0.7)
                                    .normalize(false)
                                    .post_nms_topn(1)
                                    .pre_nms_topn(6000)
                                    .for_deformable(false)
                                    .framework("caffe")
                                    .clip_before_nms(true)
                                    .clip_after_nms(false)
                                    .box_scale(1.0)
                                    .box_coord_scale(1.0)
                                    .ratio({0.5, 1, 2})
                                    .scale({8.0, 16.0, 32.0}))
};

INSTANTIATE_TEST_SUITE_P(precommit_SomeCase, KmbProposalLayerTests, testing::ValuesIn(proposalParams));