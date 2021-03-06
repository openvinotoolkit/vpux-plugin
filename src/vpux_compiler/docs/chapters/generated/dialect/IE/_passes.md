<!-- Autogenerated by mlir-tblgen; don't manually edit -->
### `-adjust-layouts`: Adjust required layouts for all layers
The pass is a part of `IECommon` pipeline.

This pass adds the required layouts instead of the default one
depending on the layer specification from underlying Dialect.
### `-convert-avg-pool-to-dw-conv`: Convert AvgPool op to GroupConvolution op
The pass is a part of `AdjustForVPU` pipeline.

This pass replaces suitable `AvgPool` operations with `GroupConvolution` operation.
### `-convert-conv1d-to-conv2d`: Convert Convolution1D and GroupConvolution1D to its 2D variance
The pass is a part of `AdjustForVPU` pipeline.

Extends input, filter and output tensors with height = 1.
[N, C, W] -> [N, C, 1, W]
strides:    {2} -> strides:    {1, 2}
pads_begin: {2} -> pads_begin: {0, 2}
pads_end:   {2} -> pads_end:   {0, 2}
dilations:  {2} -> dilations:  {1, 2}
### `-convert-deconv-to-conv`: Convert Deconvolution 2D to Convolution 2D
The pass is a part of `AdjustForVPU` pipeline.

Replaces deconvolution by upsampling and convolution
### `-convert-depthToSpace`: Convert DepthToSpace layer to {reshape -> transpose -> reshape} subgraph
The pass is a part of `AdjustForVPU` pipeline.

This pass replaces all `DepthToSpace` operations with {reshape -> transpose -> reshape} subgraph.
### `-convert-fc-to-conv`: Convert FullyConnected op to Convolution operation
The pass is a part of `AdjustForVPU` pipeline.

This pass replaces all `FullyConnected` operations with `Convolution` operation.
It inserts extra `Reshape` operations to satisfy `Convolution` specification.
### `-convert-nearest-to-strided-concat`: Convert nearest interpolate op to strided concat ops
The pass is a part of `AdjustForVPU` pipeline.

This pass replaces `Nearest Interpolate` operations with `Concat` operations with strides.
### `-convert-pad-to-concat`: Convert Pad Ops to Concat with Constant
The pass is a part of `AdjustForVPU` pipeline.

After FusePadOps pass, there are Pad Ops can not be fused.
Replace `IE::PadOp` with `IE::ConcatOp` and `Const::DeclareOp`
Only `IE::PadMode::CONSTANT` case is supported.
### `-convert-paddings-to-floor-mode`: Convert Convolution and Pooling layers paddings to FLOOR rouding mode
The pass is a part of `AdjustForVPU` pipeline.

This pass updates padding attributes for Convolution and Pooling layers.
It switches layer rounding mode to FLOOR and updates paddings to satisfy output shape.
### `-convert-precision-to-fp16`: Convert tensors precision from FP32 to FP16
The pass is a part of `AdjustForVPU` pipeline.

This pass replaces all FP32 tensors with FP16.
It updates both function bodies as well as Function signatures.
### `-convert-precision-to-i32`: Convert tensors precision from I64 to I32
The pass is a part of `AdjustForVPU` pipeline.
This pass replaces all I64 tensors with I32.
It updates both function bodies as well as Function signatures.
### `-convert-quantize-ops-to-eltwise`: Converts per-tensor Quantize/Dequantize to eltwise And mixed-precision operation
The pass is a part of `LowPrecision` pipeline.

Converts per-tensor Quantize/Dequantize to eltwise And mixed-precision operation
where input2 is input1 to perform type conversion on DPU instead of UPA.
### `-convert-reduce-to-pooling`: Convert reduce to pooling ops
The pass is to convert reduce operations (mean, max, sum, min) into pooling.
### `-convert-scale-shift-depthwise`: Convert Scale-Shift operation to Depthwise Convolution
The pass is a part of `HardwareMode` pipeline.

Convert Scale-Shift operation to Depthwise convolution.
### `-convert-shape-to-4d`: Convert tensors shapes to 4D
The pass is a part of `AdjustForVPU` pipeline.

