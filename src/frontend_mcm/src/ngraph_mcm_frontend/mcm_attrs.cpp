//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

// clang-format off

#include "ngraph_mcm_frontend/mcm_attrs.hpp"
#include <sstream>
#include <memory>
#include <algorithm>

#include <iostream>

mv::Order cvtLayoutToMCM(ie::Layout layout) {
    std::ostringstream layoutName;
    if (ie::Layout::SCALAR == layout) {
        std::cout << "Unsupported layout " << layout << std::endl;
        layout = ie::Layout::C;
    }
    layoutName << layout;
    return mv::Order(layoutName.str());
}

McmOpAttrs& McmOpAttrs::get(std::shared_ptr<ngraph::Node> node) {
    if (const auto attrs = std::dynamic_pointer_cast<McmOpAttrs>(node->get_op_annotations())) {
        return *attrs;
    }

    const auto attrs = std::make_shared<McmOpAttrs>();
    node->set_op_annotations(attrs);

    return *attrs;
}

void McmOpAttrs::copy(std::shared_ptr<ngraph::Node> src_node, std::shared_ptr<ngraph::Node> dst_node) {
    const auto attrs = std::make_shared<McmOpAttrs>(get(src_node));
    dst_node->set_op_annotations(attrs);
}

const mv::Order& McmOpAttrs::getOrder(std::shared_ptr<ngraph::Node> node, size_t outInd) {
    auto& attrs = get(node);

    const auto it = attrs._mvOrders.find(outInd);
    if (it != attrs._mvOrders.end()) {
        return it->second;
    }

    const auto tensor = node->output(outInd).get_tensor_ptr();
    const auto ieLayout = ie::TensorDesc::getLayoutByDims(tensor->get_shape());
    const auto order = cvtLayoutToMCM(ieLayout);

    const auto res = attrs._mvOrders.insert({outInd, order});
    IE_ASSERT(res.second);

    return res.first->second;
}

void McmOpAttrs::setOrder(const mv::Order& order, std::shared_ptr<ngraph::Node> node, size_t outInd) {
    auto& attrs = get(node);

    const auto tensor = node->output(outInd).get_tensor_ptr();
    IE_ASSERT(order.size() == tensor->get_shape().size());

    const auto it = attrs._mvOrders.find(outInd);
    if (it != attrs._mvOrders.end()) {
        it->second = order;
    } else {
        const auto res = attrs._mvOrders.insert({outInd, order});
        IE_ASSERT(res.second);
    }
}

// clang-format on
