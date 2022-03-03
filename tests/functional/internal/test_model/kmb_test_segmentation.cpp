//
// Copyright 2020 Intel Corporation.
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

#include "kmb_test_base.hpp"

#include <iostream>

/*
 *  Semantic segmentation
 */
static std::vector<long> fp32toNearestLong(const Blob::Ptr& ieBlob) {
    std::vector<long> nearestLongCollection;
    const float* blobRawPtr = ieBlob->cbuffer().as<const float*>();
    for (size_t pos = 0; pos < ieBlob->size(); pos++) {
        long nearestLong = std::lround(blobRawPtr[pos]);
        nearestLongCollection.push_back(nearestLong);
    }

    return nearestLongCollection;
}

static std::vector<long> int32toLong(const Blob::Ptr& ieBlob) {
    std::vector<long> nearestLongCollection;
    const int32_t* blobRawPtr = ieBlob->cbuffer().as<const int32_t*>();
    for (size_t pos = 0; pos < ieBlob->size(); pos++) {
        nearestLongCollection.push_back(blobRawPtr[pos]);
    }

    return nearestLongCollection;
}

/*
 * Calculates intersections and unions using associative containers.
 * 1. Create intersection mapping with label as key and number of elements as value
 * 2. Create union mapping with label as key and and number of elements as value
 * 3. For each offset increase intersection and union maps
 * 4. For each union label divide intersection cardinality by union cardinality
 */
static float calculateMeanIntersectionOverUnion(
        const std::vector<long>& vpuOut,
        const std::vector<long>& cpuOut) {
    std::unordered_map<long, size_t> intersectionMap;
    std::unordered_map<long, size_t> unionMap;
    for (size_t pos = 0; pos < vpuOut.size() && pos < cpuOut.size(); pos++) {
        long vpuLabel = vpuOut.at(pos);
        long cpuLabel = cpuOut.at(pos);
        if (vpuLabel == cpuLabel) {
            // labels are the same -- increment intersection at label key
            // increment union at that label key only once
            // if label has not been created yet, std::map sets it to 0
            intersectionMap[vpuLabel]++;
            unionMap[vpuLabel]++;
        } else {
            // labels are different -- increment element count at both labels
            unionMap[vpuLabel]++;
            unionMap[cpuLabel]++;
        }
    }

    float totalIoU = 0.f;
    size_t nonZeroUnions = 0;
    for (const auto& unionPair : unionMap) {
        const auto& labelId = unionPair.first;
        float intersectionCardinality = intersectionMap[labelId];
        float unionCardinality = unionPair.second;
        float classIoU = intersectionCardinality / unionCardinality;
        std::cout << "Label: " << labelId << " IoU: " << classIoU << std::endl;
        nonZeroUnions++;
        totalIoU += classIoU;
    }

    IE_ASSERT(nonZeroUnions != 0);
    float meanIoU = totalIoU / nonZeroUnions;
    return meanIoU;
}

void KmbSegmentationNetworkTest::runTest(
        const TestNetworkDesc& netDesc,
        const TestImageDesc& image,
        const float meanIntersectionOverUnionTolerance) {
    const auto check = [=](const BlobMap& actualBlobs,
                           const BlobMap& refBlobs,
                           const ConstInputsDataMap&) {
        IE_ASSERT(actualBlobs.size() == 1 &&
                  actualBlobs.size() == refBlobs.size());
        const auto& actualBlob = actualBlobs.begin()->second;
        const auto& refBlob    = refBlobs.begin()->second;
        // FIXME VPU compiler overrides any output precision to FP32 when asked
        // CPU doesn't override I32 output precision during compilation

        std::vector<long> vpuOut = fp32toNearestLong(vpux::toFP32(as<MemoryBlob>(actualBlob)));
        std::vector<long> cpuOut = int32toLong(refBlob);
        ASSERT_EQ(vpuOut.size(), cpuOut.size())
            << "vpuOut.size: " << vpuOut.size() << " "
            << "cpuOut.size: " << cpuOut.size();

        float meanIoU = calculateMeanIntersectionOverUnion(vpuOut, cpuOut);
        EXPECT_LE(meanIntersectionOverUnionTolerance, meanIoU)
            << "meanIoU: " << meanIoU << " "
            << "meanIoUTolerance: " << meanIntersectionOverUnionTolerance;
    };

    const auto init_input = [=](const ConstInputsDataMap& inputs) {
      IE_ASSERT(inputs.size() == 1);
      auto inputTensorDesc = inputs.begin()->second->getTensorDesc();

      registerBlobGenerator(
          inputs.begin()->first,
          inputTensorDesc,
          [&image](const TensorDesc& desc) {
            const auto blob = loadImage(image, desc.getDims()[1], desc.getDims()[2], desc.getDims()[3]);
            IE_ASSERT(blob->getTensorDesc().getDims() == desc.getDims());

            return vpux::toPrecision(vpux::toLayout(as<MemoryBlob>(blob), desc.getLayout()), desc.getPrecision());
          });
    };

    KmbNetworkTestBase::runTest(netDesc, init_input, check);
}

