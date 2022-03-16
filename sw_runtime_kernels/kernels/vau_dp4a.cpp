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

#include <moviVectorTypes.h>
#include <math.h>
#include <param_vau_dp4a.h>

using namespace sw_params;

extern "C"
void vau_dp4a(const struct VauDp4aParams *lParams) {
    const struct MemRefData* inputs = lParams->tensors + 0;
    const struct MemRefData* outputs = lParams->tensors + VauDp4aParams::NumInputs;

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

    if (nElements > 0) {
        // zero the accumulator before MAC
        schar16 a = *in1++;
        schar16 b = *in2++;
        *out++ = __builtin_shave_vau_dp4az_v16i8_vacc0_rr(a, b);
    }
    for (int32_t e = 1; e < nElements; ++e) {
        // retain previous accumulator value
        schar16 a = *in1++;
        schar16 b = *in2++;
        *out++ = __builtin_shave_vau_dp4a_v16i8_vacc0_rr(a, b);
    }
}