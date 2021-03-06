//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

#include <moviVectorTypes.h>
#include <math.h>
#include <param_vau_dp4.h>

using namespace sw_params;

extern "C"
void vau_dp4(const struct VauDp4Params *lParams) {
    const struct MemRefData* inputs = lParams->tensors + 0;
    const struct MemRefData* outputs = lParams->tensors + VauDp4Params::NumInputs;

    const int32_t *dims = (int32_t*)(outputs[0].dimsAddr);

    int32_t nElements = 1;

    for (int32_t i = 0; i < outputs[0].numDims; ++i) {
        // TODO: check overflow
        nElements *= dims[i];
    }

    // NOTE: test must align tensor size according to vector size
    nElements = nElements / 4;

    schar16* in1 = (schar16*)(inputs[0].dataAddr);
    schar16* in2 = (schar16*)(inputs[1].dataAddr);
    int4* out = (int4*)(outputs[0].dataAddr);

    for (int32_t e = 0; e < nElements; ++e) {
        schar16 a = *in1++;
        schar16 b = *in2++;
        *out++ = __builtin_shave_vau_dp4_v16i8_rr(a, b);
    }
}
