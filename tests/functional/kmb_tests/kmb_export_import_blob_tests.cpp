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

#include <fstream>
#include <vpu/kmb_plugin_config.hpp>

#include "kmb_layers_tests.hpp"

#include <ie_icnn_network_stats.hpp>
#include <cnn_network_int8_normalizer.hpp>
#include <ie_util_internal.hpp>

#define ERROR_BOUND (.1f)

using namespace InferenceEngine;
using namespace InferenceEngine::details;

enum class FileIOResult { FileNotOpened = -1, FilesWithDifferentSize = -2, FilesHaveEqualSize = 1 };

#ifdef ENABLE_MCM_COMPILER
// This function calculate size of file and is used in tests below to check that
// two files with blobs have the same size
size_t getFileSize(const std::string &fileName) {
    std::ifstream file(fileName.c_str(), std::ifstream::in | std::ifstream::binary);

    if(!file.is_open()) {
        return static_cast<size_t>(FileIOResult::FileNotOpened);
    }

    file.seekg(0, std::ios::end);
    size_t sizeOfFile = file.tellg();
    file.close();

    return sizeOfFile;
}

// Function for comparison of content of two blob files
FileIOResult isContentOfFilesEqual (const std::string &fileName1, const std::string &fileName2){
    std::ifstream file1(fileName1.c_str(), std::ifstream::in | std::ifstream::binary);
    std::ifstream file2(fileName2.c_str(), std::ifstream::in | std::ifstream::binary);

    if( !file1.is_open() || !file2.is_open() ) {
        return FileIOResult :: FileNotOpened;
    }

    char x, y;

    while ( !file1.eof() || !file2.eof() )
    {
        file1.read(&x, 1);
        file2.read(&y, 1);
        if ( x != y )
            return FileIOResult :: FilesWithDifferentSize;
    }
    return FileIOResult :: FilesHaveEqualSize;
}

// This function is call at the end part of each test.
// At the begining of each test it is necessary to set up network (CNNNetwork) and its configuration.
// After that you could call this function to make all of remaining work.
void ExportImportBlobToFromFile(const CNNNetwork& network, const std::map<std::string, std::string>& config, const std::string& testDescription ) {
    Core ie;
    ExecutableNetwork exeNetwork = ie.LoadNetwork(network, "KMB", config);

    std::string blobFileName1 = testDescription + "ExportBlob01.blob";
    exeNetwork.Export(blobFileName1);
    ASSERT_GT( getFileSize(blobFileName1), 0 ) << "Alarm! Alarm! We have gotten blob file with zero size!!!";

    //  Try to import executable network and write it back to file via Export()
    ExecutableNetwork importedNetwork = ie.ImportNetwork(blobFileName1, "KMB", config);
    std::string blobFileName2 = testDescription + "ExportBlob02.blob";
    importedNetwork.Export(blobFileName2);

    // Check for non-zero size of files and  compare them by size
    ASSERT_GT( getFileSize(blobFileName1), 0 ); // Test to be sure that first file size is not zero.
    ASSERT_GT( getFileSize(blobFileName2), 0 ); // Test to be sure that second file size is not zero.
    ASSERT_EQ(getFileSize(blobFileName1), getFileSize(blobFileName2)); // And now compare size of first and second file

    //  Compare contents of files
    ASSERT_EQ( isContentOfFilesEqual(blobFileName1, blobFileName2), FileIOResult::FilesHaveEqualSize );
}

