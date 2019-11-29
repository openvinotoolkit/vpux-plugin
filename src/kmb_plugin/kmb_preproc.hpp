// Copyright (C) 2019 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <ie_blob.h>

#include <ie_preprocess_data.hpp>
#include <map>
#include <string>

namespace InferenceEngine {
namespace SippPreproc {

bool useSIPP();
bool isApplicable(
    const BlobMap& inputs, const std::map<std::string, PreProcessDataPtr>& preprocData, InputsDataMap& networkInputs);

void execSIPPDataPreprocessing(BlobMap& inputs, std::map<std::string, PreProcessDataPtr>& preprocData,
    InputsDataMap& networkInputs, int curBatch, bool serial, unsigned int numShaves = 4);

}  // namespace SippPreproc
}  // namespace InferenceEngine