This pass replaces ND tensor with 4D analogues for layers, which has such limitations on VPUIP level.
Also this pass replaces ND network inputs and outputs with 4D analogues to overcome runtime limitations.
### `-convert-shuffle-channels`: Convert ShuffleChannels to Reshape->Transpose->Reshape
The pass is a part of `AdjustForVPU` pipeline.
Converts ShuffleChannels to Reshape->Transpose->Reshape.
### `-convert-tile-to-per-axis-tiles`: Convert tile op by multiple axes to multiple PerAxisTile operations
The pass is a part of `AdjustForVPU` pipeline.

This pass replaces all `Tile` op with a set of `PerAxisTile` operations.
### `-convert-to-mem-permute`: Convert Reorder and Transpose ops to MemPermute operation
The pass is a part of `AdjustForVPU` pipeline.

This pass replaces all `Reorder` and `Transpose` operations with `MemPermute` operation.
### `-convert-weights-to-u8`: Shift data from a signed range to an unsigned one
The pass is a part of `LowPrecision` pipeline.

Pass detects quantized convolution and shifts weights data from a signed range to an unsigned one
### `-delete-peraxis-quantization`: Delete PerAxis Quantize Dequantize for VPUX37XX
The pass is a part of `LowPrecision` pipeline.

It deletes per axis quantization which left after LPT.
Conversion is not mathimatically equal, but for now it gives small
    accuracy deviation
### `-dequantize-const`: Dequantize constant tensors
The pass is a part of `LowPrecision` pipeline.

It performs constant folding for `Constant -> quant.dcast` case.
The pass is used as a fallback to FP16 computations for the cases, where quantized types where not used by layers.
### `-expand-activation-channels`: Allign input tensors shape of DPU operation with hardware requirements
The pass is a part of `buildHardwareModePipeline` pipeline.

This pass processes operations, which can be compile as a DPU tasks and
    expands channels number to number divisible by 16 in case they doesn't satisfy hardware requirements
### `-fuse-convert-with-quantize`: Fuse Convert with Quantize into QuantCast operation
Pass detects pattern Convert(i8/ui8 -> FP16) -> Quantize(FP16 -> !quant.uniform<...>)
and fuses it into single QuantCast(i8/ui8 -> !quant.uniform<...>) operation.
### `-fuse-pad-ops`: Fuse PadOp with CONSTANT model
The pass is a part of `AdjustForVPU` pipeline.

PadOp with CONSTANT model, pad value is 0 and the padding is needed in H and W dimensions only.
Merge [Pad] -> [Conv] into [Conv].
Merge [Pad] -> [GroupConv] into [GroupConv].
Merge [Pad] -> [MaxPool] into [MaxPool].
### `-fuse-post-ops`: Fuse activation functions with tasks that support post-processing
The pass is a part of `AdjustForVPU` pipeline.

Fuse activation functions (e.g. ReLU, leaky ReLU) with tasks that support post-processing
depending on the compilation mode
### `-fuse-quantized-ops`: Update quantize/dequantize ops
The pass is a part of `LowPrecision` pipeline.

Pass detects pattern quant.dcast -> op -> quant.qcast and converts it into single quantized Op
### `-handle-asymmetric-strides`: Handle operations with asymmetric strides
The pass is a part of `AdjustForVPU` pipeline.

This pass splits operations so that they are able to be infered with symmetric strides
    on dpu because of hardware limitation.
### `-handle-large-kernels`: Handle large kernels ops
The pass is a part of `AdjustForVPU` pipeline.

This pass replaces average pooling layers that have kernels bigger than supported by hardware (11x11),
with equivalent two average pooling (approx equiv in case of prime kernel i.e. 13x13).
### `-handle-large-strides`: Handle operations with large strides
This pass splits operations with strides larger than supported on hardware.
### `-insert-maxpool-to-concat-lrelu`: Insert Maxpool op between Concat and LeakyRelu ops
The pass is a part of `AdjustForVPU` pipeline.

Pass converts Concat->LeakyRelu to Concat->Maxpool->LeakyRelu.
### `-isolated-tiling`: Tile layers in isolation so that all their I/O meet the memory capacity
The pass applies tiling to the layers whose memory requirements exceed the capacity available.