TEST_F(kmbLayersTests_nightly, TestExportImportBlob01) {

    extern std::string conv_after_scale_shift;
    std::string model = conv_after_scale_shift;
    REPLACE_WITH_STR(model, "<biases offset=\"6\" size=\"6\"/>", " ");
    REPLACE_WITH_STR(model, "<biases offset=\"18828\" size=\"128\"/>", " ");

    ASSERT_NO_THROW(_net_reader.ReadNetwork(model.data(), model.length()));
    ASSERT_TRUE(_net_reader.isParseSuccess());

    std::size_t weightSize = 6 + 18816;
    std::size_t biasSize = 6 + 128;
    TBlob<uint8_t>::Ptr weightsBlob(GenWeights(weightSize + biasSize));
    ASSERT_NO_THROW(_net_reader.SetWeights(weightsBlob));

    CNNNetwork network = _net_reader.getNetwork();

    _inputsInfo = network.getInputsInfo();
    _inputsInfo["input"]->setPrecision(Precision::FP16);

    _outputsInfo = network.getOutputsInfo();
    _outputsInfo["conv_test1"]->setPrecision(Precision::FP16);

    std::map<std::string, std::string> config;
    setCommonConfig(config);
    config[VPU_KMB_CONFIG_KEY(MCM_PARSING_ONLY)] = CONFIG_VALUE(NO);
    config[VPU_KMB_CONFIG_KEY(MCM_GENERATE_BLOB)] = CONFIG_VALUE(YES);
    config[VPU_KMB_CONFIG_KEY(MCM_GENERATE_DOT)] = CONFIG_VALUE(YES);
    config[VPU_KMB_CONFIG_KEY(MCM_GENERATE_JSON)] = CONFIG_VALUE(YES);

    ExportImportBlobToFromFile(network, config, "Test01" );
}

TEST_F(kmbLayersTests_nightly, TestExportImportBlob02) {

    extern std::string full_quant_model;

    std::string model = full_quant_model;

    REPLACE_WITH_STR(model, "<biases offset=\"147456\" size=\"256\"/>", " ");
    REPLACE_WITH_STR(model, "<biases offset=\"213248\" size=\"1024\"/>", " ");

    ASSERT_NO_THROW(_net_reader.ReadNetwork(model.data(), model.length()));
    ASSERT_TRUE( _net_reader.isParseSuccess() );

    StatusCode sts;
    InferenceEngine::ResponseDesc response;
    std::map<std::string, std::string> config;
    ExecutableNetwork executableNetwork;
    details::CNNNetworkImplPtr clonedNetwork;
    CNNNetworkInt8Normalizer cnnorm;

    setCommonConfig(config);
    config[VPU_KMB_CONFIG_KEY(MCM_PARSING_ONLY)] = CONFIG_VALUE(NO);
    config[VPU_KMB_CONFIG_KEY(MCM_GENERATE_BLOB)] = CONFIG_VALUE(YES);
    config[VPU_KMB_CONFIG_KEY(MCM_GENERATE_DOT)] = CONFIG_VALUE(YES);
    config[VPU_KMB_CONFIG_KEY(MCM_GENERATE_JSON)] = CONFIG_VALUE(YES);

    std::size_t weightSize = 147456 + 65536;
    std::size_t biasSize = 256 + 1024;
    TBlob<uint8_t>::Ptr weightsBlob(GenWeights(weightSize + biasSize));
    ASSERT_NO_THROW(_net_reader.SetWeights(weightsBlob));

    CNNNetwork network = _net_reader.getNetwork();

    _inputsInfo = network.getInputsInfo();
    _inputsInfo["input"]->setPrecision(Precision::FP32);

    _outputsInfo = network.getOutputsInfo();
    _outputsInfo["conv2"]->setPrecision(Precision::FP32);

    ICNNNetworkStats* pstats = nullptr;
    StatusCode s = ((ICNNNetwork&)network).getStats(&pstats, nullptr);

    ASSERT_EQ(StatusCode::OK, s);

    if ( ! pstats->isEmpty() ) {
        clonedNetwork = cloneNet(network);
        //cnnorm.NormalizeNetwork(*clonedNetwork, *pstats);
        InferenceEngine::details::CNNNetworkInt8Normalizer::NormalizeNetwork(*clonedNetwork, *pstats);
    }

    ExportImportBlobToFromFile(CNNNetwork(clonedNetwork), config, "Test02" );
}


