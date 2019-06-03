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

#include <thread>
#include <chrono>
#include <gtest/gtest.h>
#include <regression_tests.hpp>
#include <string>
#include <inference_engine/precision_utils.h>
#include <vpu/vpu_plugin_config.hpp>
#include <vpu/private_plugin_config.hpp>

#include <ie_icnn_network_stats.hpp>
#include <cnn_network_int8_normalizer.hpp>
#include <ie_util_internal.hpp>

using namespace ::testing;
using namespace InferenceEngine;
using namespace Regression::Matchers;
using namespace InferenceEngine::details;

namespace
{

class CompilationParameter {
public:
    CompilationParameter() = default;
    inline CompilationParameter(std::string name,
                                std::string path_to_network,
                                std::string path_to_weights);
    //Accessors

    inline std::string name() const;
    inline std::string pathToNetwork() const;
    inline std::string pathToWeights() const;

protected:
    //Data section
    std::string name_;
    std::string path_to_network_;
    std::string path_to_weights_;
};

using CompilationTestParam = WithParamInterface<std::tuple<std::string, CompilationParameter>>;

class VpuNoRegressionWithCompilation : public Regression::RegressionTests,
                                       public CompilationTestParam {
public:
    using TestParam = WithParamInterface<std::tuple<std::string, CompilationParameter>>;

    // Operations
    static std::string getTestCaseName(TestParamInfo <CompilationTestParam::ParamType> param);
    inline void loadNetworkWrapper(std::map<std::string, std::string> config);

    // Accessors
    std::string getPluginName() const override ;
    std::string getDeviceName() const override ;
    std::map<std::string, std::string> _config;

protected:
    // Data section
    std::string pluginName;
    CompilationParameter path_to_files;

    //Operations
    void SetUp() override;
};

std::string VpuNoRegressionWithCompilation::getTestCaseName(
        TestParamInfo <CompilationTestParam::ParamType> param) {
    return std::string("Main_") +
           get<0>(param.param) +
           std::string("_") + get<1>(param.param).name();
}

void VpuNoRegressionWithCompilation::SetUp() {
    pluginName = get<0>(TestParam::GetParam());
    path_to_files = get<1>(TestParam::GetParam());

    PluginCache::get().reset();
}

inline std::string VpuNoRegressionWithCompilation::getPluginName() const {
    return pluginName;
}

inline std::string VpuNoRegressionWithCompilation::getDeviceName() const {
    return "";
}

class KmbNoRegressionCompilationOnly : public VpuNoRegressionWithCompilation {
};

inline CompilationParameter::CompilationParameter(
        std::string name,
        std::string path_to_network,
        std::string path_to_weights):
        name_(name),
        path_to_network_(path_to_network),
        path_to_weights_(path_to_weights)
{
}

inline std::string CompilationParameter::name() const {
    return name_;
}

inline std::string CompilationParameter::pathToNetwork() const {
    return path_to_network_;
}

inline std::string CompilationParameter::pathToWeights() const {
    return path_to_weights_;
}

std::vector<CompilationParameter> compilation_parameters_kmb =
{
    CompilationParameter{"mobilenet_v2_1.0_224_quant_frozen_69.62",
                       "/KMB_models/INT8/mobilenet_v2_1.0_224_quant_frozen_69.62/mobilenet_v2_1.xml",
                       "/KMB_models/INT8/mobilenet_v2_1.0_224_quant_frozen_69.62/mobilenet_v2_1.bin"},
    CompilationParameter{"inception_v1_224_quant_frozen_69.8",
                       "/KMB_models/INT8/inception_v1_224_quant_frozen_69.8/inception_v1_224_quant_frozen_no_preprocess.xml",
                       "/KMB_models/INT8/inception_v1_224_quant_frozen_69.8/inception_v1_224_quant_frozen_no_preprocess.bin"},
    CompilationParameter{"inception_v3_quant_frozen_77.64",
                       "/KMB_models/INT8/inception_v3_quant_frozen_77.64/inception_v3_quant_frozen_no_preprocess.xml",
                       "/KMB_models/INT8/inception_v3_quant_frozen_77.64/inception_v3_quant_frozen_no_preprocess.bin"},
    CompilationParameter{"resnet50_int8_68.04",
                        "/KMB_models/INT8/resnet50_int8_68.04/resnet50_int8_no_preprocess.xml",
                        "/KMB_models/INT8/resnet50_int8_68.04/resnet50_int8_no_preprocess.bin"},
    CompilationParameter{"resnet_v1_50_75.19",
                         "/KMB_models/FP16/resnet_v1_50_75.19/resnet50_v1_fp16.xml",
                         "/KMB_models/FP16/resnet_v1_50_75.19/resnet50_v1_fp16.bin"},
    CompilationParameter{"mobilenet_v2_1.0_224_frozen_71.74",
                         "/KMB_models/FP16/mobilenet_v2_1.0_224_frozen_71.74/mobilenet_v2_1_no_preprocess.xml",
                         "/KMB_models/FP16/mobilenet_v2_1.0_224_frozen_71.74/mobilenet_v2_1_no_preprocess.bin"},
    CompilationParameter{"inception_v3_74.19",
                         "/KMB_models/FP16/inception_v3_74.19/inception_v3_no_preprocess.xml",
                         "/KMB_models/FP16/inception_v3_74.19/inception_v3_no_preprocess.bin"},
};

}

