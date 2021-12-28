// RUN: vpux-opt --split-input-file --convert-sw-layers-to-VPUIP %s | FileCheck %s

#NCHW = affine_map<(d0, d1, d2, d3) -> (d0, d1, d2, d3)>

module @Test attributes {VPU.arch = "MTL", VPU.compilationMode = "ReferenceHW"} {

// CHECK: module @VPU.SW {
// CHECK:   func private @builtin_SoftMax(memref<*xf16>, memref<*xf16>, i64) attributes {VPU.kernel_code = "single_shave_softmax.cpp", VPU.kernel_entry = "singleShaveSoftmax"}
// CHECK: }

func @SingleSWLayer(%arg0: memref<1x1x1x1000xf16>, %arg1: memref<1x1x1x1000xf16>) -> memref<1x1x1x1000xf16> {
    %0 = IERT.SoftMax {axisInd = 3} inputs(%arg0 : memref<1x1x1x1000xf16>) outputs(%arg1 : memref<1x1x1x1000xf16>) -> memref<1x1x1x1000xf16>
    return %0: memref<1x1x1x1000xf16>

// CHECK: [[VAR0:%.*]] = memref.alloc() : memref<1x1x1x1000xf16, @CMX_NN>
// CHECK: [[VAR1:%.*]] = IERT.Copy inputs(%arg0 : memref<1x1x1x1000xf16>) outputs([[VAR0]] : memref<1x1x1x1000xf16, @CMX_NN>) -> memref<1x1x1x1000xf16, @CMX_NN>

// CHECK: [[VAR2:%.*]] = memref.alloc() : memref<1x1x1x1000xf16, @CMX_NN>
// CHECK: [[VAR3:%.*]] = VPUIP.SW.Kernel @VPU.SW::@builtin_SoftMax inputs([[VAR1]] : memref<1x1x1x1000xf16, @CMX_NN>) outputs([[VAR2]] : memref<1x1x1x1000xf16, @CMX_NN>) on tile 0 -> memref<1x1x1x1000xf16, @CMX_NN>  {
// CHECK: ^bb0(%arg2: memref<1x1x1x1000xf16, @CMX_NN>, %arg3: memref<1x1x1x1000xf16, @CMX_NN>):
// CHECK:   %c3_i64 = arith.constant 3 : i64
// CHECK:   VPUIP.SW.Kernel.run(%arg2, %arg3, %c3_i64) : memref<1x1x1x1000xf16, @CMX_NN>, memref<1x1x1x1000xf16, @CMX_NN>, i64
// CHECK: }

// CHECK: [[VAR4:%.*]] = IERT.Copy inputs([[VAR3]] : memref<1x1x1x1000xf16, @CMX_NN>) outputs(%arg1 : memref<1x1x1x1000xf16>) -> memref<1x1x1x1000xf16>
// CHECK: return [[VAR4]] : memref<1x1x1x1000xf16>

}

}

// -----

#NCHW = affine_map<(d0, d1, d2, d3) -> (d0, d1, d2, d3)>

