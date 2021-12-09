//
// Copyright Intel Corporation.
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

#include "vpux/compiler/movitools/movitools.h"
#include <mlir/Support/DebugStringHelper.h>
#include "vpux/utils/core/error.hpp"

#include <llvm/ADT/SmallString.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4267)  // size_t to integer conversion
#endif

#include <llvm/Support/Path.h>
#include <llvm/Support/Process.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

using namespace llvm;  // NOLINT

namespace vpux {
namespace movitools {

std::string getMoviToolsDir() {
    const auto toolsDir = llvm::sys::Process::GetEnv("MV_TOOLS_DIR");
    VPUX_THROW_UNLESS(toolsDir.hasValue(), "MV_TOOLS_DIR env var must be set");

    const auto toolsVer = llvm::sys::Process::GetEnv("MV_TOOLS_VERSION");
    VPUX_THROW_UNLESS(toolsVer.hasValue(), "MV_TOOLS_VERSION env var must be set");

    SmallString<128> finalToolsDir(toolsDir.getValue());
    sys::path::append(finalToolsDir, toolsVer.getValue());
    VPUX_THROW_UNLESS(sys::fs::is_directory(finalToolsDir), "{0} is not a directory", finalToolsDir.str());

    return finalToolsDir.str().str();
}

}  // namespace movitools
}  // namespace vpux