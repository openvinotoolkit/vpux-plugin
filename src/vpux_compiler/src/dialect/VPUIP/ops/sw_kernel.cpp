//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

//

#include "vpux/compiler/dialect/VPUIP/ops.hpp"

#include <llvm/ADT/TypeSwitch.h>

using namespace vpux;
using namespace mlir;

namespace {
// special format of dims/order available only on kernel-FW side
int64_t computeReverseMemDim(mlir::Value tensorArg, int64_t dimIdx) {
    const auto inOrder = DimsOrder::fromValue(tensorArg);
    MemDim md = inOrder.toMemDim(Dim(dimIdx));

    const auto shape = getShape(tensorArg);
    auto nDims = checked_cast<uint32_t>(shape.size());
    return nDims - 1 - md.ind();
}

static SmallVector<int64_t> reversePermutation(mlir::AffineMap map) {
    const auto origPerm = DimsOrder::fromAffineMap(map).toPermutation();
    SmallVector<int64_t> revPerm(origPerm.size());
    for (const auto srcInd : irange(origPerm.size())) {
        const auto dstInd = origPerm[srcInd].ind();
        const auto revSrcInd = origPerm.size() - 1 - srcInd;
        const auto revDstInd = origPerm.size() - 1 - dstInd;
        revPerm[revSrcInd] = revDstInd;
    }

    return revPerm;
}
}  // namespace

namespace vpux {
namespace VPUIP {

VPUIP::BlobWriter::SpecificTask SwKernelOp::serialize(vpux::VPUIP::BlobWriter& writer) {
    return writer.createSW_KernelTask(*this);
}

void SwKernelOp::build(mlir::OpBuilder& builder, mlir::OperationState& opState, mlir::ValueRange inputs,
                       mlir::ValueRange results, mlir::SymbolRefAttr kernelFunction, mlir::IntegerAttr tileIndex) {
    // looks this is a result types
    build(builder, opState, results.getTypes(), kernelFunction, inputs, results, tileIndex);
}

mlir::LogicalResult SwKernelOp::inferReturnTypes(mlir::MLIRContext* ctx, mlir::Optional<mlir::Location> optLoc,
                                                 mlir::ValueRange operands, mlir::DictionaryAttr attrs,
                                                 mlir::RegionRange /*regions*/,
                                                 mlir::SmallVectorImpl<mlir::Type>& inferredTypes) {
    const auto loc = optLoc.getValueOr(mlir::UnknownLoc::get(ctx));

    VPUIP::SwKernelOpAdaptor swKernelOp(operands, attrs);
    if (mlir::failed(swKernelOp.verify(loc))) {
        return mlir::failure();
    }

    VPUX_THROW_UNLESS(swKernelOp.inputs().size() == 1, "For now act-kernels with only one input are supported. Got {0}",
                      swKernelOp.inputs().size());
    VPUX_THROW_UNLESS(swKernelOp.output_buffs().size() == 1,
                      "For now act-kernels with only one output are supported. Got {0}",
                      swKernelOp.output_buffs().size());

    const auto outType = swKernelOp.output_buffs()[0].getType();

    inferredTypes.push_back(outType);

    return mlir::success();
}

IERT::KernelInfo SwKernelOp::getKernelInfo(mlir::Operation* origOp) {
    mlir::MLIRContext* ctx = origOp->getContext();

    return llvm::TypeSwitch<mlir::Operation*, IERT::KernelInfo>(origOp)
            .Case<IERT::ExpOp>([&](IERT::ExpOp) {
                return IERT::KernelInfo{SmallVector<mlir::Attribute>{}, {"exp_fp16"}, {"exp_fp16.cpp"}};
            })
            .Case<IERT::TanhOp>([&](IERT::TanhOp) {
                return IERT::KernelInfo{SmallVector<mlir::Attribute>{}, {"tanh_fp16"}, {"tanh_fp16.cpp"}};
            })
            .Case<IERT::HSwishOp>([&](IERT::HSwishOp) {
                return IERT::KernelInfo{SmallVector<mlir::Attribute>{}, {"hswish_fp16"}, {"hswish_fp16.cpp"}};
            })
            .Case<IERT::SigmoidOp>([&](IERT::SigmoidOp) {
                return IERT::KernelInfo{SmallVector<mlir::Attribute>{}, {"sigmoid_fp16"}, {"sigmoid_fp16.c"}};
            })
            .Case<IERT::SoftMaxOp>([&](IERT::SoftMaxOp softmax) {
                // input tensor, to transform axis
                const auto axisParam = computeReverseMemDim(softmax.input(), softmax.axisInd());
                const auto axisParamAttr = getIntAttr(ctx, axisParam);
                return IERT::KernelInfo{SmallVector<mlir::Attribute>{axisParamAttr},
                                        {"singleShaveSoftmax"},
                                        {"single_shave_softmax.cpp"}};
            })
            .Case<IERT::InterpolateOp>([&](IERT::InterpolateOp Interpolate) {
                const auto mode = static_cast<int64_t>(Interpolate.modeAttr().getValue());
                const auto coordMode = static_cast<int64_t>(Interpolate.coord_modeAttr().getValue());
                const auto nearestMode = static_cast<int64_t>(Interpolate.nearest_modeAttr().getValue());
                const auto antialias = static_cast<int64_t>(Interpolate.antialiasAttr() != nullptr);

                const auto modeAttr = getIntAttr(ctx, mode);
                const auto coordModeAttr = getIntAttr(ctx, coordMode);
                const auto nearestModeAttr = getIntAttr(ctx, nearestMode);
                const auto antialiasAttr = getIntAttr(ctx, antialias);

                return IERT::KernelInfo{
                        SmallVector<mlir::Attribute>{modeAttr, coordModeAttr, nearestModeAttr, antialiasAttr},
                        {"singleShaveInterpolate"},
                        {"single_shave_interpolate.cpp"}};
            })
            .Case<IERT::EluOp>([&](IERT::EluOp elu) {
                return IERT::KernelInfo{SmallVector<mlir::Attribute>{elu.xAttr()}, {"elu_fp16"}, {"elu_fp16.cpp"}};
            })
            .Case<IERT::SqrtOp>([&](IERT::SqrtOp) {
                return IERT::KernelInfo{SmallVector<mlir::Attribute>{}, {"sqrt_fp16"}, {"sqrt_fp16.cpp"}};
            })
            .Case<IERT::MVNOp>([&](IERT::MVNOp MVN) {
                return IERT::KernelInfo{SmallVector<mlir::Attribute>{MVN.across_channelsAttr(),
                                                                     MVN.normalize_varianceAttr(), MVN.epsAttr()},
                                        {"singleShaveMVN"},
                                        {"single_shave_MVN.cpp"}};
            })
            .Case<IERT::MemPermuteOp>([&](IERT::MemPermuteOp permute) {
                auto memPermArr = reversePermutation(permute.mem_perm());
                return IERT::KernelInfo{SmallVector<mlir::Attribute>{getIntArrayAttr(ctx, memPermArr)},
                                        {"reorder_fp16"},
                                        {"reorder_fp16.cpp"}};
            })
            .Default([](mlir::Operation* unknownOp) -> IERT::KernelInfo {
                VPUX_THROW("Operation '{0}' is not supported by the act-shaves", unknownOp->getName());
            });
}

}  // namespace VPUIP
}  // namespace vpux
