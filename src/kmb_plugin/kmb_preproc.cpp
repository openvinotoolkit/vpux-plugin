// Copyright (C) 2019 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "kmb_preproc.hpp"

#include <map>
#include <memory>
#include <string>

#include "kmb_allocator.h"

#ifdef ENABLE_VPUAL
#include "kmb_preproc_pool.hpp"
#endif

namespace InferenceEngine {

namespace SippPreproc {
bool useSIPP() {
    static const bool USE_SIPP = [](const char* str) -> bool {
        std::string var(str ? str : "");
        return var == "Y" || var == "YES" || var == "ON" || var == "1";
    }(std::getenv("USE_SIPP"));

    return USE_SIPP;
}

static bool supported(ResizeAlgorithm interp, ColorFormat inFmt) {
    return (interp == RESIZE_BILINEAR) && (inFmt == ColorFormat::NV12);
}

bool isApplicable(const InferenceEngine::BlobMap& inputs, const std::map<std::string, PreProcessDataPtr>& preprocData,
    InputsDataMap& networkInputs) {
    if (inputs.size() != 1) return false;

    for (auto& input : inputs) {
        const auto& blobName = input.first;
        auto it = preprocData.find(blobName);
        if (it != preprocData.end()) {
            const auto& preprocInfo = networkInputs[blobName]->getPreProcess();
            if (!supported(preprocInfo.getResizeAlgorithm(), preprocInfo.getColorFormat())) {
                return false;
            }
        }
    }
    return true;
}

void execSIPPDataPreprocessing(InferenceEngine::BlobMap& inputs, std::map<std::string, PreProcessDataPtr>& preprocData,
    InferenceEngine::InputsDataMap& networkInputs, InferenceEngine::ColorFormat out_format, unsigned int numShaves,
    unsigned int lpi, const size_t& deviceId) {
#ifdef ENABLE_VPUAL
    IE_ASSERT(numShaves > 0 && numShaves <= 16)
        << "SippPreproc::execSIPPDataPreprocessing "
        << "attempt to set invalid number of shaves for SIPP: " << numShaves << ", valid numbers are from 1 to 16";
    sippPreprocPool().execSIPPDataPreprocessing(
        {inputs, preprocData, networkInputs, out_format}, numShaves, lpi, deviceId);
#else
    UNUSED(inputs);
    UNUSED(preprocData);
    UNUSED(networkInputs);
    UNUSED(out_format);
    UNUSED(numShaves);
    UNUSED(lpi);

#endif
}
}  // namespace SippPreproc
}  // namespace InferenceEngine
