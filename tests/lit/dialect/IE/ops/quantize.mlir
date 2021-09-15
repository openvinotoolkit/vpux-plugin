// RUN: vpux-opt --split-input-file --canonicalize %s | FileCheck %s

!qElemType = type !quant.uniform<u8:f16, 2.4627450980392158>
func @ConstFold() -> tensor<1x8x4x4x!qElemType> {
    %0 = const.Declare tensor<1x8x4x4xf32> = #const.Content<dense<5.0> : tensor<1x8x4x4xf32>>
    %1 = IE.Quantize(%0) {dstElemType = !qElemType}: tensor<1x8x4x4xf32> -> tensor<1x8x4x4x!qElemType>
    return %1 : tensor<1x8x4x4x!qElemType>

    // CHECK:       [[VAL0:%.*]] = const.Declare tensor<1x8x4x4x!quant.uniform<u8:f16, 2.4627450980392158>> =
    // CHECK-SAME:       #const.Content<dense<5.000000e+00> : tensor<1x8x4x4xf32>,
    // CHECK-SAME:       [#const.ConvertElemType<ui8>, #const.QuantCast<!qElemType>]
    // CHECK-NOT:   IE.Quantize
    // CHECK:       return [[VAL0]]
}

// -----

!qElemType = type !quant.uniform<u8:f16, 2.4627450980392158>
func @FuseDequantQuantWithSeveralUses(%arg0: tensor<1x8x4x4x!qElemType>) -> (tensor<1x8x4x4xf16>, tensor<1x8x4x4x!qElemType>) {
    %0 = IE.Dequantize(%arg0) {dstElemType = f16} : tensor<1x8x4x4x!qElemType> -> tensor<1x8x4x4xf16>
    %1 = IE.ReLU(%0) : tensor<1x8x4x4xf16> -> tensor<1x8x4x4xf16>
    %2 = IE.Quantize(%0) {dstElemType = !qElemType}: tensor<1x8x4x4xf16> -> tensor<1x8x4x4x!qElemType>
    return %1, %2 : tensor<1x8x4x4xf16>, tensor<1x8x4x4x!qElemType>

    // CHECK-DAG:   [[VAL0:%.*]] = IE.Dequantize(%arg0)
    // CHECK-NOT:   IE.Quantize
    // CHECK:       [[VAL1:%.*]] = IE.ReLU([[VAL0]])
    // CHECK:       return [[VAL1]], %arg0 : tensor<1x8x4x4xf16>, tensor<1x8x4x4x!quant.uniform<u8:f16, 2.4627450980392158>>
}

// -----

!qElemType = type !quant.uniform<u8:f16, 2.4627450980392158>
func @FuseDequantQuant(%arg0: tensor<1x8x4x4x!qElemType>) -> tensor<1x8x4x4x!qElemType> {
    %1 = IE.Dequantize(%arg0) {dstElemType = f16} : tensor<1x8x4x4x!qElemType> -> tensor<1x8x4x4xf16>
    %2 = IE.Quantize(%1) {dstElemType = !qElemType}: tensor<1x8x4x4xf16> -> tensor<1x8x4x4x!qElemType>
    return %2 : tensor<1x8x4x4x!qElemType>

    // CHECK-NOT:   IE.Dequantize
    // CHECK-NOT:   IE.Quantize
    // CHECK:       return %arg0 : tensor<1x8x4x4x!quant.uniform<u8:f16, 2.4627450980392158>>
}

// -----

!qElemType = type !quant.uniform<u8<0:254>:f16:1, {8.7179349163385824E-4:127,5.2096149114173233E-4:127}>
func @FuseDequantQuantPerAxis(%arg0: tensor<1x2x4x4x!qElemType>) -> tensor<1x2x4x4x!qElemType> {
    %1 = IE.Dequantize(%arg0) {dstElemType = f16} : tensor<1x2x4x4x!qElemType> -> tensor<1x2x4x4xf16>
    %2 = IE.Quantize(%1) {dstElemType = !qElemType}: tensor<1x2x4x4xf16> -> tensor<1x2x4x4x!qElemType>
    return %2 : tensor<1x2x4x4x!qElemType>

    // CHECK-NOT:   IE.Dequantize
    // CHECK-NOT:   IE.Quantize
    // CHECK:       return %arg0 : tensor<1x2x4x4x!quant.uniform<u8<0:254>:f16:1, {8.7179349163385824E-4:127,5.2096149114173233E-4:127}>>
}

// -----

!qElemType1 = type !quant.uniform<u8:f16, 1.4627450980392158>
!qElemType2 = type !quant.uniform<u8:f16, 2.3463457356746546>
func @DifferentQuantizationParams(%arg0: tensor<1x8x4x4x!qElemType1>) -> tensor<1x8x4x4x!qElemType2> {
    %0 = IE.Dequantize(%arg0) {dstElemType = f16} : tensor<1x8x4x4x!qElemType1> -> tensor<1x8x4x4xf16>
    %1 = IE.Quantize(%0) {dstElemType = !qElemType2}: tensor<1x8x4x4xf16> -> tensor<1x8x4x4x!qElemType2>
    return %1 : tensor<1x8x4x4x!qElemType2>

    // CHECK-DAG:   IE.Dequantize
    // CHECK-DAG:   IE.Quantize
    // CHECK:   return %1 : tensor<1x8x4x4x!quant.uniform<u8:f16, 2.3463457356746544>>
}

// -----

!qElemType1 = type !quant.uniform<u8<0:254>:f16:1, {8.7179349163385824E-4:127,5.2096149114173233E-4:127}>
!qElemType2 = type !quant.uniform<u8<0:254>:f16:1, {3.4678567856785681E-4:127,9.5675698679264696E-4:127}>
func @DifferentQuantizationParams(%arg0: tensor<1x2x4x4x!qElemType1>) -> tensor<1x2x4x4x!qElemType2> {
    %0 = IE.Dequantize(%arg0) {dstElemType = f16} : tensor<1x2x4x4x!qElemType1> -> tensor<1x2x4x4xf16>
    %1 = IE.Quantize(%0) {dstElemType = !qElemType2}: tensor<1x2x4x4xf16> -> tensor<1x2x4x4x!qElemType2>
    return %1 : tensor<1x2x4x4x!qElemType2>

    // CHECK-DAG:   IE.Dequantize
    // CHECK-DAG:   IE.Quantize
    // CHECK:   return %1 : tensor<1x2x4x4x!quant.uniform<u8<0:254>:f16:1, {3.4678567856785681E-4:127,9.5675698679264696E-4:127}>>
}