inline void VpuNoRegressionWithCompilation::loadNetworkWrapper(std::map<std::string, std::string> config) {
    StatusCode sts;
    InferenceEngine::ResponseDesc response;
    HeteroPluginPtr plugin(make_plugin_name(pluginName));
    CNNNetReader reader;
    reader.ReadNetwork((ModelsPath() + path_to_files.pathToNetwork()).c_str());
    reader.ReadWeights((ModelsPath() + path_to_files.pathToWeights()).c_str());
    CNNNetwork network = reader.getNetwork();

    IExecutableNetwork::Ptr exeNetwork;

    // Try to get statistics
    ICNNNetworkStats* pstats = nullptr;
    StatusCode s = ((ICNNNetwork&)network).getStats(&pstats, nullptr);

    if (s == StatusCode::OK && pstats && !pstats->isEmpty()) {
        details::CNNNetworkImplPtr clonedNetwork;
        CNNNetworkInt8Normalizer cnnorm;

        config[VPU_CONFIG_KEY(ALLOW_FP32_MODELS)] = CONFIG_VALUE(YES);
        clonedNetwork = cloneNet(network);
        cnnorm.NormalizeNetwork(*clonedNetwork, *pstats);
        sts = plugin->LoadNetwork(exeNetwork, *clonedNetwork, config, &response);
    } else {
        sts = plugin->LoadNetwork(exeNetwork, network, config, &response);
    }

    ASSERT_EQ(StatusCode::OK, sts) << response.msg;
}

TEST_P(KmbNoRegressionCompilationOnly, KmbParsingTest) {
    _config[VPU_KMB_CONFIG_KEY(MCM_PARSING_ONLY)] = CONFIG_VALUE(YES);

    loadNetworkWrapper(_config);
}

TEST_P(KmbNoRegressionCompilationOnly, DISABLED_KmbNetworkCompileTest) {
    std::map<std::string, std::string> config(_config);

    config[VPU_KMB_CONFIG_KEY(MCM_GENERATE_JSON)] = CONFIG_VALUE(YES);
    config[VPU_KMB_CONFIG_KEY(MCM_GENERATE_DOT)]  = CONFIG_VALUE(YES);
    config[VPU_KMB_CONFIG_KEY(MCM_PARSING_ONLY)]  = CONFIG_VALUE(NO);
    const ::testing::TestInfo* const test_info =
            ::testing::UnitTest::GetInstance()->current_test_info();
    config[VPU_KMB_CONFIG_KEY(MCM_COMPILATION_RESULTS_PATH)] = test_info->test_case_name();
    config[VPU_KMB_CONFIG_KEY(MCM_COMPILATION_RESULTS)] = test_info->name();
    std::string& tmpPath = config[VPU_KMB_CONFIG_KEY(MCM_COMPILATION_RESULTS_PATH)];
    std::replace(tmpPath.begin(), tmpPath.end(), '/', '_');
    std::string& tmpName = config[VPU_KMB_CONFIG_KEY(MCM_COMPILATION_RESULTS)];
    std::replace(tmpName.begin(), tmpName.end(), '/', '_');

    loadNetworkWrapper(config);
}

INSTANTIATE_TEST_CASE_P(
        KmbParsingTest_smoke_nightly,
        KmbNoRegressionCompilationOnly,
        Combine(Values("kmbPlugin"),
                ValuesIn(compilation_parameters_kmb)),
                KmbNoRegressionCompilationOnly::getTestCaseName);
