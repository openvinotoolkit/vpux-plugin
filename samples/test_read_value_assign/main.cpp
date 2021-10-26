//
// Copyright 2019 Intel Corporation.
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

#include <algorithm>
#include <fstream>
#include <limits>
#include <list>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <inference_engine.hpp>

#include <samples/args_helper.hpp>
#include <samples/common.hpp>
#include <samples/slog.hpp>

// #include "detection_sample_yolov2tiny.h"
#include "region_yolov2tiny.h"

#include <format_reader_ptr.h>
#include <ie_compound_blob.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <yolo_helpers.hpp>

#include "vpux/utils/IE/blob.hpp"

using namespace InferenceEngine;

/**
 * @brief The entry point the Inference Engine sample application
 * @file detection_sample/main.cpp
 * @example detection_sample/main.cpp
 */
// int argc, char* argv[]
int main() {
    slog::info << "InferenceEngine: " << GetInferenceEngineVersion() << slog::endl;
    slog::info << "Creating Inference Engine" << slog::endl;
    Core ie;

    // --------------------------- 2. Read blob Generated by MCM Compiler ----------------------------------
    std::string binFileName = "";
    slog::info << "Loading blob:\t" << binFileName << slog::endl;

    ExecutableNetwork importedNetwork = ie.ImportNetwork(binFileName, "KMB", {});
    // -----------------------------------------------------------------------------------------------------

    // --------------------------- 3. Configure input & output ---------------------------------------------
    ConstInputsDataMap inputInfo = importedNetwork.GetInputsInfo();
    if (inputInfo.empty())
        throw std::logic_error("inputInfo is empty");

    // -----------------------------------------------------------------------------------------------------

    // --------------------------- 4. Create infer request -------------------------------------------------
    InferenceEngine::InferRequest inferRequest = importedNetwork.CreateInferRequest();
    slog::info << "CreateInferRequest completed successfully" << slog::endl;
    // -----------------------------------------------------------------------------------------------------

    // --------------------------- 5. Prepare input --------------------------------------------------------
    /** Use first input blob **/

    auto inputIterator = inputInfo.begin();
    // input0
    {
        TensorDesc inputDataDesc = inputIterator->second->getTensorDesc();
        auto firstInputName = inputIterator->first;
        std::vector<size_t> inputBlobDims = inputDataDesc.getDims();
        const size_t imageWidth  = 1000;
        const size_t imageHeight = 1000;

        uint8_t inputData0[1000 * 1000];
        for (int i = 0; i < static_cast<int>(imageWidth * imageHeight); i++) inputData0[i] = 1.0;

        Blob::Ptr imageBlob =
                make_shared_blob<uint8_t>(TensorDesc(Precision::U8, inputBlobDims, Layout::NHWC), static_cast<uint8_t*>(inputData0));

        inferRequest.SetBlob(firstInputName.c_str(), imageBlob);
    }
    // input1
    {
        inputIterator++;

        TensorDesc inputDataDesc = inputIterator->second->getTensorDesc();
        auto inputName = inputIterator->first;
        std::vector<size_t> inputBlobDims = inputDataDesc.getDims();

        uint8_t inputData0[1000 * 1000];
        for (int i = 0; i < static_cast<int>(1000 * 1000 * sizeof(int16_t)); i++) inputData0[i] = 42;

        Blob::Ptr imageBlob = make_shared_blob<uint8_t>(TensorDesc(Precision::U8, inputBlobDims, Layout::NHWC), static_cast<uint8_t*>(inputData0));

        inferRequest.SetBlob(inputName.c_str(), imageBlob);
    }

    const int numIterations = 1;
    for (int i = 0; i < numIterations; i++) {
        inferRequest.Infer();
    }

    ConstOutputsDataMap outputInfo = importedNetwork.GetOutputsInfo();

    // take the second output
    auto outputIterator = outputInfo.begin();
    outputIterator++;
    // END here
    {
        std::string outputName = outputInfo.begin()->first;
        Blob::Ptr outputBlob = inferRequest.GetBlob(outputName.c_str());

        uint8_t *poutput = static_cast<uint8_t*>(outputBlob->buffer());

        for (int i = 0; i < static_cast<int>(143 * sizeof(int16_t)); i++) {
            std::cout << poutput[i] << ", ";
        }
        std::cout << std::endl;

        std::cout << "outputName = " << outputName << std::endl;
    }

    slog::info << "Execution successful" << slog::endl;
    return 0;
}
