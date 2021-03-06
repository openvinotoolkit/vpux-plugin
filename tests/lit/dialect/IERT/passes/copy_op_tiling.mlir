// RUN: vpux-opt --split-input-file --tile-copies %s | FileCheck %s

#NCHW = affine_map<(d0, d1, d2, d3) -> (d0, d1, d2, d3)>

func @SplitByChannels(
        %arg0: memref<1x320x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>,
        %arg1: memref<1x320x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>)
        -> memref<1x320x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}> {
    %0 = IERT.Copy inputs(%arg0 : memref<1x320x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>)
                   outputs(%arg1 : memref<1x320x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>)
                   -> memref<1x320x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>

    return %0 : memref<1x320x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>

    // CHECK: %[[ARG_0_TILE_0:.*]] = IERT.SubView %arg0 [0, 0, 0, 0] [1, 255, 32, 16] :
    // CHECK-SAME:              memref<1x320x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>
    // CHECK-SAME:           to memref<1x255x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>

    // CHECK: %[[ARG_1_TILE_0:.*]] = IERT.SubView %arg1 [0, 0, 0, 0] [1, 255, 32, 16] :
    // CHECK-SAME:              memref<1x320x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>
    // CHECK-SAME:           to memref<1x255x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>

    // CHECK: %[[COPY_TILE_0:.*]] = IERT.Copy
    // CHECK-SAME:  inputs(%[[ARG_0_TILE_0]] : memref<1x255x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>)
    // CHECK-SAME:  outputs(%[[ARG_1_TILE_0]] : memref<1x255x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>)
    // CHECK-SAME:  -> memref<1x255x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>

    // CHECK: %[[ARG_0_TILE_1:.*]] = IERT.SubView %arg0 [0, 255, 0, 0] [1, 65, 32, 16] :
    // CHECK-SAME:              memref<1x320x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>
    // CHECK-SAME:           to memref<1x65x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>

    // CHECK: %[[ARG_1_TILE_1:.*]] = IERT.SubView %arg1 [0, 255, 0, 0] [1, 65, 32, 16] :
    // CHECK-SAME:              memref<1x320x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>
    // CHECK-SAME:           to memref<1x65x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>

    // CHECK: %[[COPY_TILE_1:.*]] = IERT.Copy
    // CHECK-SAME:  inputs(%[[ARG_0_TILE_1]] : memref<1x65x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>)
    // CHECK-SAME:  outputs(%[[ARG_1_TILE_1]] : memref<1x65x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>)
    // CHECK-SAME:  -> memref<1x65x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>

    // CHECK: %[[CONCAT:.*]] = IERT.ConcatView
    // CHECK-SAME:  inputs(%[[COPY_TILE_0]], %[[COPY_TILE_1]] :
    // CHECK-SAME:      memref<1x255x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>,
    // CHECK-SAME:      memref<1x65x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>)
    // CHECK-SAME:  outputs(%arg1 : memref<1x320x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>)
    // CHECK-SAME:  -> memref<1x320x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>

    // CHECK: return %[[CONCAT]] : memref<1x320x32x16xf16, {order = #NCHW, strides = [655360, 2048, 32, 1]}>
}

// -----

#NHWC = affine_map<(d0, d1, d2, d3) -> (d0, d2, d3, d1)>