The pass tries to split each single layer in isolation, with no smarter heuristics
such as "allow running in parallel" or "allow continious computation in tiles" or any else.

The pass does not use any cost model to optimize the entire layer's processing time. It just
iteratively increases the number of tiles until the the largest tile's memory requirements  meet
the device capacity, and stops there.
### `-legalize-dilated-conv`: Handle dilated convolutions
The pass is a part of `buildHardwareModePipeline` pipeline.

This pass expands filter of dilated convolution so that they are able to be infered
    on dpu because of hardware limitation.
### `-manual-tiling`: Tile layers with manual strategy
The pass performs manual tiling on layers specified by the user.
### `-matmul-inputs-to-2d`: Convert MatMul inputs to 2d
This pass converts `MatMul` inputs to 2d.

For example, `MatMul` input with 4x1x64 geometry will be split to four inputs with 1x64 dimensions.
Resulting inputs with filters go to `MatMul` operations and the outputs are concatenated.
### `-merge-fake-quant`: Merge back to FakeQuantize
The pass is a part of `LowPrecision` pipeline.

It merges pair `quant.qcast -> quant.dcast` into single `IE.FakeQuantize`.
The pass is used as a fallback to FP16 computations for the cases, where quantized types where not used by layers.
### `-optimize-reorders`: Optimize extra Reorder operations
The pass is a part of `IECommon` pipeline.

This pass tries to optimize out Reorder operations for common cases
by propagating them from inputs to outputs and merging into layers.
### `-optimize-unaligned-qdq-seq`: Swaps AffineReshape->FakeQuantize sequence if channels become unaligned after AffineReshape
Pass swaps order of AffineReshape->FakeQuantize sequence if channels become unaligned after AffineReshape
Otherwise additionals ops are introduce in order to align channels which impacts performance.
### `-per-axis-fq-concat`: Supports Concat operation with per-axis FQ inputs
The pass is a part of `HardwareMode` pipeline.

It creates `FakeQuantize` operation, which combines per-channel quantization from `Concat` inputs,
and places it after the `Concat` operation. For example:
The following `Concat`:
```
    FQ 1x256x128x128 -> Concat <- FQ 1x48x128x128
                          |
                        GroupConv 1x304x128x128
```
will be transformed into:
```
    FQ 1x256x128x128 -> Concat <- FQ 1x48x128x128
                          |
                         FQ 1x304x128x128
                          |
                        GroupConv 1x304x128x128
```
### `-prefetch-tiling`: Tile layers into smaller tiles to enable prefetch pipeline
The pass performs tiling on layers to enable prefetch pipeline.

The pass tries run tiles in parallel.
The 'prefetch' means that the next tile could be loaded in advance when the current tile is computing.

The pass does not consider cost models,
only tiles layers to make at least two tiles could be loaded in CMX memory at the same time.
### `-propagate-fq-through-pad`: Propagate FakeQuantize operation after Pad is replaced with Concat
`ConvertPadToConcat` adds a `Concat` operation which does not propagate `FakeQuantize` operation.

1. Check if such `Concat` operation has one and only one quantized input
2. Fetch quantization parameters
3. Apply them to every single `Concat` input and output
### `-propagate-quantize-dequantize`: Propagate Quantize/Dequantize through agnostic operations
The pass is a part of LowPrecision pipeline.

Quantize/Dequantize are propagated through operations
### `-remove-quantdequant-seq`: Removes quantize->dequantize ops sequence
The optional pass in the `LowPrecision` pipeline.

Pass detects pattern quantize -> dequantize and removes it
### `-resolve-pwl-post-ops`: Resolve requirements for fused PWL post-ops
Ensures the correct quantization ranges are used for fused PWL activation functions or
unfuses them if surrounding tensors are not quantized per-tensor.
### `-resolve-strided-slice`: Decouple strided slice to slice + reshape
The pass is a part of `AdjustForVPU` pipeline.
It replaces IE::StridedSlice with non zero masks to a simpler IE::StridedSlice with zero masks + IE::Reshape
It replaces IE::StridedSlice with dense<1> strides strides with a simple IE::Slice operation
### `-split-conv-with-multiple-fq`: Splits Convolution for multiple FakeQuantize
The pass is a part of `HardwareMode` pipeline.

