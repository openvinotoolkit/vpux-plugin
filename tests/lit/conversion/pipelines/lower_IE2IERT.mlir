// RUN: vpux-opt --split-input-file --set-compile-params="vpu-arch=MA2490" --lower-IE-to-IERT %s | FileCheck %s

//
// The 'lower-IE-to-IERT' pass:
//
//   * Fully replaces IE Dialect with IERT Dielect.
//   * Changes all Values types from `tensor` to `memref`.
//   * Changes Function results tensors to arguments.
//   * Inserts `linalg.copy` for `IERT.Constant` as result case.
//   * Inserts `memref.dealloc` Operations for inner buffers.
//

// CHECK: func @SingleLayer([[ARG0:%.*]]: memref<1x1000xf16>, [[ARG1:%.*]]: memref<1x1000xf16>) {
func @SingleLayer(%arg0: tensor<1x1000xf16>) -> tensor<1x1000xf16> {
    %0 = IE.SoftMax(%arg0) {axisInd = 1 : i32} : tensor<1x1000xf16> -> tensor<1x1000xf16>
    return %0 : tensor<1x1000xf16>

    // CHECK: IERT.SoftMax([[ARG0]], [[ARG1]]) {axisInd = 1 : i32} : memref<1x1000xf16>, memref<1x1000xf16>
    // CHECK: return
}

// -----

// CHECK: func @ConstantLayer([[ARG0:%.*]]: memref<1x2x2x2xf16>) {
func @ConstantLayer() -> tensor<1x2x2x2xf16> {
    %0 = IE.Constant tensor<1x2x2x2xf16> = dense<1.0> : tensor<1x2x2x2xf32>
    return %0 : tensor<1x2x2x2xf16>

    // CHECK: [[VAR0:%.*]] = IERT.Constant memref<1x2x2x2xf16> = dense<1.000000e+00> : tensor<1x2x2x2xf32>
    // CHECK: linalg.copy([[VAR0]], [[ARG0]]) : memref<1x2x2x2xf16>, memref<1x2x2x2xf16>
    // CHECK: return
}

// -----

#map0 = affine_map<(d0, d1, d2, d3) -> (d0)>
#map1 = affine_map<(d0, d1, d2, d3) -> (d1, d2, d3)>

// CHECK: func @Reshape([[ARG0:%.*]]: memref<1x512x1x1xf32>, [[ARG1:%.*]]: memref<1x512xf32>) {
func @Reshape(%arg0 : tensor<1x512x1x1xf32>) -> tensor<1x512xf32> {
    %0 = linalg.tensor_reshape %arg0 [#map0, #map1] : tensor<1x512x1x1xf32> into tensor<1x512xf32>
    return %0 : tensor<1x512xf32>

    // CHECK: [[VAR0:%.*]] = linalg.reshape [[ARG0]] [#map0, #map1] : memref<1x512x1x1xf32> into memref<1x512xf32>
    // CHECK: linalg.copy([[VAR0]], [[ARG1]]) : memref<1x512xf32>, memref<1x512xf32>
    // CHECK: return
}

// -----

#map0 = affine_map<(d0, d1, d2, d3) -> (d0)>
#map1 = affine_map<(d0, d1, d2, d3) -> (d1, d2, d3)>

// CHECK: func @ReshapeInGraph([[ARG0:%.*]]: memref<1x512x1x1xf32>, [[ARG1:%.*]]: memref<1x512x1x1xf32>) {
func @ReshapeInGraph(%arg0 : tensor<1x512x1x1xf32>) -> tensor<1x512x1x1xf32> {
    %0 = linalg.tensor_reshape %arg0 [#map0, #map1] : tensor<1x512x1x1xf32> into tensor<1x512xf32>
    %1 = IE.SoftMax(%0) {axisInd = 1 : i32} : tensor<1x512xf32> -> tensor<1x512xf32>
    %2 = linalg.tensor_reshape %1 [#map0, #map1] : tensor<1x512xf32> into tensor<1x512x1x1xf32>
    return %2 : tensor<1x512x1x1xf32>

    // CHECK: [[VAR0:%.*]] = linalg.reshape [[ARG0]] [#map0, #map1] : memref<1x512x1x1xf32> into memref<1x512xf32>
    // CHECK: [[VAR1:%.*]] = memref.alloc() : memref<1x512xf32>
    // CHECK: IERT.SoftMax([[VAR0]], [[VAR1]]) {axisInd = 1 : i32} : memref<1x512xf32>, memref<1x512xf32>
    // CHECK: [[VAR2:%.*]] = linalg.reshape [[VAR1]] [#map0, #map1] : memref<1x512xf32> into memref<1x512x1x1xf32>
    // CHECK: linalg.copy([[VAR2]], [[ARG1]]) : memref<1x512x1x1xf32>, memref<1x512x1x1xf32>
    // CHECK: memref.dealloc [[VAR1]] : memref<1x512xf32>
    // CHECK: return
}
