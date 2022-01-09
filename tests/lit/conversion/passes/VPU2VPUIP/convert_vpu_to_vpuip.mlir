// RUN: vpux-opt --split-input-file --init-compiler="vpu-arch=KMB compilation-mode=DefaultHW" --convert-vpu-to-vpuip --canonicalize %s | FileCheck %s

#NHWC = affine_map<(d0, d1, d2, d3) -> (d0, d2, d3, d1)>

// CHECK-LABEL: @Conv
func @Conv(%arg0: memref<1x16x16x16xf16, #NHWC, @CMX_NN>, %arg1: memref<16x16x1x1xf16, #NHWC, @CMX_NN>)
        -> memref<1x16x16x16xf16, #NHWC, @CMX_NN> {
    %0 = builtin.unrealized_conversion_cast %arg0 : memref<1x16x16x16xf16, #NHWC, @CMX_NN>
        to tensor<1x16x16x16xf16, {mem_space = @CMX_NN, order = #NHWC}>
    %1 = builtin.unrealized_conversion_cast %arg1 : memref<16x16x1x1xf16, #NHWC, @CMX_NN>
        to tensor<16x16x1x1xf16, {mem_space = @CMX_NN, order = #NHWC}>

    %2 = VPU.NCE.Convolution(%0, %1) {
                pad = {bottom = 0, left = 0, right = 0, top = 0},
                ppe = {clamp_high = 2147483647, clamp_low = 0, lrelu_mult = 1, lrelu_shift = 0, mode = "LRELU"},
                strides = [1, 1]
            } -> tensor<1x16x16x16xf16, {mem_space = @CMX_NN, order = #NHWC}> {
        VPU.DPU.Workload [0, 0, 0, 0] [1, 16, 3, 16] {bottom = 0, left = 0, right = 0, top = 0} "VECTOR_FP16"
        VPU.DPU.Workload [0, 0, 3, 0] [1, 16, 3, 16] {bottom = 0, left = 0, right = 0, top = 0} "VECTOR_FP16"
        VPU.DPU.Workload [0, 0, 6, 0] [1, 16, 3, 16] {bottom = 0, left = 0, right = 0, top = 0} "VECTOR_FP16"
        VPU.DPU.Workload [0, 0, 9, 0] [1, 16, 3, 16] {bottom = 0, left = 0, right = 0, top = 0} "VECTOR_FP16"
        VPU.DPU.Workload [0, 0, 12, 0] [1, 16, 4, 16] {bottom = 0, left = 0, right = 0, top = 0} "VECTOR_FP16"
    }

    %3 = builtin.unrealized_conversion_cast %2 : tensor<1x16x16x16xf16, {mem_space = @CMX_NN, order = #NHWC}>
        to memref<1x16x16x16xf16, #NHWC, @CMX_NN>

    return %3 : memref<1x16x16x16xf16, #NHWC, @CMX_NN>
}

// CHECK:       [[OUT_BUF:%.+]] = memref.alloc() : memref<1x16x16x16xf16, #NHWC, @CMX_NN>

// CHECK:       [[WT_CST:%.+]] = VPUIP.WeightsTableOp
// CHECK-SAME:      op_input(%arg0 : memref<1x16x16x16xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      op_output([[OUT_BUF]] : memref<1x16x16x16xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      weights(%arg1 : memref<16x16x1x1xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      -> memref<16x1x1x4xsi32>
// CHECK:       [[WT_BUF:%.+]] = memref.alloc() : memref<16x1x1x4xsi32, @CMX_NN>
// CHECK:       [[WT:%.+]] = IERT.Copy inputs([[WT_CST]] : memref<16x1x1x4xsi32>)
// CHECK-SAME:      outputs([[WT_BUF]] : memref<16x1x1x4xsi32, @CMX_NN>)

// CHECK:       VPUIP.NCEClusterTask
// CHECK-SAME:          kernel_padding = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          kernel_size = [1, 1],
// CHECK-SAME:          kernel_strides = [1, 1],
// CHECK-SAME:          task_type = "CONV"
// CHECK-SAME:      input(%arg0 : memref<1x16x16x16xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      weights(%arg1 : memref<16x16x1x1xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      weight_table([[WT]] : memref<16x1x1x4xsi32, @CMX_NN>)
// CHECK-SAME:      parent_input(%arg0 : memref<1x16x16x16xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      parent_output([[OUT_BUF]] : memref<1x16x16x16xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      outputs([[OUT_BUF]] : memref<1x16x16x16xf16, #NHWC, @CMX_NN>)
// CHECK:           DPUTask
// CHECK-SAME:          end = [15, 2, 15],
// CHECK-SAME:          mpe_mode = "VECTOR_FP16",
// CHECK-SAME:          pad = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          start = [0, 0, 0]
// CHECK:           DPUTask
// CHECK-SAME:          end = [15, 5, 15],
// CHECK-SAME:          mpe_mode = "VECTOR_FP16",
// CHECK-SAME:          pad = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          start = [0, 3, 0]
// CHECK:           DPUTask
// CHECK-SAME:          end = [15, 8, 15],
// CHECK-SAME:          mpe_mode = "VECTOR_FP16",
// CHECK-SAME:          pad = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          start = [0, 6, 0]
// CHECK:           DPUTask
// CHECK-SAME:          end = [15, 11, 15],
// CHECK-SAME:          mpe_mode = "VECTOR_FP16",
// CHECK-SAME:          pad = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          start = [0, 9, 0]
// CHECK:           DPUTask
// CHECK-SAME:          end = [15, 15, 15],
// CHECK-SAME:          mpe_mode = "VECTOR_FP16",
// CHECK-SAME:          pad = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          start = [0, 12, 0]
// CHECK:           PPETask "LRELU" {clamp_high = 2147483647 : i64, clamp_low = 0 : i64, lrelu_mult = 1 : i64, lrelu_shift = 0 : i64}


// -----

#NHWC = affine_map<(d0, d1, d2, d3) -> (d0, d2, d3, d1)>

// CHECK-LABEL: @MaxPool
func @MaxPool(%arg0: memref<1x16x1x4xf16, #NHWC, @CMX_NN>) -> memref<1x16x1x4xf16, #NHWC, @CMX_NN> {
    %0 = builtin.unrealized_conversion_cast %arg0 : memref<1x16x1x4xf16, #NHWC, @CMX_NN>
        to tensor<1x16x1x4xf16, {mem_space = @CMX_NN, order = #NHWC}>

    %1 = VPU.NCE.MaxPool(%0) {
                kernel_size = [1, 1],
                pad = {bottom = 0, left = 0, right = 0, top = 0},
                strides = [1, 1]
            } -> tensor<1x16x1x4xf16, {mem_space = @CMX_NN, order = #NHWC}> {
        VPU.DPU.Workload [0, 0, 0, 0] [1, 16, 1, 4] {bottom = 0, left = 0, right = 0, top = 0} "VECTOR_FP16"
    }

    %2 = builtin.unrealized_conversion_cast %1 : tensor<1x16x1x4xf16, {mem_space = @CMX_NN, order = #NHWC}>
        to memref<1x16x1x4xf16, #NHWC, @CMX_NN>

    return %2 : memref<1x16x1x4xf16, #NHWC, @CMX_NN>
}

// CHECK:       [[ACT_WINDOW_CST:%.+]] = const.Declare memref<16x1x1x16xui8>

// CHECK:       [[OUT_BUF:%.+]] = memref.alloc() : memref<1x16x1x4xf16, #NHWC, @CMX_NN>

// CHECK:       [[ACT_WINDOW_BUF:%.+]] = memref.alloc() : memref<16x1x1x16xui8, @CMX_NN>
// CHECK:       [[ACT_WINDOW:%.+]] = IERT.Copy inputs([[ACT_WINDOW_CST]] : memref<16x1x1x16xui8>)
// CHECK-SAME:      outputs([[ACT_WINDOW_BUF]] : memref<16x1x1x16xui8, @CMX_NN>)

// CHECK:       [[WT_CST:%.+]] = VPUIP.WeightsTableOp
// CHECK-SAME:      op_input(%arg0 : memref<1x16x1x4xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      op_output([[OUT_BUF]] : memref<1x16x1x4xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      activation_window([[ACT_WINDOW_BUF]] : memref<16x1x1x16xui8, @CMX_NN>)
// CHECK-SAME:      -> memref<16x1x1x4xsi32>
// CHECK:       [[WT_BUF:%.+]] = memref.alloc() : memref<16x1x1x4xsi32, @CMX_NN>
// CHECK:       [[WT:%.+]] = IERT.Copy inputs([[WT_CST]] : memref<16x1x1x4xsi32>)
// CHECK-SAME:      outputs([[WT_BUF]] : memref<16x1x1x4xsi32, @CMX_NN>)

// CHECK:       VPUIP.NCEClusterTask
// CHECK-SAME:          activation_window_channel_length = 4 : i64,
// CHECK-SAME:          kernel_padding = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          kernel_size = [1, 1],
// CHECK-SAME:          kernel_strides = [1, 1],
// CHECK-SAME:          task_type = "MAXPOOL"
// CHECK-SAME:      input(%arg0 : memref<1x16x1x4xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      weight_table([[WT]] : memref<16x1x1x4xsi32, @CMX_NN>)
// CHECK-SAME:      activation_window([[ACT_WINDOW]] : memref<16x1x1x16xui8, @CMX_NN>)
// CHECK-SAME:      parent_input(%arg0 : memref<1x16x1x4xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      parent_output([[OUT_BUF]] : memref<1x16x1x4xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      outputs([[OUT_BUF]] : memref<1x16x1x4xf16, #NHWC, @CMX_NN>)
// CHECK:           DPUTask
// CHECK-SAME:          end = [3, 0, 15],
// CHECK-SAME:          mpe_mode = "VECTOR_FP16",
// CHECK-SAME:          pad = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          start = [0, 0, 0]

// -----

#NHWC = affine_map<(d0, d1, d2, d3) -> (d0, d2, d3, d1)>

// CHECK-LABEL: @DepthConv
func @DepthConv(%arg0: memref<1x16x40x80xf16, #NHWC, @CMX_NN>, %arg1: memref<16x1x4x8xf16, #NHWC, @CMX_NN>)
        -> memref<1x16x37x73xf16, #NHWC, @CMX_NN> {
    %0 = builtin.unrealized_conversion_cast %arg0 : memref<1x16x40x80xf16, #NHWC, @CMX_NN>
        to tensor<1x16x40x80xf16, {mem_space = @CMX_NN, order = #NHWC}>
    %1 = builtin.unrealized_conversion_cast %arg1 : memref<16x1x4x8xf16, #NHWC, @CMX_NN>
        to tensor<16x1x4x8xf16, {mem_space = @CMX_NN, order = #NHWC}>

    %2 = VPU.NCE.DepthConvolution(%0, %1) {
                pad = {bottom = 0, left = 0, right = 0, top = 0},
                strides = [1, 1]
            } -> tensor<1x16x37x73xf16, {mem_space = @CMX_NN, order = #NHWC}> {
        VPU.DPU.Workload [0, 0, 0, 0] [1, 16, 7, 73] {bottom = 0, left = 0, right = 0, top = 0} "VECTOR_FP16"
        VPU.DPU.Workload [0, 0, 7, 0] [1, 16, 7, 73] {bottom = 0, left = 0, right = 0, top = 0} "VECTOR_FP16"
        VPU.DPU.Workload [0, 0, 14, 0] [1, 16, 7, 73] {bottom = 0, left = 0, right = 0, top = 0} "VECTOR_FP16"
        VPU.DPU.Workload [0, 0, 21, 0] [1, 16, 7, 73] {bottom = 0, left = 0, right = 0, top = 0} "VECTOR_FP16"
        VPU.DPU.Workload [0, 0, 28, 0] [1, 16, 9, 73] {bottom = 0, left = 0, right = 0, top = 0} "VECTOR_FP16"
    }

    %3 = builtin.unrealized_conversion_cast %2 : tensor<1x16x37x73xf16, {mem_space = @CMX_NN, order = #NHWC}>
        to memref<1x16x37x73xf16, #NHWC, @CMX_NN>

    return %3 : memref<1x16x37x73xf16, #NHWC, @CMX_NN>
}

// CHECK:       [[ACT_WINDOW_CST:%.+]] = const.Declare memref<16x1x1x16xui8>

// CHECK:       [[ACT_WINDOW_BUF:%.+]] = memref.alloc() : memref<16x1x1x16xui8, @CMX_NN>
// CHECK:       [[ACT_WINDOW:%.+]] = IERT.Copy inputs([[ACT_WINDOW_CST]] : memref<16x1x1x16xui8>)
// CHECK-SAME:      outputs([[ACT_WINDOW_BUF]] : memref<16x1x1x16xui8, @CMX_NN>)

// CHECK:       [[OUT_BUF:%.+]] = memref.alloc() : memref<1x16x37x73xf16, #NHWC, @CMX_NN>

// CHECK:       [[WT_CST:%.+]] = VPUIP.WeightsTableOp
// CHECK-SAME:      op_input(%arg0 : memref<1x16x40x80xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      op_output([[OUT_BUF]] : memref<1x16x37x73xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      weights(%arg1 : memref<16x1x4x8xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      activation_window([[ACT_WINDOW_BUF]] : memref<16x1x1x16xui8, @CMX_NN>)
// CHECK-SAME:      -> memref<16x1x1x4xsi32>
// CHECK:       [[WT_BUF:%.+]] = memref.alloc() : memref<16x1x1x4xsi32, @CMX_NN>
// CHECK:       [[WT:%.+]] = IERT.Copy inputs([[WT_CST]] : memref<16x1x1x4xsi32>)
// CHECK-SAME:      outputs([[WT_BUF]] : memref<16x1x1x4xsi32, @CMX_NN>)

// CHECK:       VPUIP.NCEClusterTask
// CHECK-SAME:          activation_window_channel_length = 44 : i64,
// CHECK-SAME:          kernel_padding = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          kernel_size = [4, 8],
// CHECK-SAME:          kernel_strides = [1, 1],
// CHECK-SAME:          task_type = "DWCONV"
// CHECK-SAME:      input(%arg0 : memref<1x16x40x80xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      weights(%arg1 : memref<16x1x4x8xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      weight_table([[WT]] : memref<16x1x1x4xsi32, @CMX_NN>)
// CHECK-SAME:      activation_window([[ACT_WINDOW]] : memref<16x1x1x16xui8, @CMX_NN>)
// CHECK-SAME:      parent_input(%arg0 : memref<1x16x40x80xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      parent_output([[OUT_BUF]] : memref<1x16x37x73xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      outputs([[OUT_BUF]] : memref<1x16x37x73xf16, #NHWC, @CMX_NN>)
// CHECK:           DPUTask
// CHECK-SAME:          end = [72, 6, 15],
// CHECK-SAME:          mpe_mode = "VECTOR_FP16",
// CHECK-SAME:          pad = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          start = [0, 0, 0]
// CHECK:           DPUTask
// CHECK-SAME:          end = [72, 13, 15],
// CHECK-SAME:          mpe_mode = "VECTOR_FP16",
// CHECK-SAME:          pad = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          start = [0, 7, 0]
// CHECK:           DPUTask
// CHECK-SAME:          end = [72, 20, 15],
// CHECK-SAME:          mpe_mode = "VECTOR_FP16",
// CHECK-SAME:          pad = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          start = [0, 14, 0]
// CHECK:           DPUTask
// CHECK-SAME:          end = [72, 27, 15],
// CHECK-SAME:          mpe_mode = "VECTOR_FP16",
// CHECK-SAME:          pad = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          start = [0, 21, 0]
// CHECK:           DPUTask
// CHECK-SAME:          end = [72, 36, 15],
// CHECK-SAME:          mpe_mode = "VECTOR_FP16",
// CHECK-SAME:          pad = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          start = [0, 28, 0]

// -----

#NHWC = affine_map<(d0, d1, d2, d3) -> (d0, d2, d3, d1)>

// CHECK-LABEL: @EltwiseAdd
func @EltwiseAdd(%arg0: memref<1x64x28x28xf16, #NHWC, @CMX_NN>, %arg1: memref<1x64x28x28xf16, #NHWC, @CMX_NN>)
        -> memref<1x64x28x28xf16, #NHWC, @CMX_NN> {
    %0 = builtin.unrealized_conversion_cast %arg0 : memref<1x64x28x28xf16, #NHWC, @CMX_NN>
        to tensor<1x64x28x28xf16, {mem_space = @CMX_NN, order = #NHWC}>
    %1 = builtin.unrealized_conversion_cast %arg0 : memref<1x64x28x28xf16, #NHWC, @CMX_NN>
        to tensor<1x64x28x28xf16, {mem_space = @CMX_NN, order = #NHWC}>

    %2 = VPU.NCE.Eltwise(%0, %1) {
                op_type = "ADD",
                ppe = {clamp_high = 2147483647, clamp_low = -2147483648, lrelu_mult = 1, lrelu_shift = 0, mode = "ADD"}
            } -> tensor<1x64x28x28xf16, {mem_space = @CMX_NN, order = #NHWC}> {
        VPU.DPU.Workload [0, 0, 0, 0] [1, 64, 5, 28] {bottom = 0, left = 0, right = 0, top = 0} "VECTOR_FP16"
        VPU.DPU.Workload [0, 0, 5, 0] [1, 64, 5, 28] {bottom = 0, left = 0, right = 0, top = 0} "VECTOR_FP16"
        VPU.DPU.Workload [0, 0, 10, 0] [1, 64, 5, 28] {bottom = 0, left = 0, right = 0, top = 0} "VECTOR_FP16"
        VPU.DPU.Workload [0, 0, 15, 0] [1, 64, 5, 28] {bottom = 0, left = 0, right = 0, top = 0} "VECTOR_FP16"
        VPU.DPU.Workload [0, 0, 20, 0] [1, 64, 8, 28] {bottom = 0, left = 0, right = 0, top = 0} "VECTOR_FP16"
    }

    %3 = builtin.unrealized_conversion_cast %2 : tensor<1x64x28x28xf16, {mem_space = @CMX_NN, order = #NHWC}>
        to memref<1x64x28x28xf16, #NHWC, @CMX_NN>

    return %3 : memref<1x64x28x28xf16, #NHWC, @CMX_NN>
}

// CHECK:       [[ALLOC0:%.+]] = memref.alloc() : memref<1x64x28x28xf16, #NHWC, @CMX_NN>

// CHECK:       VPUIP.NCEClusterTask
// CHECK-SAME:          activation_window_channel_length = 0 : i64,
// CHECK-SAME:          task_type = "ELTWISE"
// CHECK-SAME:      input(%arg0 : memref<1x64x28x28xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      weights(%arg0 : memref<1x64x28x28xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      parent_input(%arg0 : memref<1x64x28x28xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      parent_output([[ALLOC0]] : memref<1x64x28x28xf16, #NHWC, @CMX_NN>)
// CHECK-SAME:      outputs([[ALLOC0]] : memref<1x64x28x28xf16, #NHWC, @CMX_NN>)
// CHECK:           DPUTask
// CHECK-SAME:          end = [27, 4, 63],
// CHECK-SAME:          mpe_mode = "VECTOR_FP16",
// CHECK-SAME:          pad = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          start = [0, 0, 0]
// CHECK:           DPUTask
// CHECK-SAME:          end = [27, 9, 63],
// CHECK-SAME:          mpe_mode = "VECTOR_FP16",
// CHECK-SAME:          pad = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          start = [0, 5, 0]
// CHECK:           DPUTask
// CHECK-SAME:          end = [27, 14, 63],
// CHECK-SAME:          mpe_mode = "VECTOR_FP16",
// CHECK-SAME:          pad = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          start = [0, 10, 0]
// CHECK:           DPUTask
// CHECK-SAME:          end = [27, 19, 63],
// CHECK-SAME:          mpe_mode = "VECTOR_FP16",
// CHECK-SAME:          pad = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          start = [0, 15, 0]
// CHECK:           DPUTask
// CHECK-SAME:          end = [27, 27, 63],
// CHECK-SAME:          mpe_mode = "VECTOR_FP16",
// CHECK-SAME:          pad = {bottom = 0 : i64, left = 0 : i64, right = 0 : i64, top = 0 : i64},
// CHECK-SAME:          start = [0, 20, 0]
// CHECK:           PPETask "ADD" {clamp_high = 2147483647 : i64, clamp_low = -2147483648 : i64, lrelu_mult = 1 : i64, lrelu_shift = 0 : i64}