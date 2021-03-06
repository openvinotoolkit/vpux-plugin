//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

#pragma once

// clang-format off

#include "ngraph_mcm_frontend/mcm_helpers.hpp"
#include <ie_layouts.h>
#include <ngraph/node.hpp>
#include <ngraph/descriptor/tensor.hpp>
#include <ngraph/op/util/op_annotations.hpp>
#include <include/mcm/tensor/order/order.hpp>
#include <include/mcm/tensor/quantization_params.hpp>
#include <memory>
#include <unordered_map>
#include <algorithm>

namespace ie = InferenceEngine;

mv::Order cvtLayoutToMCM(ie::Layout layout);

class McmOpAttrs final : public ngraph::op::util::OpAnnotations {
public:
    static McmOpAttrs& get(std::shared_ptr<ngraph::Node> node);
    static void copy(std::shared_ptr<ngraph::Node> src_node, std::shared_ptr<ngraph::Node> dst_node);

    static const mv::Order& getOrder(std::shared_ptr<ngraph::Node> node, size_t outInd = 0);
    static void setOrder(const mv::Order& order, std::shared_ptr<ngraph::Node> node, size_t outInd = 0);

private:
    std::unordered_map<size_t, mv::Order> _mvOrders;
};

// clang-format on
