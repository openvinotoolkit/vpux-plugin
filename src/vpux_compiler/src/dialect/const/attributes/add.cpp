//
// Copyright Intel Corporation.
//
// LEGAL NOTICE: Your use of this software and any required dependent software
// (the "Software Package") is subject to the terms and conditions of
// the Intel(R) OpenVINO(TM) Distribution License for the Software Package,
// which may also include notices, disclaimers, or license terms for
// third party or open source software included in or with the Software Package,
// and your use indicates your acceptance of all such terms. Please refer
// to the "third-party-programs.txt" or other similarly-named text file
// included with the Software Package for additional details.
//

#include "vpux/compiler/dialect/const/attributes/content.hpp"
#include "vpux/compiler/utils/attributes.hpp"

#include "vpux/utils/IE/loop.hpp"
#include "vpux/utils/core/func_ref.hpp"

#include <mlir/Dialect/Quant/QuantTypes.h>
#include <mlir/IR/DialectImplementation.h>

using namespace vpux;

//
// AddAttr::walkImmediateSubElements
//

void vpux::Const::AddAttr::walkImmediateSubElements(llvm::function_ref<void(Attribute)> walkAttrsFn,
                                                    llvm::function_ref<void(mlir::Type)>) const {
    walkAttrsFn(getBias());
}

//
// AddAttr::print
//

void vpux::Const::AddAttr::print(mlir::DialectAsmPrinter& printer) const {
    printer << getMnemonic() << "<";
    printer.printAttribute(getBias());
    printer << ">";
}

//
// PadWithZeroAttr::parse
//

mlir::Attribute vpux::Const::AddAttr::parse(mlir::DialectAsmParser& parser, mlir::Type) {
    if (mlir::failed(parser.parseLess())) {
        return nullptr;
    }

    mlir::FloatAttr bias;
    if (mlir::failed(parser.parseAttribute(bias))) {
        return nullptr;
    }

    if (mlir::failed(parser.parseGreater())) {
        return nullptr;
    }

    return Const::AddAttr::get(bias);
}

//
// AddAttr::inferOutputType
//

mlir::ShapedType vpux::Const::AddAttr::inferOutputType(mlir::ShapedType input) const {
    return input;
}

//
// AddAttr::transform
//

Const::Content vpux::Const::AddAttr::transform(vpux::Const::Content& input) const {
    auto output = Const::Content::allocTempBuffer(inferOutputType(input.getType()),
                                                  mlir::Float32Type::get(getContext()), input.isSplat());

    const auto values = input.getValues<float>();
    auto shiftedVals = output.getTempBuf<float>();

    const auto bias = static_cast<float>(getBias().getValue().convertToDouble());

    loop_1d(LoopExecPolicy::Parallel, shiftedVals.size(), [&](size_t i) {
        shiftedVals[i] = values[i] + bias;
    });

    return output;
}

Const::ContentAttr vpux::Const::ContentAttr::add(double bias) const {
    return get(*this, Const::AddAttr::get(getFPAttr(getContext(), bias)).cast<Const::TransformAttrInterface>());
}
