// RUN: vpux-opt --split-input-file --convert-precision-to-i32 --canonicalize %s | FileCheck %s

// CHECK-LABEL: @GatherConvertIndices
func @GatherConvertIndices(%arg0: tensor<100xf16>) -> tensor<10xf16> {
  %0 = const.Declare tensor<10xsi64> = #const.Content<dense<1> : tensor<10xsi64>>

  %prob = IE.Gather(%arg0, %0) {axis_value = 0, batch_dims = 0} : tensor<100xf16>,tensor<10xsi64> -> tensor<10xf16>

  return %prob : tensor<10xf16>

  //CHECK: [[VAL0:%.*]] = const.Declare tensor<10xsi32> = #const.Content<dense<1> : tensor<10xsi64>, [#const.ConvertElemType<si32>]>
  //CHECK: [[VAL1:%.*]] = IE.Gather(%arg0, [[VAL0]]) {axis_value = 0 : i64, batch_dims = 0 : i64} : tensor<100xf16>, tensor<10xsi32> -> tensor<10xf16>
  //CHECK: return [[VAL1]]
}
