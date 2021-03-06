//
// Copyright (C) 2022 Intel Corporation.
// SPDX-License-Identifier: Apache 2.0
//

//

#pragma once

#include "InferGraph.h"
#include "simple_graph.hpp"

//------------------------------------------------------------------------------
class HddlUnite_Graph_Helper {
public:
    using Ptr = std::shared_ptr<HddlUnite_Graph_Helper>;

    HddlUnite_Graph_Helper();
    ~HddlUnite_Graph_Helper();
    explicit HddlUnite_Graph_Helper(const HddlUnite::WorkloadContext& workloadContext);

    HddlUnite::Inference::Graph::Ptr getGraph();

protected:
    HddlUnite::Inference::Graph::Ptr _graphPtr = nullptr;
    std::stringstream _blobStream;
};

//------------------------------------------------------------------------------
inline HddlUnite_Graph_Helper::HddlUnite_Graph_Helper() {
    utils::simpleGraph::getExeNetwork()->Export(_blobStream);
    const auto blobData = _blobStream.str();
    HddlStatusCode statusCode = HddlUnite::Inference::loadGraph(
            _graphPtr, "simpleGraph", blobData.data(), blobData.size());
    if (statusCode != HDDL_OK) {
        IE_THROW() << "Failed to load graph";
    }
}

inline HddlUnite_Graph_Helper::~HddlUnite_Graph_Helper() {
    HddlUnite::Inference::unloadGraph(_graphPtr);
}


inline HddlUnite_Graph_Helper::HddlUnite_Graph_Helper(const HddlUnite::WorkloadContext& workloadContext) {
    const auto blobData = _blobStream.str();
    HddlStatusCode statusCode = HddlUnite::Inference::loadGraph(
            _graphPtr, "simpleGraph", blobData.data(), blobData.size(), {workloadContext});
    if (statusCode != HDDL_OK) {
        IE_THROW() << "Failed to load graph";
    }
}

inline HddlUnite::Inference::Graph::Ptr HddlUnite_Graph_Helper::getGraph() {
    return _graphPtr;
}
