//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//

#if defined(__arm__) || defined(__aarch64__)

#include "kmb_preproc_pool.hpp"

#include "vpux/utils/IE/itt.hpp"

#include <mutex>
#include <sstream>

#include "kmb_preproc_gapi.hpp"

// clang-format off
namespace InferenceEngine {
namespace KmbPreproc {

Preprocessor::Preprocessor(unsigned int shaveFirst, unsigned int shaveLast, unsigned int lpi, Path ppPath)
    : _preproc(new PreprocEngine(shaveFirst, shaveLast, lpi, ppPath)) {}

Preprocessor::~Preprocessor() = default;

void Preprocessor::execDataPreprocessing(const PreprocTask& t, const int deviceId) {
    OV_ITT_SCOPED_TASK(vpux::itt::domains::VPUXPlugin, "Preprocessor::execDataPreprocessing");
    IE_ASSERT(t.inputs.size() == 1);
    for (auto& input : t.inputs) {
        const auto& blobName = input.first;
        auto it = t.preprocData.find(blobName);
        if (it != t.preprocData.end()) {
            const auto& preprocInfo = t.networkInputs.at(blobName)->getPreProcess();
            _preproc->preproc(t.preprocData.at(blobName)->getRoiBlob(),
                              input.second,
                              preprocInfo.getResizeAlgorithm(),
                              preprocInfo.getColorFormat(),
                              t.out_format,
                              deviceId);
        }
    }
}

PreprocessorPool::PreprocessorPool(
    unsigned int shaveFirst, unsigned int shaveLast, unsigned int nPipelines, unsigned int lpi, Path ppPath)
    : _preprocs(nPipelines), _numberOfShaves(shaveLast + 1 - shaveFirst) {
    IE_ASSERT(ppPath == Path::SIPP || ppPath == Path::M2I || ppPath == Path::SHAVE_ONLY_M2I);


    auto shavesPerPipeline = _numberOfShaves / nPipelines;
    IE_ASSERT(shavesPerPipeline > 0);
    for (unsigned int i = 0; i < nPipelines; i++) {
        unsigned int sf = shaveFirst + i * shavesPerPipeline;
        unsigned int sl = sf + shavesPerPipeline - 1;
        _preprocs[i].reset(new Preprocessor(sf, sl, lpi, ppPath));
        _free_preprocs.push(_preprocs[i].get());
    }
}

void PreprocessorPool::execDataPreprocessing(const PreprocTask& task, const int deviceId) {
    OV_ITT_SCOPED_TASK(vpux::itt::domains::VPUXPlugin, "PreprocessorPool::execDataPreprocessing");
    std::unique_lock<std::mutex> lock(_mutex);
    if (_free_preprocs.empty()) {
        _free_cond.wait(lock, [&]() {
            return !_free_preprocs.empty();
        });
    }
    auto preproc = _free_preprocs.front();
    _free_preprocs.pop();
    lock.unlock();

    preproc->execDataPreprocessing(task, deviceId);

    lock.lock();
    _free_preprocs.push(preproc);
    lock.unlock();
    _free_cond.notify_one();
}

unsigned int PreprocessorPool::getNumberOfShaves() const { return _numberOfShaves; }

PreprocessorPool& PreprocPool::getPool(
    const std::string& preprocPoolId, unsigned int numberOfShaves, unsigned int lpi, unsigned int numberOfPipes, Path ppPath) {
    std::unique_lock<std::mutex> lock(_mutex);
    if (_preprocPools.count(preprocPoolId) == 0) {
        //First SHAVE number is actually obsolete parameter.
        //Resource manager can ignore it and choose first SHAVE number by its own.
        //Anyway some valid value should be provided to sippCreatePipeline by plugin.
        //Following check must succeed: lastShave < 16. lastShave = firstShave + numberOfShaves - 1;
        //firstShave must be a positive integer from `0` to `12`.
        //The number of shaves is `16`, maximal number of pipelines is `2`,
        //maximal number of shaves per pipeline is `2`, which makes `16 - 2 * 2 = 12`.
        auto firstFreeShave = 0;
        for (const auto& pool : _preprocPools) {
            firstFreeShave += pool.second->getNumberOfShaves();
        }
        // FIXME?
        // SIPP Preprocessing SHAVEs assigned to M2I will also affect this shave distribution!
        auto lastShave = firstFreeShave + numberOfShaves - 1;

        IE_ASSERT(lastShave < 16) << "PreprocPool error: attempt to execute preprocessing on " << firstFreeShave
                                  << "-" << lastShave << " SHAVEs, last SHAVE index must be less than 16";

        _preprocPools[preprocPoolId].reset(new PreprocessorPool(firstFreeShave, lastShave, numberOfPipes, lpi, ppPath));
    }
    lock.unlock();

    return *_preprocPools[preprocPoolId];
}

void PreprocPool::execDataPreprocessing(
    const PreprocTask& task, unsigned int numberOfShaves, unsigned int lpi, unsigned int numberOfPipes, Path ppPath, const std::string& preprocPoolId,
    const int deviceId) {
    if (task.inputs.empty()) {
        IE_THROW() << "Inputs are empty.";
    }
    auto dims = task.inputs.begin()->second->getTensorDesc().getDims();
    getPool(preprocPoolId, numberOfShaves, lpi, numberOfPipes, ppPath).execDataPreprocessing(task, deviceId);
}

PreprocPool& preprocPool() {
    static PreprocPool pool;
    return pool;
}

}  // namespace KmbPreproc
}  // namespace InferenceEngine
// clang-format on
#endif  // #ifdef defined(__arm__) || defined(__aarch64__)
