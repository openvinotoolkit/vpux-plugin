// Copyright (C) 2020 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include <vector>
#include "single_layer_tests/topk.hpp"
#include "kmb_layer_test.hpp"

namespace LayerTestsDefinitions {

    class KmbTopKLayerTest : virtual public TopKLayerTest, virtual public LayerTestsUtils::KmbLayerTestsCommon {
        void SkipBeforeLoad() override {
            if (isCompilerMCM()) {
                throw LayerTestsUtils::KmbSkipTestException("TopK is not enabled for MCM compiler");
            }
        }
    };
    
    class KmbTopKLayerTest_MTL : public TopKLayerTest, virtual public LayerTestsUtils::KmbLayerTestsCommon {
        void SkipBeforeInfer() override {
            throw LayerTestsUtils::KmbSkipTestException("Runtime issue.");
        }
        
        void SkipBeforeLoad() override {
            if(isPlatformMTL()) {
                if (std::getenv("OV_BUILD_DIR") == nullptr) {
                    throw LayerTestsUtils::KmbSkipTestException("OV_BUILD_DIR env directory must be specified, in order to reach act-shave kernels.");
                }
    
#if defined(__arm__) || defined(__aarch64__) || defined(_WIN32) || defined(_WIN64)
                throw LayerTestsUtils::KmbSkipTestException("Does not compile on ARM and Windows.");
#endif
            }
        }
    };

    TEST_P(KmbTopKLayerTest, CompareWithRefs) {
        Run();
    }

    TEST_P(KmbTopKLayerTest, CompareWithRefs_MLIR) {
        useCompilerMLIR();
        Run();
    }
    
    TEST_P(KmbTopKLayerTest_MTL, CompareWithRefs_MLIR){
        useCompilerMLIR();
        setPlatformMTL();
        setDefaultHardwareModeMLIR();
        Run();
    }

}  // namespace LayerTestsDefinitions

using namespace LayerTestsDefinitions;

namespace {

    const std::vector<InferenceEngine::Precision> netPrecisions = {
            InferenceEngine::Precision::FP16
    };

    const std::vector<int64_t> axes = {0, 1, 2};

    const std::vector<int64_t> k = {1, 5, 10};

    const std::vector<ngraph::opset4::TopK::Mode> modes = {
            ngraph::opset4::TopK::Mode::MIN,
            ngraph::opset4::TopK::Mode::MAX
    };

    const std::vector<ngraph::opset4::TopK::SortType> sortTypes = {
            // The implements of SortType::NONE are different.
            // Reference uses std::nth_element and returns k out-of-order values.
            // Kernel returns k data sorted in values. nth_element causes computation increase.
            // ngraph::opset4::TopK::SortType::NONE,
            ngraph::opset4::TopK::SortType::SORT_INDICES,
            ngraph::opset4::TopK::SortType::SORT_VALUES,
    };

// [Track number: S#41824]
    INSTANTIATE_TEST_SUITE_P(smoke_TopK, KmbTopKLayerTest,
                             ::testing::Combine(
                                     ::testing::ValuesIn(k),
                                     ::testing::ValuesIn(axes),
                                     ::testing::ValuesIn(modes),
                                     ::testing::ValuesIn(sortTypes),
                                     ::testing::ValuesIn(netPrecisions),
                                     ::testing::Values(InferenceEngine::Precision::FP16),
                                     ::testing::Values(InferenceEngine::Precision::UNSPECIFIED),
                                     ::testing::Values(InferenceEngine::Layout::ANY),
                                     ::testing::Values(std::vector<size_t>({10, 10, 10})),
                                     ::testing::Values(LayerTestsUtils::testPlatformTargetDevice)),
                             TopKLayerTest::getTestCaseName);
    
    INSTANTIATE_TEST_SUITE_P(smoke_TopK, KmbTopKLayerTest_MTL,
                             ::testing::Combine(
                                     ::testing::ValuesIn(k),
                                     ::testing::ValuesIn(axes),
                                     ::testing::ValuesIn(modes),
                                     ::testing::ValuesIn(sortTypes),
                                     ::testing::ValuesIn(netPrecisions),
                                     ::testing::Values(InferenceEngine::Precision::FP16),
                                     ::testing::Values(InferenceEngine::Precision::UNSPECIFIED),
                                     ::testing::Values(InferenceEngine::Layout::ANY),
                                     ::testing::Values(std::vector<size_t>({10, 10, 10})),
                                     ::testing::Values(LayerTestsUtils::testPlatformTargetDevice)),
                             TopKLayerTest::getTestCaseName);
}  // namespace
