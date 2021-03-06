//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

//

#pragma once

#include <ie_common.h>
#include "vpux/utils/plugin/profiling_parser.hpp"

namespace vpux {
namespace profiling {

enum class OutputType { NONE, TEXT, JSON };

std::map<std::string, InferenceEngine::InferenceEngineProfileInfo> convertProfilingLayersToIEInfo(
        std::vector<LayerInfo>& layerInfo);

// This function decodes profiling buffer into readable format.
// Format can be either regular text or TraceEvent json.
// outputFile is an optional argument - path to a file to store output. stdout if empty.
void outputWriter(const OutputType profilingType, const std::pair<const uint8_t*, uint64_t>& blob,
                  const std::pair<const uint8_t*, uint64_t>& profiling, const std::string& outputFile);

}  // namespace profiling
}  // namespace vpux
