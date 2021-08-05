// RUN: vpux-opt --split-input-file --convert-precision-to-fp16 --canonicalize %s | FileCheck %s

//
// The 'convert-precision-to-fp16' pass:
//
//   * Updates both Function bodies and Function prototypes.
//   * It shouldn't touch user types defined in `IE.CNNNetwork`.
//   * It should update types for `Constant` operation.
//

// CHECK-LABEL: @FP32toFP16
module @FP32toFP16 {

IE.CNNNetwork
    entryPoint : @main
    inputsInfo : {
        // CHECK: IE.DataInfo "data" : tensor<1x1000xf32>
        IE.DataInfo "data" : tensor<1x1000xf32>
    }
    outputsInfo : {
        // CHECK: IE.DataInfo "prob" : tensor<1x1000xf32>
        IE.DataInfo "prob" : tensor<1x1000xf32>
    }

// CHECK: func @main(%arg0: tensor<1x1000xf16>) -> tensor<1x1000xf16>
func @main(%arg0: tensor<1x1000xf32>) -> tensor<1x1000xf32> {
    %prob = IE.SoftMax(%arg0) {axisInd = 1} : tensor<1x1000xf32> -> tensor<1x1000xf32>
    // CHECK:       %[[OUT:.*]] = IE.SoftMax(%arg0)
    // CHECK-SAME:      tensor<1x1000xf16> -> tensor<1x1000xf16>

    return %prob : tensor<1x1000xf32>
    // CHECK: return %[[OUT]] : tensor<1x1000xf16>
}

}

// -----

// CHECK-LABEL: @ConstantLayer
module @ConstantLayer {

IE.CNNNetwork
    entryPoint : @main
    inputsInfo : {
    }
    outputsInfo : {
        // CHECK: IE.DataInfo "output" : tensor<1x2x2x2xf32>
        IE.DataInfo "output" : tensor<1x2x2x2xf32>
    }

// CHECK: func @main() -> tensor<1x2x2x2xf16>
func @main() -> tensor<1x2x2x2xf32> {
    %0 = const.Declare tensor<1x2x2x2xf32> = #const.Content<dense<1.0> : tensor<1x2x2x2xf32>>
    return %0 : tensor<1x2x2x2xf32>

    // CHECK:       %[[OUT:.*]] = const.Declare tensor<1x2x2x2xf16> =
    // CHECK-SAME:      #const.Content<dense<1.000000e+00> : tensor<1x2x2x2xf32>, [#const.ConvertElemType<f16>]>
    // CHECK:       return %[[OUT]] : tensor<1x2x2x2xf16>
}

}