module @Test attributes {VPU.arch = "MTL", VPU.compilationMode = "ReferenceHW"} {

// CHECK: module @VPU.SW  {
// CHECK:     func private @builtin_Sigmoid(memref<*xf16>, memref<*xf16>) attributes {VPU.kernel_code = "sigmoid_fp16.c", VPU.kernel_entry = "sigmoid_fp16"}
// CHECK:     func private @builtin_SoftMax(memref<*xf16>, memref<*xf16>, i64) attributes {VPU.kernel_code = "single_shave_softmax.cpp", VPU.kernel_entry = "singleShaveSoftmax"}
// CHECK:   }

// CHECK: func @ThreeSWLayers(%arg0: memref<1x1x1x2000xf16>, %arg1: memref<1x1x1x1000xf16>) -> memref<1x1x1x1000xf16> {
func @ThreeSWLayers(%arg0: memref<1x1x1x2000xf16>, %arg1: memref<1x1x1x1000xf16>) -> memref<1x1x1x1000xf16> {
    %tmp1 = memref.alloc() : memref<1x1x1x2000xf16>
    %tmp2 = memref.alloc() : memref<1x1x1x2000xf16>

    %0 = IERT.SoftMax {axisInd = 3} inputs(%arg0 : memref<1x1x1x2000xf16>) outputs(%tmp1 : memref<1x1x1x2000xf16>) -> memref<1x1x1x2000xf16>
    %1 = IERT.Sigmoid {axisInd = 3} inputs(%0 : memref<1x1x1x2000xf16>) outputs(%tmp2 : memref<1x1x1x2000xf16>) -> memref<1x1x1x2000xf16>

    %2 = IERT.SubView %1[0, 0, 0, 1000] [1, 1, 1, 1000]
        : memref<1x1x1x2000xf16> to memref<1x1x1x1000xf16, {order = #NCHW, strides = [2000, 2000, 2000, 1]}>

    %3 = IERT.SoftMax {axisInd = 3} inputs(%2 : memref<1x1x1x1000xf16, {order = #NCHW, strides = [2000, 2000, 2000, 1]}>) outputs(%arg1 : memref<1x1x1x1000xf16>) -> memref<1x1x1x1000xf16>

    return %3 : memref<1x1x1x1000xf16>

// CHECK: [[VAR0:%.*]] = memref.alloc() : memref<1x1x1x2000xf16>
// CHECK: [[VAR1:%.*]] = memref.alloc() : memref<1x1x1x2000xf16>

// CHECK: [[VAR2:%.*]] = memref.alloc() : memref<1x1x1x2000xf16, @CMX_NN>
// CHECK: [[VAR3:%.*]] = IERT.Copy inputs(%arg0 : memref<1x1x1x2000xf16>) outputs([[VAR2]] : memref<1x1x1x2000xf16, @CMX_NN>) -> memref<1x1x1x2000xf16, @CMX_NN>
// CHECK: [[VAR4:%.*]] = memref.alloc() : memref<1x1x1x2000xf16, @CMX_NN>
// CHECK: [[VAR5:%.*]] = VPUIP.SW.Kernel @VPU.SW::@builtin_SoftMax inputs([[VAR3]] : memref<1x1x1x2000xf16, @CMX_NN>) outputs([[VAR4]] : memref<1x1x1x2000xf16, @CMX_NN>) on tile 0 -> memref<1x1x1x2000xf16, @CMX_NN>  {
// CHECK:       ^bb0(%arg2: memref<1x1x1x2000xf16, @CMX_NN>, %arg3: memref<1x1x1x2000xf16, @CMX_NN>):  // no predecessors
// CHECK:         %c3_i64 = arith.constant 3 : i64
// CHECK:         VPUIP.SW.Kernel.run(%arg2, %arg3, %c3_i64) : memref<1x1x1x2000xf16, @CMX_NN>, memref<1x1x1x2000xf16, @CMX_NN>, i64
// CHECK:       }
// CHECK: [[VAR6:%.*]] = IERT.Copy inputs([[VAR5]] : memref<1x1x1x2000xf16, @CMX_NN>) outputs([[VAR0]] : memref<1x1x1x2000xf16>) -> memref<1x1x1x2000xf16>

// CHECK: [[VAR7:%.*]] = memref.alloc() : memref<1x1x1x2000xf16, @CMX_NN>
// CHECK: [[VAR8:%.*]] = IERT.Copy inputs([[VAR6]] : memref<1x1x1x2000xf16>) outputs([[VAR7]] : memref<1x1x1x2000xf16, @CMX_NN>) -> memref<1x1x1x2000xf16, @CMX_NN>
// CHECK: [[VAR9:%.*]] = memref.alloc() : memref<1x1x1x2000xf16, @CMX_NN>
// CHECK: [[VAR10:%.*]] = VPUIP.SW.Kernel @VPU.SW::@builtin_Sigmoid inputs([[VAR8]] : memref<1x1x1x2000xf16, @CMX_NN>) outputs([[VAR9]] : memref<1x1x1x2000xf16, @CMX_NN>) on tile 0 -> memref<1x1x1x2000xf16, @CMX_NN>  {
// CHECK:      ^bb0(%arg2: memref<1x1x1x2000xf16, @CMX_NN>, %arg3: memref<1x1x1x2000xf16, @CMX_NN>):  // no predecessors
// CHECK:       VPUIP.SW.Kernel.run(%arg2, %arg3) : memref<1x1x1x2000xf16, @CMX_NN>, memref<1x1x1x2000xf16, @CMX_NN>
// CHECK:  }
// CHECK: [[VAR11:%.*]] = IERT.Copy inputs([[VAR10]] : memref<1x1x1x2000xf16, @CMX_NN>) outputs([[VAR1]] : memref<1x1x1x2000xf16>) -> memref<1x1x1x2000xf16>

// CHECK: [[VAR12:%.*]] = IERT.SubView [[VAR11]] [0, 0, 0, 1000] [1, 1, 1, 1000] : memref<1x1x1x2000xf16> to memref<1x1x1x1000xf16, {order = #NCHW, strides = [2000, 2000, 2000, 1]}>

// CHECK: [[VAR13:%.*]] = memref.alloc() : memref<1x1x1x1000xf16, @CMX_NN>
// CHECK: [[VAR14:%.*]] = IERT.Copy inputs([[VAR12]] : memref<1x1x1x1000xf16, {order = #NCHW, strides = [2000, 2000, 2000, 1]}>) outputs([[VAR13]] : memref<1x1x1x1000xf16, @CMX_NN>) -> memref<1x1x1x1000xf16, @CMX_NN>
// CHECK: [[VAR15:%.*]] = memref.alloc() : memref<1x1x1x1000xf16, @CMX_NN>
// CHECK: [[VAR16:%.*]] = VPUIP.SW.Kernel @VPU.SW::@builtin_SoftMax inputs([[VAR14]] : memref<1x1x1x1000xf16, @CMX_NN>) outputs([[VAR15]] : memref<1x1x1x1000xf16, @CMX_NN>) on tile 0 -> memref<1x1x1x1000xf16, @CMX_NN>  {
// CHECK:  ^bb0(%arg2: memref<1x1x1x1000xf16, @CMX_NN>, %arg3: memref<1x1x1x1000xf16, @CMX_NN>):  // no predecessors
// CHECK:  %c3_i64 = arith.constant 3 : i64
// CHECK:  VPUIP.SW.Kernel.run(%arg2, %arg3, %c3_i64) : memref<1x1x1x1000xf16, @CMX_NN>, memref<1x1x1x1000xf16, @CMX_NN>, i64
// CHECK:  }
// CHECK: [[VAR17:%.*]] = IERT.Copy inputs([[VAR16]] : memref<1x1x1x1000xf16, @CMX_NN>) outputs(%arg1 : memref<1x1x1x1000xf16>) -> memref<1x1x1x1000xf16>

// CHECK:  return [[VAR17]] : memref<1x1x1x1000xf16>

}

}
