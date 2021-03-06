// Copyright (C) 2018-2020 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <ie_common.h>

#include <pugixml.hpp>
#include "vpux/utils/core/enums.hpp"

namespace vpu {

namespace ie = InferenceEngine;

enum class CustomParamType : int32_t { Input, Output, Data, LocalData, InputBuffer, OutputBuffer, Int, Float };

enum class CustomDataFormat : int32_t {
    BYXF = 0,  // NHWC used in most software layers
    BFYX = 1,  // NCHW used if HW module is enabled
    YXF = 2,   // HWC used in most software layers
    FYX = 3,   // CHW used if HW module is enabled
    BF = 4,    // NC layout
    Any = 5    // doesn't really matter
};

enum class CustomDimSource : int32_t { Input, Output };

class CustomKernelCpp;
class CustomKernelOcl;

class CustomKernelVisitor {
public:
    virtual void visitCpp(const CustomKernelCpp& kernel) = 0;
    virtual void visitCL(const CustomKernelOcl& kernel) = 0;
};

class CustomKernel {
public:
    using Ptr = std::shared_ptr<CustomKernel>;

    struct BindingParameter final {
        CustomParamType type = CustomParamType::Input;
        CustomDataFormat format = CustomDataFormat::Any;
        std::string argName;
        int portIndex = -1;
        std::string irSource;
        std::string bufferSizeRule;
        CustomDimSource dimSource = CustomDimSource::Input;
        int dimIdx = -1;
    };

protected:
    std::vector<uint8_t> _kernelBinary;
    std::vector<BindingParameter> _kernelBindings;

    CustomDimSource _wgDimSource = CustomDimSource::Input;
    int _wgDimIdx = -1;

    int _maxShaves = 0;
    int _inputDataCount = 0;

public:
    const std::vector<uint8_t>& kernelBinary() const {
        return _kernelBinary;
    }
    const std::vector<BindingParameter>& bindings() const {
        return _kernelBindings;
    }

    CustomDimSource dimSource() const {
        return _wgDimSource;
    }
    int dimSourceIndex() const {
        return _wgDimIdx;
    }

    int maxShaves() const {
        return _maxShaves;
    }
    int inputDataCount() const {
        return _inputDataCount;
    }

    virtual void accept(CustomKernelVisitor& validator) const = 0;

protected:
    std::vector<uint8_t> loadKernelBinary(const pugi::xml_node& node, const std::string& configDir);
    std::vector<CustomKernel::BindingParameter> processParametersNode(const pugi::xml_node& node);
    std::pair<CustomDimSource, int> parseDimSource(const std::string& dims);
    CustomDataFormat formatFromString(const std::string& str);
    std::vector<std::string> parseSizeRule(const std::string& size);
};

class CustomKernelCpp final : public CustomKernel {
public:
    explicit CustomKernelCpp(const pugi::xml_node& node, const std::string& configDir);

    void accept(CustomKernelVisitor& validator) const override;

protected:
    void processWorkSizesNode(const pugi::xml_node& node);
};

class CustomKernelOcl final : public CustomKernel {
private:
    std::vector<std::string> _globalGridSizeRules;
    std::vector<std::string> _localGridSizeRules;
    int _kernelId = 0;

public:
    explicit CustomKernelOcl(const pugi::xml_node& node, const std::string& configDir);

    void accept(CustomKernelVisitor& validator) const override;

    const std::vector<std::string>& globalGridSizeRules() const {
        return _globalGridSizeRules;
    }
    const std::vector<std::string>& localGridSizeRules() const {
        return _localGridSizeRules;
    }
    int kernelId() const {
        return _kernelId;
    }

private:
    void processWorkSizesNode(const pugi::xml_node& node);
};

}  // namespace vpu
