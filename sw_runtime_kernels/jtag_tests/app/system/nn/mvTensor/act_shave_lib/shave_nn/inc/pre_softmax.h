/*
* {% copyright %}
*/
#pragma once

#include "sw_layer.h"
#include "param_passthrough.h"

namespace nn {
namespace shave_lib {

extern "C" {
preambleImpl preSoftmax;
preambleImpl preSingleSoftmax;
}

} // namespace shave_lib
} // namespace nn
