// RUN: vpux-opt --split-input-file --transpose-reorder-concat-pass --canonicalize %s | FileCheck %s

#NHWC = affine_map<(d0, d1, d2, d3) -> (d0, d2, d3, d1)>
#map = affine_map<(d0, d1, d2, d3) -> (d0, d3, d1, d2)>

// CHECK: func @InsertReorderBeforeConcat(%arg0: tensor<1x8x512x64xf16>, %arg1: tensor<1x2x1x512xf16>) -> tensor<1x64x9x512xf16>

func @InsertReorderBeforeConcat(%arg0: tensor<1x8x512x64xf16>, %arg1: tensor<1x2x1x512xf16>) -> tensor<1x64x9x512xf16> {
    %cst = const.Declare tensor<64x2x1x1xf16> = #const.Content<dense<1.0>
        : tensor<64x2x1x1xf32>, [#const.ConvertElemType<f16>]>

    %0 = IE.Transpose(%arg0) {order_value = #map}
        : tensor<1x8x512x64xf16> -> tensor<1x64x8x512xf16>

    %1 = IE.Convolution(%arg1, %cst) {
        dilations = [1, 1],
        pads_begin = [0, 0],
        pads_end = [0, 0],
        strides = [1, 1]
    } : tensor<1x2x1x512xf16>, tensor<64x2x1x1xf16> -> tensor<1x64x1x512xf16>

    %2 = IE.Concat(%0, %1) {
        static_offsets = [[0, 0, 0, 0], [0, 0, 8, 0]]
    } : tensor<1x64x8x512xf16>, tensor<1x64x1x512xf16> -> tensor<1x64x9x512xf16>

    return %2 : tensor<1x64x9x512xf16>

    // CHECK:   %[[CONSTANT_1:.*]] = const.Declare tensor<64x2x1x1xf16> = #const.Content<dense<1.000000e+00>
    // CHECK-SAME:  : tensor<64x2x1x1xf32>, [#const.ConvertElemType<f16>]>

    // CHECK:   %[[TRANSPOSE:.*]] = IE.Transpose(%arg0) {order_value = #map}
    // CHECK-SAME   : tensor<1x8x512x64xf16> -> tensor<1x64x8x512xf16>

    // CHECK:   %[[CONV2D:.*]] = IE.Convolution(%arg1, %[[CONSTANT_1]]) {
    // CHECK-SAME:      dilations = [1, 1],
    // CHECK-SAME:      pads_begin = [0, 0],
    // CHECK-SAME:      pads_end = [0, 0],
    // CHECK-SAME:      strides = [1, 1]
    // CHECK-SAME:  } : tensor<1x2x1x512xf16>, tensor<64x2x1x1xf16> -> tensor<1x64x1x512xf16>

    // CHECK:   %[[TRANSPOSE_NHWC:.*]] = IE.Reorder(%[[TRANSPOSE]]) {dstOrder = #NHWC}
    // CHECK-SAME:  : tensor<1x64x8x512xf16> -> tensor<1x64x8x512xf16, {order = #NHWC}>
    // CHECK:   %[[CONV2D_NHWC:.*]] = IE.Reorder(%[[CONV2D]]) {dstOrder = #NHWC}
    // CHECK-SAME:  : tensor<1x64x1x512xf16> -> tensor<1x64x1x512xf16, {order = #NHWC}>

    // CHECK:   %[[CONCAT:.*]] = IE.Concat(%[[TRANSPOSE_NHWC]], %[[CONV2D_NHWC]]) {
    // CHECK-SAME{LITERAL}:     static_offsets = [[0, 0, 0, 0], [0, 0, 8, 0]]
    // CHECK-SAME:  } : tensor<1x64x8x512xf16, {order = #NHWC}>, tensor<1x64x1x512xf16, {order = #NHWC}> -> tensor<1x64x9x512xf16, {order = #NHWC}>

    // CHECK:   %[[REORDER:.*]] = IE.Reorder(%[[CONCAT]]) {dstOrder = #NCHW}
    // CHECK-SAME:  : tensor<1x64x9x512xf16, {order = #NHWC}> -> tensor<1x64x9x512xf16>

    // CHECK:   return %[[REORDER]] : tensor<1x64x9x512xf16>
}