TEST_F(kmbLayersTests_nightly, TestExportImportBlob03) {
    const std::string model = R"V0G0N(
    <net batch="1" name="POOLING_TEST" version="2">
        <layers>
            <layer id="0" name="input" precision="FP16" type="Input">
                <output>
                    <port id="0">
                        <dim>1</dim>
                        <dim>3</dim>
                        <dim>224</dim>
                        <dim>224</dim>
                    </port>
                </output>
            </layer>
            <layer id="1" name="pooling_test" precision="FP16" type="Pooling">
                <data auto_pad="same_upper" exclude-pad="true" kernel="3,3" pads_begin="0,0" pads_end="1,1" pool-method="max" strides="2,2"/>
                <input>
                    <port id="0">
                        <dim>1</dim>
                        <dim>3</dim>
                        <dim>224</dim>
                        <dim>224</dim>
                    </port>
                </input>
                <output>
                    <port id="1">
                        <dim>1</dim>
                        <dim>3</dim>
                        <dim>224</dim>
                        <dim>224</dim>
                    </port>
                </output>
            </layer>
            </layers>
        <edges>
            <edge from-layer="0" from-port="0" to-layer="1" to-port="0"/>
        </edges>
    </net>
        )V0G0N";

    StatusCode st;

    ASSERT_NO_THROW(_net_reader.ReadNetwork(model.data(), model.length()));
    ASSERT_TRUE(_net_reader.isParseSuccess());

    auto network = _net_reader.getNetwork();

    _inputsInfo = network.getInputsInfo();
    _inputsInfo["input"]->setPrecision(Precision::FP16);

    _outputsInfo = network.getOutputsInfo();
    _outputsInfo["pooling_test"]->setPrecision(Precision::FP16);

    std::map<std::string, std::string> config;
    setCommonConfig(config);
    config[VPU_KMB_CONFIG_KEY(MCM_PARSING_ONLY)] = CONFIG_VALUE(NO);
    config[VPU_KMB_CONFIG_KEY(MCM_GENERATE_BLOB)] = CONFIG_VALUE(YES);
    config[VPU_KMB_CONFIG_KEY(MCM_GENERATE_DOT)] = CONFIG_VALUE(YES);
    config[VPU_KMB_CONFIG_KEY(MCM_GENERATE_JSON)] = CONFIG_VALUE(YES);

    ExportImportBlobToFromFile(network, config, "Test03" );
}


TEST_F(kmbLayersTests_nightly, TestExportImportBlob04) {
    const std::string model = R"V0G0N(
        <net batch="1" name="RELU_TEST" version="2">
            <layers>
                <layer id="0" name="input" precision="FP16" type="Input">
                    <output>
                        <port id="0">
                            <dim>1</dim>
                            <dim>64</dim>
                            <dim>112</dim>
                            <dim>112</dim>
                        </port>
                    </output>
                </layer>
                <layer id="3" name="relu_test" precision="FP16" type="ReLU">
                    <input>
                        <port id="0">
                            <dim>1</dim>
                            <dim>64</dim>
                            <dim>112</dim>
                            <dim>112</dim>
                        </port>
                    </input>
                    <output>
                        <port id="1">
                            <dim>1</dim>
                            <dim>64</dim>
                            <dim>112</dim>
                            <dim>112</dim>
                        </port>
                    </output>
                </layer>
            </layers>
            <edges>
                <edge from-layer="0" from-port="0" to-layer="3" to-port="0"/>
            </edges>
        </net>
            )V0G0N";


    ASSERT_NO_THROW(_net_reader.ReadNetwork(model.data(), model.length()));
    ASSERT_TRUE(_net_reader.isParseSuccess());

    auto network = _net_reader.getNetwork();

    _inputsInfo = network.getInputsInfo();
    _inputsInfo["input"]->setPrecision(Precision::FP16);

    _outputsInfo = network.getOutputsInfo();
    _outputsInfo["relu_test"]->setPrecision(Precision::FP16);

    std::map<std::string, std::string> config;
    setCommonConfig(config);
    config[VPU_KMB_CONFIG_KEY(MCM_PARSING_ONLY)] = CONFIG_VALUE(NO);
    config[VPU_KMB_CONFIG_KEY(MCM_GENERATE_BLOB)] = CONFIG_VALUE(YES);
    config[VPU_KMB_CONFIG_KEY(MCM_GENERATE_DOT)] = CONFIG_VALUE(YES);
    config[VPU_KMB_CONFIG_KEY(MCM_GENERATE_JSON)] = CONFIG_VALUE(YES);

    ExportImportBlobToFromFile(network, config, "Test04" );

}

#endif

