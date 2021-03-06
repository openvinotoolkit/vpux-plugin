//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#ifndef HDE_HPP_
#define HDE_HPP_

#include "include/mcm/tensor/tensor.hpp"
#include "include/mcm/computation/model/control_model.hpp"
#include "include/mcm/computation/model/data_model.hpp"
#include "include/mcm/utils/compression/compressor.hpp"
#include "include/huffman_encoding/Huffman.hpp"
#include "include/huffman_encoding/huffmanCodec.hpp"


namespace mv
{
class Hde : public Compressor
{
    public:
        Hde(uint32_t bitPerSymbol, uint32_t maxNumberEncodedSymbols, uint32_t verbosity, uint32_t blockSize, bool pStatsOnly, uint32_t bypassMode);
        std::unique_ptr<huffmanCodec> codec_ = nullptr;

        std::pair<std::vector<int64_t>, uint32_t> compress(std::vector<int64_t>& data, mv::Tensor& t);
        std::pair<std::vector<int64_t>, uint32_t> compress(std::vector<int64_t>& data, mv::Data::TensorIterator& t);
        std::vector<uint8_t> decompress(std::vector<uint8_t>& compressedData);
};
} 
#endif 