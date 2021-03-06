//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#ifndef TARGET_DESCRIPTOR_HPP_
#define TARGET_DESCRIPTOR_HPP_

#include <vector>
#include <string>
#include <set>
#include <fstream>
#include <algorithm>
#include "include/mcm/base/json/json.hpp"
#include "include/mcm/utils/parser/json_text.hpp"
#include "include/mcm/base/json/string.hpp"
#include "include/mcm/base/printable.hpp"
#include "include/mcm/tensor/order/order.hpp"
#include "include/mcm/tensor/dtype/dtype.hpp"
#include "include/mcm/logger/log_sender.hpp"
#include "include/mcm/computation/op/op_registry.hpp"
#include "include/mcm/base/element.hpp"
#include "include/mcm/target/kmb/custom_pwl_table.hpp"

namespace mv
{
    struct DPUMode { unsigned H, W; };
    using  DPUModeList = std::vector<mv::DPUMode>;

    enum class Target
    {
        ma2490,
        ma3100,
        ma3720,
        Unknown
    };

    enum class CodecType
    {
        HDE,
        BTC,
        Unknown
    };

    struct WorkloadConfig
    {
        std::vector<std::size_t> validZTiles;
        std::vector<std::string> algorithms;
        DPUModeList dpuModes;
    };

    struct CodecDescriptor
    {
        CodecType codecName;

        CodecDescriptor() {}
        virtual ~CodecDescriptor() {}
    };

    struct HdeDescriptor : public CodecDescriptor
    {
        std::size_t numberOfHDEModules;
        std::size_t bitPerSymbol;
        std::size_t blockSize;
        std::size_t maxNumberEncodedSymbols;
        bool bypassMode;
    };

    struct GeneralTargetConfigs
    {
        bool floatScaleTable;
        bool allowMultipleInputScales;
        size_t leakyAccuracyBits;
        bool pwlEnabled;
        PWLTableMap pwlTables;
    };

    struct BTCDescriptor : public CodecDescriptor
    {
        std::size_t numCompressionModules;
        std::size_t bufferAlignment;
        std::size_t bitmapPreprocEnable;
        bool bypassMode;
    };

    typedef std::vector<std::pair<std::string, std::string>> DataTypeSet;

    struct DataTypeSupport
    {
        DataTypeSet failCase;
        DataTypeSet mitigation;
    };

    class TargetDescriptor : public LogSender
    {

        struct MemoryDescriptor
        {

            long long size;
            std::size_t alignment;
        };

        struct NceDescriptor
        {
            std::size_t totalNumber;
        };

        static Target toTarget(const std::string& str);
        const static unsigned jsonParserBufferLenght_ = 128;

        Target target_;
        DType globalDType_;
        std::map<std::string, MemoryDescriptor> memoryDefs_;
        std::shared_ptr<CodecDescriptor> codecDef_;
        std::map<std::string, NceDescriptor> nceDefs_;
        std::map<std::string, std::size_t> processorDefs_;
        std::map<std::string, WorkloadConfig> workloadConfigs_;
        std::vector<DataTypeSupport> dtypeSupport_;
        GeneralTargetConfigs generalConfigs_;

    public:

        TargetDescriptor(const std::string& filePath = "");
        bool load(const std::string& filePath);
        bool save(const std::string& filePath);
        void reset();

        void setTarget(Target target);
        void setDType(DType dType);

        bool defineMemory(const std::string& name, long long size, std::size_t alignment);
        bool undefineMemory(const std::string& name);

        Target getTarget() const;
        DType getDType() const;
        CodecType getCodecName() const;

        mv::Element getSerialDefinition(std::string op_name, std::string platform_name) const;
        const GeneralTargetConfigs& generalTargetConfigs() const;
        const std::map<std::string, MemoryDescriptor>& memoryDefs() const;
        const std::map<std::string, NceDescriptor>& nceDefs() const;
        const std::map<std::string, std::size_t>& processorDefs() const;
        const std::shared_ptr<CodecDescriptor>& codecDef() const;
        const std::map<std::string, WorkloadConfig> & getWorkloadConfigs() const;
        const std::vector<mv::DataTypeSupport> & dtypeSupport() const;

        std::string getLogID() const override;

        static std::string toString(Target target);

    };

}

#endif // TARGET_DESCRIPTOR_HPP_
