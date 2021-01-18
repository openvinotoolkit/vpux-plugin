// RUN: vpux-opt --split-input-file --canonicalize %s | FileCheck %s

// CHECK-LABEL: @ConstFold
func @ConstFold() -> tensor<1x16xf16> {
    %0 = IE.Constant tensor<1x16xf32> = dense<1.0> : tensor<16xf32>
    %1 = IE.Convert(%0) { dstType = f16 } : tensor<1x16xf32> -> tensor<1x16xf16>
    return %1 : tensor<1x16xf16>

    // CHECK:       %[[VAL0:.*]] = IE.Constant tensor<1x16xf16> = dense<1.000000e+00> : tensor<16xf32>
    // CHECK-NOT:   IE.Convert
    // CHECK:       return %[[VAL0]]
}