func @SplitByHeight(
        %arg0 : memref<1x18x360x1280xf16, {order = #NHWC, strides = [14745600, 1, 40960, 32]}>,
        %arg1 : memref<1x18x360x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}>)
        -> memref<1x18x360x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}> {
    %0 = IERT.Copy inputs(%arg0 : memref<1x18x360x1280xf16, {order = #NHWC, strides = [14745600, 1, 40960, 32]}>)
                   outputs(%arg1 : memref<1x18x360x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}>)
                   -> memref<1x18x360x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}>

    return %0 : memref<1x18x360x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}>

    // CHECK: %[[ARG_0_TILE_0:.*]] = IERT.SubView %arg0 [0, 0, 0, 0] [1, 18, 255, 1280] :
    // CHECK-SAME:              memref<1x18x360x1280xf16, {order = #NHWC, strides = [14745600, 1, 40960, 32]}
    // CHECK-SAME:           to memref<1x18x255x1280xf16, {order = #NHWC, strides = [14745600, 1, 40960, 32]}>

    // CHECK: %[[ARG_1_TILE_0:.*]] = IERT.SubView %arg1 [0, 0, 0, 0] [1, 18, 255, 1280] :
    // CHECK-SAME:              memref<1x18x360x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}>
    // CHECK-SAME:           to memref<1x18x255x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}>

    // CHECK: %[[COPY_TILE_0:.*]] = IERT.Copy
    // CHECK-SAME:  inputs(%[[ARG_0_TILE_0]] : memref<1x18x255x1280xf16, {order = #NHWC, strides = [14745600, 1, 40960, 32]}>)
    // CHECK-SAME:  outputs(%[[ARG_1_TILE_0]] : memref<1x18x255x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}>)
    // CHECK-SAME:  -> memref<1x18x255x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}>

    // CHECK: %[[ARG_0_TILE_1:.*]] = IERT.SubView %arg0 [0, 0, 255, 0] [1, 18, 105, 1280] :
    // CHECK-SAME:              memref<1x18x360x1280xf16, {order = #NHWC, strides = [14745600, 1, 40960, 32]}>
    // CHECK-SAME:           to memref<1x18x105x1280xf16, {order = #NHWC, strides = [14745600, 1, 40960, 32]}>

    // CHECK: %[[ARG_1_TILE_1:.*]] = IERT.SubView %arg1 [0, 0, 255, 0] [1, 18, 105, 1280] :
    // CHECK-SAME:              memref<1x18x360x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}>
    // CHECK-SAME:           to memref<1x18x105x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}>

    // CHECK: %[[COPY_TILE_1:.*]] = IERT.Copy
    // CHECK-SAME:  inputs(%[[ARG_0_TILE_1]] : memref<1x18x105x1280xf16, {order = #NHWC, strides = [14745600, 1, 40960, 32]}>)
    // CHECK-SAME:  outputs(%[[ARG_1_TILE_1]] : memref<1x18x105x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}>)
    // CHECK-SAME:  -> memref<1x18x105x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}>

    // CHECK: %[[CONCAT:.*]] = IERT.ConcatView
    // CHECK-SAME:  inputs(%[[COPY_TILE_0]], %[[COPY_TILE_1]] :
    // CHECK-SAME:      memref<1x18x255x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}>,
    // CHECK-SAME:      memref<1x18x105x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}>)
    // CHECK-SAME:  outputs(%arg1 : memref<1x18x360x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}>)
    // CHECK-SAME:  -> memref<1x18x360x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}>

    // CHECK: return %[[CONCAT]] : memref<1x18x360x1280xf16, {order = #NHWC, strides = [29491200, 1, 81920, 32]}>
}

#NCHW = affine_map<(d0, d1, d2, d3) -> (d0, d1, d2, d3)>

