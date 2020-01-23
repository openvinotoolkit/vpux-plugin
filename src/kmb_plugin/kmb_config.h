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

#pragma once

#include <mcm_config.h>

#include <map>
#include <string>
#include <unordered_set>

namespace vpu {

class KmbConfig final : public MCMConfig {
public:
    bool useKmbExecutor() const { return _useKmbExecutor; }

    bool loadNetworkAfterCompilation() const { return _loadNetworkAfterCompilation; }

    int throghputStreams() const { return _throghputStreams; }

    const std::string& platform() const { return _platform; }

    int numberOfSIPPShaves() const { return _numberOfSIPPShaves; }

    int SIPPLpi() const { return _SIPPLpi; }

protected:
    const std::unordered_set<std::string>& getCompileOptions() const override;
    const std::unordered_set<std::string>& getRunTimeOptions() const override;
    void parse(const std::map<std::string, std::string>& config) override;

private:
#ifdef ENABLE_VPUAL
    bool _useKmbExecutor = true;
#else
    bool _useKmbExecutor = false;
#endif

    bool _loadNetworkAfterCompilation = false;
    int _throghputStreams = 1;

    std::string _platform = "VPU_2490";

    int _numberOfSIPPShaves = 4;
    int _SIPPLpi = 8;
};

}  // namespace vpu
