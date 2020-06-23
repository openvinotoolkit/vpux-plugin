//
// Copyright 2019 Intel Corporation.
//
// This software and the related documents are Intel copyrighted materials,
// and your use of them is governed by the express license under which they
// were provided to you (End User License Agreement for the Intel(R) Software
// Development Products (Version May 2017)). Unless the License provides
// otherwise, you may not use, modify, copy, publish, distribute, disclose or
// transmit this software or the related documents without Intel's prior
// written permission.
//
// This software and the related documents are provided as is, with no
// express or implied warranties, other than those that are expressly
// stated in the License.
//

#include "kmb_layers_tests.hpp"

#define ERROR_BOUND (1.e-3f)

using namespace InferenceEngine;

class kmbLayersTestsBias_nightly : public kmbLayersTests_nightly, public testing::WithParamInterface<SizeVector> {};

#ifdef ENABLE_MCM_COMPILER

// [Track number: S#27224]
TEST_P(kmbLayersTestsBias_nightly, DISABLED_TestsBias) {
    auto dim = GetParam();
    std::size_t biasesSize = 1;
    for (uint32_t pos = 0; pos < dim.size(); ++pos) {
        biasesSize *= dim[pos];
    }

    TBlob<uint8_t>::Ptr weightsBlob(GenWeights<uint16_t>(0 + biasesSize));
    SetInputTensors({dim});
    SetOutputTensors({dim});

    NetworkInit("Bias", nullptr, 0, biasesSize, weightsBlob,
        Precision::FP16  // output precision
    );
}

static std::vector<SizeVector> s_biasDims = {{{1, 32, 10, 10}}, {{1, 8, 4, 4}}};

INSTANTIATE_TEST_CASE_P(accuracy, kmbLayersTestsBias_nightly, ::testing::ValuesIn(s_biasDims));

#endif