std::vector<std::pair<int, float>> getTopK(std::vector<float> probs, size_t topK) {
    using ClassProb = std::pair<int, float>;
    std::vector<ClassProb> sorted_probs;
    for(size_t i = 0; i < probs.size(); ++i) {
        sorted_probs.emplace_back(i, probs[i]);
    }

    std::sort(sorted_probs.begin(), sorted_probs.end(), [](const ClassProb& left, const ClassProb& right) {
        return left.second > right.second;
    });

    sorted_probs.resize(std::min(topK, sorted_probs.size()));
    return sorted_probs;

}

std::vector<long> perPixelLabel(Blob::Ptr blob) {
    auto blobLock = as<MemoryBlob>(blob)->rmap();
    IE_ASSERT(blobLock != nullptr);
    auto data = blobLock.as<const float*>();
    IE_ASSERT(data != nullptr);

    auto dims = blob->getTensorDesc().getDims();
    IE_ASSERT(dims.size() == 4 && dims[0] == 1);

    std::vector<long> labels;
    for (size_t h = 0; h < dims[2]; ++h) {
        for (size_t w = 0; w < dims[3]; ++w) {
            std::vector<float> probs;
            for (size_t c = 0; c < dims[1]; ++c) {
                probs.push_back(data[c * dims[2] * dims[3] + h * dims[3] + w]);
            }

            labels.push_back(getTopK(probs,1).at(0).first);
        }
    }

    return labels;
}

void UnetNetworkTest::runTest(
    const TestNetworkDesc& netDesc, const TestImageDesc& image, const float meanIntersectionOverUnionTolerance) {
    const auto check = [=](const BlobMap& actualBlobs,
                           const BlobMap& refBlobs,
                           const ConstInputsDataMap&) {

      IE_ASSERT(actualBlobs.size() == 1 &&
                actualBlobs.size() == refBlobs.size());
      const auto& actualBlob = actualBlobs.begin()->second;
      const auto& refBlob    = refBlobs.begin()->second;

      std::vector<long> vpuOut = perPixelLabel(vpux::toFP32(as<MemoryBlob>(actualBlob)));
      std::vector<long> cpuOut = perPixelLabel(refBlob);

      ASSERT_EQ(vpuOut.size(), cpuOut.size())
                      << "vpuOut.size: " << vpuOut.size() << " "
                      << "cpuOut.size: " << cpuOut.size();

      float meanIoU = calculateMeanIntersectionOverUnion(vpuOut, cpuOut);
      EXPECT_LE(meanIntersectionOverUnionTolerance, meanIoU)
                  << "meanIoU: " << meanIoU << " "
                  << "meanIoUTolerance: " << meanIntersectionOverUnionTolerance;
    };

    const auto init_input = [=](const ConstInputsDataMap& inputs) {
      IE_ASSERT(inputs.size() == 1);
      auto inputTensorDesc = inputs.begin()->second->getTensorDesc();
      registerSingleImage(image, inputs.begin()->first, inputTensorDesc);
    };

    KmbNetworkTestBase::runTest(netDesc, init_input, check);
}