func @LegalizeCopy(
        %arg0: memref<1x64x512x512xf16, #NCHW>,
        %arg1: memref<1x64x512x512xf16, #NCHW>)
        -> memref<1x64x512x512xf16, #NCHW> {
    %0 = IERT.Copy inputs(%arg0 : memref<1x64x512x512xf16, #NCHW>)
                   outputs(%arg1 : memref<1x64x512x512xf16, #NCHW>)
                   -> memref<1x64x512x512xf16, #NCHW>

    return %0 : memref<1x64x512x512xf16, #NCHW>

    // Currently, large Copy nodes are tiled C-wise

    // Cut first tile:
    // CHECK: [[VAR0:%.*]] = IERT.SubView %arg0 [0, 0, 0, 0] [1, 32, 512, 512] :
    // CHECK-SAME:      memref<1x64x512x512xf16>
    // CHECK-SAME:   to memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>
    // CHECK: [[VAR1:%.*]] = IERT.SubView %arg1 [0, 0, 0, 0] [1, 32, 512, 512] :
    // CHECK-SAME:      memref<1x64x512x512xf16>
    // CHECK-SAME:   to memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>
    // CHECK: [[VAR2:%.*]] = IERT.Copy
    // CHECK-SAME:      inputs([[VAR0]] : memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>)
    // CHECK-SAME:      outputs([[VAR1]] : memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>)
    // CHECK-SAME:        -> memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>

    // Cut the second tile:
    // CHECK: [[VAR3:%.*]] = IERT.SubView %arg0 [0, 32, 0, 0] [1, 32, 512, 512] :
    // CHECK-SAME:      memref<1x64x512x512xf16>
    // CHECK-SAME:   to memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>
    // CHECK: [[VAR4:%.*]] = IERT.SubView %arg1 [0, 32, 0, 0] [1, 32, 512, 512] :
    // CHECK-SAME:      memref<1x64x512x512xf16>
    // CHECK-SAME:   to memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>
    // CHECK: [[VAR5:%.*]] = IERT.Copy
    // CHECK-SAME:      inputs([[VAR3]] : memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>)
    // CHECK-SAME:      outputs([[VAR4]] : memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>)
    // CHECK-SAME:        -> memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>

    // Concatenate the resulting output tiles:
    // CHECK: [[VAR6:%.*]] = IERT.ConcatView
    // CHECK-SAME:      inputs([[VAR2]], [[VAR5]] :
    // CHECK-SAME:        memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>,
    // CHECK-SAME:        memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>)
    // CHECK-SAME:      outputs(%arg1 : memref<1x64x512x512xf16>)
    // CHECK-SAME:        -> memref<1x64x512x512xf16>
    // CHECK: return [[VAR6]] : memref<1x64x512x512xf16>
}

// -----

#NCHW = affine_map<(d0, d1, d2, d3) -> (d0, d1, d2, d3)>

func @LegalizeStridedCopy(
        %arg0: memref<1x64x512x512xf16, {order = #NCHW, strides = [33554432, 262144, 512, 1]}>,
        %arg1: memref<1x64x512x512xf16, #NCHW>)
        -> memref<1x64x512x512xf16, #NCHW> {
    %0 = IERT.Copy inputs(%arg0 : memref<1x64x512x512xf16, {order = #NCHW, strides = [33554432, 262144, 512, 1]}>)
                   outputs(%arg1 : memref<1x64x512x512xf16, #NCHW>)
                   -> memref<1x64x512x512xf16, #NCHW>

    return %0 : memref<1x64x512x512xf16, #NCHW>

    // Currently, large Copy nodes are tiled C-wise
    // If the Copy is strided, the strides should be preserved

    // Cut the first tile:
    // CHECK: [[VAR0:%.*]] = IERT.SubView %arg0 [0, 0, 0, 0] [1, 32, 512, 512] :
    // CHECK-SAME:      memref<1x64x512x512xf16, {order = #NCHW, strides = [33554432, 262144, 512, 1]}>
    // CHECK-SAME:   to memref<1x32x512x512xf16, {order = #NCHW, strides = [33554432, 262144, 512, 1]}>
    // CHECK: [[VAR1:%.*]] = IERT.SubView %arg1 [0, 0, 0, 0] [1, 32, 512, 512] :
    // CHECK-SAME:      memref<1x64x512x512xf16>
    // CHECK-SAME:   to memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>

    // The Copy-tile preserves the original strides:
    // CHECK: [[VAR2:%.*]] = IERT.Copy
    // CHECK-SAME:      inputs([[VAR0]] : memref<1x32x512x512xf16, {order = #NCHW, strides = [33554432, 262144, 512, 1]}>)
    // CHECK-SAME:      outputs([[VAR1]] : memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>)
    // CHECK-SAME:        -> memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>

    // Cut the second tile:
    // CHECK: [[VAR3:%.*]] = IERT.SubView %arg0 [0, 32, 0, 0] [1, 32, 512, 512] :
    // CHECK-SAME:      memref<1x64x512x512xf16, {order = #NCHW, strides = [33554432, 262144, 512, 1]}>
    // CHECK-SAME:   to memref<1x32x512x512xf16, {order = #NCHW, strides = [33554432, 262144, 512, 1]}>
    // CHECK: [[VAR4:%.*]] = IERT.SubView %arg1 [0, 32, 0, 0] [1, 32, 512, 512] :
    // CHECK-SAME:      memref<1x64x512x512xf16>
    // CHECK-SAME:   to memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>

    // The Copy-tile preserves the original strides:
    // CHECK: [[VAR5:%.*]] = IERT.Copy
    // CHECK-SAME:      inputs([[VAR3]] : memref<1x32x512x512xf16, {order = #NCHW, strides = [33554432, 262144, 512, 1]}>)
    // CHECK-SAME:      outputs([[VAR4]] : memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>)
    // CHECK-SAME:        -> memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>

    // Concatenate the resulting output tiles:
    // CHECK: [[VAR6:%.*]] = IERT.ConcatView
    // CHECK-SAME:      inputs([[VAR2]], [[VAR5]] :
    // CHECK-SAME:        memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>,
    // CHECK-SAME:        memref<1x32x512x512xf16, {order = #NCHW, strides = [16777216, 262144, 512, 1]}>)
    // CHECK-SAME:      outputs(%arg1 : memref<1x64x512x512xf16>)
    // CHECK-SAME:        -> memref<1x64x512x512xf16>
    // CHECK: return [[VAR6]] : memref<1x64x512x512xf16>
}

// -----

#NCHW = affine_map<(d0, d1, d2, d3) -> (d0, d1, d2, d3)>

func @DoNotLegalizeCopy(
        %arg0: memref<1x32x512x512xf16, #NCHW>,
        %arg1: memref<1x32x512x512xf16, #NCHW>)
        -> memref<1x32x512x512xf16, #NCHW> {
    %0 = IERT.Copy inputs(%arg0 : memref<1x32x512x512xf16, #NCHW>)
                   outputs(%arg1 : memref<1x32x512x512xf16, #NCHW>)
                   -> memref<1x32x512x512xf16, #NCHW>

    return %0 : memref<1x32x512x512xf16, #NCHW>

    // Small enough Copy nodes (those with transaction volume less than 16MB) should not be affected by the pass

    // CHECK: [[VAR0:%.*]] = IERT.Copy
    // CHECK-SAME:      inputs(%arg0 : memref<1x32x512x512xf16>)
    // CHECK-SAME:      outputs(%arg1 : memref<1x32x512x512xf16>)
    // CHECK-SAME:        -> memref<1x32x512x512xf16>
    // CHECK: return [[VAR0]] : memref<1x32x512x512xf16>
}