It splits `Convolution` operation with multiple consumers with `FakeQuantize` operations,
into multiple `Convolution` operations, one for each consumer. This transformation is needed to be
able to quantize convolution and fuse bias and post-processing operations.
### `-split-fake-quant`: Splits FakeQuantize
The pass is a part of `LowPrecision` pipeline.

It splits `FakeQuantize` operations to `quant.qcast -> quant.dcast` pair.
### `-swap-concat-with-eltwise`: Swaps Concat operation with elementwise -> FQ patterns
The pass is a part of `HardwareMode` pipeline.

It swaps `Concat` operation with elementwise -> FQ subgraph when possible. For example:
* `PReLU` -> per-tensor `FakeQuantize` subgraph is eligible for such swap.

This transormation allows to fuse `FakeQuantize` to NCE operations.
### `-swap-fake-quant-reshape`: Swap FakeQuantize with Reshape when required to void redundant expand and permute ops
The pass is a part of `LowPrecision` pipeline.

It matches pattern non-channel-aligned op -> optional Reshapes -> FQ -> Reshapes -> channel-aligned op
Move the FQ right before the channel-aligned op to avoid redundant expand and permute ops.
### `-swap-maxpool-with-act`: Swaps the MaxPool and activation
This pass is needed for VPUX37XX only since HW MaxPool does not support post-op operations.
Operations are swapped only if there is an operation before MaxPool that supports post-ops.
### `-swap-transpose-with-fq`: Swaps Transpose operation with FakeQuantize
The pass is a part of `HardwareMode` pipeline.

It swaps `Transpose` operation with per-tensor `FakeQuantize` operation when possible.
This transormation reduces the number of `MemPermute` operations in resulting graph.
### `-transpose-reorder-concat-pass`: Inserts Reorder operation between Transpose and Concat
The pass is a part of `HardwareMode` pipeline.

It inserts `Reorder` operation between `Transpose` and `Concat` operation when possible.
This transormation reduces the number of `MemPermute` operations in resulting graph.
### `-transpose-to-permute-cast`: Converts Transpose operation to PermuteCast with Reorder
It is possible to replace a `Transpose` operation with a combination of `PermuteCast` and `Reorder`.
To compute the permutation cast, which is required for the source tensor, one must inverse the
affine map from the original `Transpose` operation. For example, consider the following transposition:
`1x16x32x64 -> 1x64x16x32`, its affine map is: `(d0, d1, d2, d3) -> (d0, d3, d1, d2)`.
The inverse will be:
```
    d0, d3, d1, d2   ->  d0, d1, d2, d3
    aN, aC, aH, aW   ->  aN, aH, aW, aC
```
Which gives permutation cast into NHWC.
In order to maintain the layout in data flow, `Reorder` must always rearrange `PermuteCast` result into the
order of original `Transpose` operation.
### `-uniquify-ops`: Remove duplicating operations with a common producer Value
The pass is a part of `AdjustForVPU` pipeline.

This pass merges operations that are identical to each other, combining consumers.
### `-unroll-batch`: Split FullyConnected inputs with multiple rows
This pass splits `FullyConnected` inputs by rows.

For example, `FullyConnected` input with 2x64 geometry will be split by two inputs with 1x64 dimensions.
Resulting vector rows go to corresponding `FullyConnected` operations and the outputs are concatenated.
### `-upstream-slice`: Optimization by upstreaming slice operations
Optimizes scenarios of IE::StridedSlice and IE::SliceOp without neighboring operations.
Moves the slice operations upwards through the graph, reducing both compute and memory usage.
In some cases the slice operation may be safely removed from the graph, if the action of upstreaming it
    only adapts the operations constants.
### `-use-user-layout`: Use user layouts for entry point function prototype
The pass is a part of `IECommon` pipeline.

This pass updates the CNNNetwork entry point function prototype
and uses user-provided layouts for its operands and results.
The pass inserts Reorder operations from/to topology layout.
### `-use-user-precision`: Use user precisions for entry point function prototype
The pass is a part of `IECommon` pipeline.

This pass updates the CNNNetwork entry point function prototype and use user-provided precisions for its operands and results.
The pass inserts Convert operations from/to topology precisions.
