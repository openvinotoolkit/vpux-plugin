#include "include/mcm/base/attribute.hpp"
#include "include/mcm/pass/pass_registry.hpp"
#include "include/mcm/pass/pass_utils.hpp"
#include "include/mcm/op_model.hpp"
#include "include/mcm/computation/model/control_model.hpp"
#include "include/mcm/computation/model/data_model.hpp"
#include "include/mcm/base/exception/runtime_error.hpp"
#include "pass/lp_scheduler/pipeline_transform.hpp"
#include "pass/lp_scheduler/pipeline_chains_transform.hpp"
#include "include/mcm/op_model.hpp"
#include <regex>
#include <iterator>
#include "include/mcm/pass/graphOptimizations/strategy_utils.hpp"
#include "include/mcm/utils/custom_math.hpp"
#include "mcm/utils/custom_strings.hpp"
#include "include/mcm/base/attribute_registry.hpp"
#include "include/mcm/tensor/shape.hpp"
#include <unordered_map> 
#include <iostream>
#include <iomanip>
#include "chrono"

using StrategySet = std::unordered_map<std::string,mv::Attribute>;
typedef mv::scheduler::Pipeline_Chains pipeline_chains_t;
typedef typename pipeline_chains_t::chain_subgraph_t subgraph_t;

static void validateStreamingOverKForChainPipeliningFnc(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);
static void addActivationStreamingFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);

namespace mv
{

    namespace pass
    {
        MV_REGISTER_PASS(ValidateStreamingOverKForChainPipelining)
        .setFunc(validateStreamingOverKForChainPipeliningFnc)
        .setDescription(
            "This pass evaluates and re-calculates streams over K to enable better performance when combined with chained pipelining in the scheduler"
        );

        
    }

   
}

static void addActivationStreamingFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&);

namespace mv
{

    namespace pass
    {

        MV_REGISTER_PASS(AddActivationStreaming)
        .setFunc(addActivationStreamingFcn)
        .setDescription(
            "This pass increases activation streaming over the H dimension for performance."
        );
    }
}

bool validateHStream(mv::Data::OpListIterator opIt, std::string clustering, std::size_t splits, size_t totalClusters)
{
    if( opIt->getOpType() == "Conv" || opIt->getOpType() == "DepthwiseConv")
    {
        if(clustering == "SplitOverH")
        {
            auto weightsShape = opIt->getInputTensor(1)->getShape();
            //Try to guess subtensor height, and avoid situations where kernel is bigger than last workload dimension
            auto outputHeight = opIt->getOutputTensor(0)->getShape()[mv::IO_HEIGHT_DIMENSION];
            auto workloadHeight = ceil((double)outputHeight / (double)(totalClusters * splits));
            if(totalClusters > 1) //last
                workloadHeight = outputHeight - (workloadHeight * (totalClusters-1)); //get remaining height
            if(workloadHeight < weightsShape[mv::KERNEL_HEIGHT])
                return false;
        }
    }

    //check that the inputSize will not be smaller than kernel size
    if (opIt->getOpType() == "MaxPool" ||
        opIt->getOpType() == "Conv" || opIt->getOpType() == "DepthwiseConv")
    {
        uint16_t kernelH;
        std::array<unsigned short, 4> padding;

        size_t originalH = opIt->getOutputTensor(0)->getShape()[mv::IO_HEIGHT_DIMENSION];
        std::vector<std::size_t> newOutputSizes = mv::tileSpatialOutputSize(originalH, splits);

        unsigned short kernelStride;
        if (opIt->hasAttr("stride"))
            kernelStride = opIt->get<std::array<unsigned short, 2>>("stride")[1];
        else
            kernelStride = 1;//fake stride

        if (opIt->hasAttr("padding"))
            padding = opIt->get<std::array<unsigned short, 4>>("padding");
        else
            padding = {0, 0, 0, 0};

        int padStart = 0;
        int padEnd = padding[3];

        if (opIt->hasAttr("kSize"))
        {
            auto kernelShape = opIt->get<std::array<unsigned short, 2>>("kSize");
            kernelH = kernelShape[1];
        }
        else
        {
            auto weightsShape = opIt->getInputTensor(1)->getShape();
            kernelH = weightsShape[mv::KERNEL_HEIGHT];
        }
        int inputSizeForLastSplit = ((newOutputSizes.back() -1) * kernelStride)  -padStart - padEnd + kernelH;
        if ((inputSizeForLastSplit + padEnd) < kernelH)
            return false;
    }

    return true;
}

bool requiresFakeActivationSparsity(mv::Data::OpListIterator opIt, bool enableChannelMajorConv)
{
    if(enableChannelMajorConv && opIt->supportsCMConv())
        return true;

    if(opIt->getOpType() == "MaxPool")
        return true;

    if(opIt->getOpType() == "DepthwiseConv")
        return true;

    return false;
}

std::tuple<size_t, size_t, size_t> getMemorySize(mv::ComputationModel& model,mv::Data::OpListIterator opIt, const mv::Shape& streamConfig)
{
    mv::OpModel om(model);
    auto globalParams = model.getGlobalConfigParams();
    bool enableChannelMajorConv = globalParams->get<bool>("enable_channel_major_conv");
    size_t totalClusters = globalParams->get<int>("Number_of_Clusters");

    auto clustering = opIt->get<std::string>("splitStrategy");
    auto inputSparse = opIt->get<bool>("inputActivationSparsity");
    auto outputSparse = opIt->get<bool>("outputActivationSparsity");
    auto weightsSparse = opIt->get<bool>("weightsSparsity");
    auto fakeSparse = requiresFakeActivationSparsity(opIt, enableChannelMajorConv);
    bool spilling = opIt->get<bool>("goPredictsSpill");
    auto prevOp = om.getSourceOp(opIt->getInputTensor(0));
    bool parentSpilling = prevOp->get<bool>("goPredictsSpill");

    return mv::memorySize(*opIt, totalClusters, enableChannelMajorConv, clustering, inputSparse, outputSparse, weightsSparse, streamConfig,
                        fakeSparse, spilling, parentSpilling);
}

// Gives the minimum number of streams over H to fit this layer, or if no number of streams enable streaming
// (for example, weights don't fit) then return 0
unsigned getMinStreamOverH(mv::ComputationModel& model, mv::Data::OpListIterator opIt)
{
    auto globalParams = model.getGlobalConfigParams();
    size_t totalClusters = globalParams->get<int>("Number_of_Clusters");
    size_t clusterMemory = globalParams->get<unsigned>("cmx");
    auto clusterStrategy = opIt->get<std::string>("splitStrategy");

    size_t input, output, weights;
    // in case initialization in memorySize fails
    input = output = weights = 0;
    std::tie(input, output, weights) = getMemorySize(model, opIt, {1,1,1,1,1});
    auto activationsSize = input + output;
    auto weightsSize = weights;
    double availableMemory = (double) clusterMemory - (double) weightsSize;

    if (availableMemory <= 0) // Weights don't fit, can't stream over H
        return 0;

    // Keep increasing H until we find one big enough to fit, or we run out of H dimension to stream
    auto outputHeight = opIt->getOutputTensor(0)->getShape()[mv::IO_HEIGHT_DIMENSION];

    // Every output slice must have at least one line to compute
    unsigned upperBoundH = outputHeight;
    if(clusterStrategy == "SplitOverH")
    {
        upperBoundH = upperBoundH/totalClusters;
    }

    // Start searching for min stream at naive requirement for splits to fit, rather than 1
    for(unsigned splits = ceil((double)activationsSize/availableMemory); splits <= upperBoundH; splits++)
    {
        auto memFitCheck = getMemorySize(model, opIt, {1,splits,1,1,1});

        if((std::get<0>(memFitCheck) + std::get<1>(memFitCheck) + std::get<2>(memFitCheck) < clusterMemory) &&
                validateHStream(opIt, clusterStrategy, splits, totalClusters))
        {
            return splits;
        }
    }

    return 0;
}

// Note: Validate a stream so that its largest slice fits in CMX and no workload issues
unsigned findOptimalValidStream(mv::ComputationModel& model, mv::Data::OpListIterator opIt, size_t startStream)
{
    auto globalParams = model.getGlobalConfigParams();
    size_t totalClusters = globalParams->get<int>("Number_of_Clusters");
    size_t clusterMemory = globalParams->get<unsigned>("cmx");
    auto clusterStrategy = opIt->get<std::string>("splitStrategy");

    for(unsigned splits = startStream; splits >= 1; splits--)
    {
        auto memFitCheck = getMemorySize(model, opIt, {1,splits,1,1,1});
        if( (std::get<0>(memFitCheck) + std::get<1>(memFitCheck) + std::get<2>(memFitCheck) < clusterMemory) &&
            validateHStream(opIt, clusterStrategy, splits, totalClusters))
                return splits;
    }

    return 1;
}

bool isStreamOptimizable(mv::ComputationModel& model, mv::Data::OpListIterator opIt, std::vector<mv::Element> streaming_strategy)
{
    auto opType = opIt->getOpType();
    if(!(opIt->hasTypeTrait("optimizable") && (opType == "Conv" || opType == "MaxPool" || opType == "DepthwiseConv")))
        return false;

    if (opIt->hasAttr("DilatedSubConv") && (opIt->get<bool>("DilatedSubConv")))
        return false;

    mv::OpModel om(model);
    auto globalParams = model.getGlobalConfigParams();
    auto prevOp = om.getSourceOp(opIt->getInputTensor(0));
    auto prevOpType = prevOp->getOpType();

    // Note: If op is streaming, location is set to DDR even if it can fit. Use the GO decision, 
    // as that will give indication if this will be CMX concatted later
    bool spilling = opIt->get<bool>("goPredictsSpill");
    bool parentSpilling = prevOp->get<bool>("goPredictsSpill");
    
    // Only consider ops that have input tensor in DDR
    // In case of parallel branches, don't trust the GO prediction
    if(!parentSpilling || (prevOpType == "Concat" || prevOpType == "Eltwise"))
        return false;

    auto clusteringStrategy = opIt->get<std::string>("splitStrategy");
    // GO rules disallow SOH with H streaming for convs, accuracy issues
    if(opType == "Conv" && clusteringStrategy == "SplitOverH")
        return false;

    bool isStreamingOtherDim = (streaming_strategy[3].get<int>("K") > 1 ||
                                    streaming_strategy[2].get<int>("C") > 1 ||
                                    streaming_strategy[4].get<int>("N") > 1 ) ? true : false;

    // Note: conservative approach, only consider ops that were already streaming over H
    // This preferences us to only dealing with big tensors - but could still see benefit
    // with tensors that may fit in CMX but are bigger than "overhead size" dmas 
    // (see magic number in K stream pass)
    if(streaming_strategy[1].get<int>("H") == 1)
        isStreamingOtherDim = true; 

    // Note: Special handling here for performance on yolov2, 
    // even if GO said to stream over K, we will still optimize over H if we can
    // Update in GO to give clustering and H stream in this case makese this less relevant for perf
    // but will leave here, as could still help a z-major compilation
    bool firstOp = false;
    if(prevOpType == "Input") firstOp = true;
    if(firstOp && isStreamingOtherDim)
    {
        auto minSplits = getMinStreamOverH(model, opIt);
        if(minSplits == 0) // stream over H alone won't fit
            return false;

        //See accuracy issues with clustering streamed over h and cmx concat?
        if(!spilling)
            return false;

        // In this case, ignore other streams (they will be erased) and just stream over H
        // (if other tests passed are passed of course)
        isStreamingOtherDim = false;
    }

    if(isStreamingOtherDim)
        return false;

    // Note: for SOH to stream over H, must concat in DDR. 
    // This is an un-solvable limitation of the data format, not a workaround.
    if( clusteringStrategy == "SplitOverH" && spilling )
    {
        // If this guy is newly streamed over H and is SOH, make sure it doesn't cmx concat!
        opIt->set<bool>("avoidCmxConcat", true);
        return true;
    }

    // Note: clustering and SOK can stream over H without regard to tensor placement, 
    if( clusteringStrategy == "SplitOverK" || clusteringStrategy == "Clustering")
        return true;

    return false;
}

//ASSUMPTION: the op must fit in cmx with just streaming over H (or no streaming at all)
std::size_t findOptimalStream(mv::ComputationModel& model, mv::Data::OpListIterator opIt, size_t originalHStream)
{
    mv::OpModel om(model);
    auto globalParams = model.getGlobalConfigParams();
    size_t clusterMemory = globalParams->get<unsigned>("cmx");
    size_t totalClusters = globalParams->get<int>("Number_of_Clusters");
    auto clusteringStrategy = opIt->get<std::string>("splitStrategy");

    // Step 1. Decide which tensor will be the benchmark for how many streams we should do
    size_t input, output, weights;
    input = output = weights = 0;
    std::tie(input, output, weights) = getMemorySize(model, opIt, {1,1,1,1,1});

    // Step 2. Calculate a possible number of streams using experimetnally found magic number
    // Idea is, if possible, allow multiple slices to fit in CMX to maximize paths
    size_t magicStreams = std::ceil((2*input + output)/ ((clusterMemory-weights)*0.6));
    if(magicStreams < originalHStream)
        magicStreams = originalHStream; //If GO gave carved it up into smaller pieces, must be for a reason
    else if(magicStreams > originalHStream*3)
        magicStreams = originalHStream*3; // Let's not get crazy with the H streams

    // Can't exceed the max, which ensures at least one line of output for each stream to compute
    size_t maxStreams = opIt->getOutputTensor(0)->getShape()[mv::IO_HEIGHT_DIMENSION];
    if(clusteringStrategy == "SplitOverH") maxStreams = maxStreams/totalClusters;

    size_t proposedStreams = std::min(magicStreams, maxStreams); //will be in range [originalHStream, maxStream]


    // Step 3. Find valid stream starting from proposedStreams and decreasing towards originalHStreams
    // Ensures lines are divided in such a way that it still fits in CMX, no workload issues etc
    auto optStream = findOptimalValidStream(model, opIt, proposedStreams);

    if(optStream < originalHStream)
        return originalHStream; // Never return fewer streams than GO assigned
    
    return optStream;
}

void assignNewSrategies(mv::ComputationModel& model, std::vector<mv::Element>& newStrategies,
                        std::shared_ptr<mv::Element> globalParams, size_t optimalNumberOfKStreams = 0) {
    mv::OpModel om(model);
    mv::DataModel dm(model);

    std::vector<mv::Element> strategyList = globalParams->get<std::vector<mv::Element>>("streaming_strategy");

    for (auto layerNameStrategy : strategyList) {
        std::string nodeName = layerNameStrategy.get<std::string>("name_filter");
        if (nodeName != "Example") {
            auto opIt = om.getOp(nodeName);

            auto streaming_strategy = layerNameStrategy.get<std::vector<mv::Element>>("splits");
            auto compDesc = model.getGlobalConfigParams();
            auto streamingStrategyList = compDesc->get<std::vector<mv::Element>>("streaming_strategy");

            auto newElement = streamingStrategyList[0];
            auto newName = newElement.get<std::string>("name_filter");
            auto newSplits = newElement.get<std::vector<mv::Element>>("splits");
            for (int i = newSplits.size(); i < 5; i++) newSplits.push_back(newSplits[0]);

            if (opIt->hasAttr("optimalNumberOfKStreams")) {
                newElement.set("name_filter", opIt->getName());
                newSplits[0].set<int>("W", streaming_strategy[0].get<int>("W"));
                newSplits[1].set<int>("H", streaming_strategy[1].get<int>("H"));
                newSplits[2].set<int>("C", streaming_strategy[2].get<int>("C"));
                newSplits[3].set<int>("K", opIt->get<unsigned>("optimalNumberOfKStreams"));
                newSplits[4].set<int>("N", streaming_strategy[4].get<int>("N"));
                newElement.set("splits", newSplits);
                newStrategies.push_back(newElement);
            } else {
                newElement.set("name_filter", opIt->getName());
                newSplits[0].set<int>("W", streaming_strategy[0].get<int>("W"));
                newSplits[1].set<int>("H", streaming_strategy[1].get<int>("H"));
                newSplits[2].set<int>("C", streaming_strategy[2].get<int>("C"));
                newSplits[3].set<int>("K", streaming_strategy[3].get<int>("K"));
                newSplits[4].set<int>("N", streaming_strategy[4].get<int>("N"));
                newElement.set("splits", newSplits);
                newStrategies.push_back(newElement);
            }
        }
    }
}

void saveNewStreamingStrategiesToJson(const mv::pass::PassEntry& pass, const mv::Attribute& streamingStrategyElements) {
    pass.log(mv::Logger::MessageType::Debug, "Saving New Streaming Strategies to JSON file");
    std::ofstream jsonOutputFile;
    std::string jsonOutFileName = "./output/mcmCompiler_new_streaming_strategy_output.json";
    jsonOutputFile.open(jsonOutFileName, std::ios::out);
    if (!(jsonOutputFile.is_open()))
        pass.log(mv::Logger::MessageType::Debug, "AddOptimalChainPipeliningStrategies Could not open output file " + jsonOutFileName);

    auto currentTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string timeStamp(ctime(&currentTime));
    if (!timeStamp.empty() && timeStamp[timeStamp.length() - 1] == '\n') timeStamp.erase(timeStamp.length() - 1);

    mv::Element SSA("Streaming strategies generated by mcmCompiler " + timeStamp);
    SSA.set("streaming_strategy", streamingStrategyElements);
    auto jsonSStrategy = SSA.toJSON(true);
  
    jsonOutputFile << jsonSStrategy.stringifyPretty() << "," << std::endl;
    jsonOutputFile.close();
}

std::unordered_map<std::string,StrategySet> getStrategies(mv::Element& passDesc)
{
    std::unordered_map<std::string,StrategySet> layerStrategies;
    auto config = passDesc.get<mv::Element>("ValidateStreamingOverKForChainPipelining");
    auto layerStrategySets  = config.get<std::vector<mv::Element>>("layerStrategies");

    for( auto layerStrategySet : layerStrategySets)
    {
        auto layerName = layerStrategySet.getName();
        auto strategySets = layerStrategySet.get<std::vector<mv::Element>>("strategies");

        for(auto strategySet : strategySets)
        {
            auto strategySetName = strategySet.getName();
            auto strategies = strategySet.get<std::vector<std::string>>("value");

            auto strategyValue = strategySet.get("value");
            layerStrategies[layerName][strategySetName] = strategyValue;

        }
    }
    return layerStrategies;
}

std::vector<mv::Attribute> getStrategiesfromCompilationDescriptor(std::unordered_map<std::string,StrategySet>& strategies, mv::Data::OpListIterator opIt, std::string strategy)
{
    std::vector<mv::Attribute> attr;
    auto layerEntry = strategies.find(opIt->getOpType());
    auto& layerCfg = layerEntry->second;
    auto strategyEntry = layerCfg.find(strategy);

    if(strategyEntry == layerCfg.end())
        throw std::runtime_error("No " + strategy + " strategy specified in the compilation descriptor for this pass");
    

    for (auto elem : strategyEntry->second.get<std::vector<std::string>>())
    {
        attr.push_back(elem);

    }

    return attr;
}

std::map<size_t, size_t> getMinWeightsPerClusterSizePerChain(std::list<subgraph_t>& chainSubgraphs, const mv::pass::PassEntry& pass,
                                         mv::ComputationModel& model, std::map<std::string, size_t>& weightsPerClusterPerOp, FILE *fptr=stdout) {
    mv::OpModel om(model);
    typedef mv::scheduler::Pipeline_Chains pipeline_chains_t;
    typedef typename pipeline_chains_t::chain_subgraph_t subgraph_t;
    std::shared_ptr<mv::Element> globalParams = model.getGlobalConfigParams();
    std::vector<mv::Element> streamingStrategyList = globalParams->get<std::vector<mv::Element>>("streaming_strategy");
    auto multiClusterStrategyList = globalParams->get<std::vector<mv::Element>>("split_strategy");
    std::map<size_t, size_t> minWeightsPerClusterPerChain;
    unsigned chainID = 0;
    std::string clustering;
    std::vector<size_t> streamsSizes;  // Store streamsSizes
    size_t nClusters = globalParams->get<int>("Number_of_Clusters");
    bool clusteringStrategyFound = false;
    bool streamingStrategyFound = false;
    size_t weightsPerCluster = 0;
    bool weightsSparsity = false;
    bool isHStreaming = false;
    bool isKStreaming = false;
    

    //Header for the excel file
    fprintf(fptr,  "%s :  %s :  %s :  %s :  %s :  %s :  %s :  %s", "chainId", "OpName", "kStreaming", "Hstreaming", "MultiCluster", "TotalSize(Inc WT)", "OutputChannels", "WeightsPerCluster(Inc WT)");
    fprintf(fptr, "\n");

    for (subgraph_t chain_subgraph : chainSubgraphs) {
        streamsSizes.clear();  // clear stream sizes for chain i

        // For each operation in chain[i]
        for (auto& op : chain_subgraph.dpu_chain_) {

            mv::Data::OpListIterator opIt = om.getOp(op->getName());

            // If its a conv
            if (opIt->getOpType() == "Conv") 
            {
                // Get the strategy for this conv
                for (auto layerNameStrategy : streamingStrategyList) {
                    std::string nodeName = layerNameStrategy.get<std::string>("name_filter");

                    if (nodeName == op->getName()) {
                        
                        //Get the streaming strategy from graph optimizer
                        auto streaming_strategy = layerNameStrategy.get<std::vector<mv::Element>>("splits");
                        isKStreaming = streaming_strategy[3].get<int>("K") > 1 ? true : false;
                        isHStreaming = streaming_strategy[1].get<int>("H") > 1 ? true : false;

                        
                        // Get the MC strategy from graph optimizer
                        std::string mcStrategy;
                        for (auto s : multiClusterStrategyList) {
                            std::string& name_filter = s.get<std::string>("name_filter");
                            std::regex exp(name_filter);
                            if (std::regex_match(opIt->getName(), exp))
                                mcStrategy = s.get<std::string>("strategy");
                        }

                        // The operation must be already assigned stream over K and SOK and not be sream over H to be considered for a new K stream strategy
                        if (isKStreaming && mcStrategy == "SplitOverK" && !isHStreaming) {

                            if (op->hasAttr("splitStrategy"))
                                clustering = op->get<std::string>("splitStrategy");

                            weightsSparsity = false;
                            if (op->hasAttr("weightsSparsity"))
                                weightsSparsity = op->get<bool>("weightsSparsity");

                            // get the memory size of the streams weights
                            weightsPerCluster = 0;
                            mv::Data::OpListIterator oitr = om.getOp(op->getName());

                            std::size_t input, output = 0;
                            std::tie(input, output, weightsPerCluster) =
                                    getMemorySize(model,oitr,
                                                       {1, (unsigned int)streaming_strategy[1].get<int>("H"),
                                                        (unsigned int)streaming_strategy[2].get<int>("C"),
                                                        (unsigned int)streaming_strategy[3].get<int>("K"),
                                                        (unsigned int)streaming_strategy[4].get<int>("N")});

                            streamsSizes.push_back(weightsPerCluster);
                            
                            weightsPerClusterPerOp.insert({opIt->getName(),weightsPerCluster});
                            
                            size_t alignedFullOutputChannels = mv::round_up( opIt->getOutputTensor(0)->getShape()[mv::IO_CHANNEL_DIMENSION], 16);

                            fprintf(fptr, "%zu : %s :  %zu : %zu  : %s : %zu : %zu : %zu ", chainID,(opIt->getName()).c_str(),  streaming_strategy[3].get<int>("K"),  
                            streaming_strategy[1].get<int>("H"), mcStrategy.c_str(),weightsPerCluster*nClusters*streaming_strategy[3].get<int>("K"),
                            alignedFullOutputChannels, weightsPerCluster);
                            fprintf(fptr, "\n");
                        }
                    }
                }
            }
        }

        //Store the minimum weights per cluster for the chain
        std::sort(streamsSizes.begin(),streamsSizes.end());
        if(!streamsSizes.empty())
            minWeightsPerClusterPerChain.insert({chainID, streamsSizes[0]});
        

        chainID++;
    }
    fprintf(fptr, "End of network analysis\n");
    fprintf(fptr, "\n");

    return minWeightsPerClusterPerChain;
}

void createHeaderforReportFile(FILE *fptr=stdout)
{
    fprintf(fptr,  "%s :  %s :  %s :  %s :  %s :  %s :  %s :  %s :  %s : %s :  %s :  %s", "chainId", "OpName", "Default kStreaming", "Default Hstreaming", "MultiCluster", "TotalSize(Inc WT)", "OutputChannels", "WeightsPerCluster(Inc WT)", "MinWeightsPerClusterInChain", "optimalNumberOfKStreams","maxNumberKStreams", "NewKStreams");
    fprintf(fptr, "\n");
}

void printInfoToFile(unsigned chainID, std::string opName, int kStreaming, int hStreaming,
                     std::string multiclusterStrategy, size_t fullweightsSize, size_t alignedFullOutputChannels,
                     size_t weightsPerClusterPerOp, size_t minWeightsPerClusterPerChain, double optimalNumberOfKStreams,
                     double maxpossibleStreams, double newKStreams, FILE* fptr = stdout) {
    fprintf(fptr,
            "%zu : %s :  %zu : %zu  : %s : %zu : %zu : %zu : %zu : %.1f : %.1f : "
            "%.1f ",
            chainID, opName.c_str(), kStreaming, hStreaming, multiclusterStrategy.c_str(), fullweightsSize,
            alignedFullOutputChannels, weightsPerClusterPerOp, minWeightsPerClusterPerChain, optimalNumberOfKStreams,
            maxpossibleStreams, newKStreams);
    fprintf(fptr, "\n");
}

 // Get the strategy assigned by GO for this conv
std::pair<std::vector<mv::Element>, std::string> getGraphOptimizerAssignedStategies(
        std::vector<mv::Element>& streamingStrategyList, std::vector<mv::Element>& multiClusterStrategyList,
        mv::ComputationModel& model, std::string opName) {

    mv::OpModel om(model);
    mv::Data::OpListIterator opIt;
    std::vector<mv::Element> streaming_strategy;
    std::string mcStrategy;
    std::pair<std::vector<mv::Element>, std::string> graphOptimizerAssignedStategies;

    for (auto layerNameStrategy : streamingStrategyList) {
        std::string nodeName = layerNameStrategy.get<std::string>("name_filter");

        if (nodeName == opName) {
            opIt = om.getOp(nodeName);

            // Get the streaming strategy assigned by graph optimizer
            streaming_strategy = layerNameStrategy.get<std::vector<mv::Element>>("splits");
            graphOptimizerAssignedStategies.first = streaming_strategy;

            // Get the MC strategy assigned by graph optimizer
            for (auto s : multiClusterStrategyList) {
                std::string& name_filter = s.get<std::string>("name_filter");
                std::regex exp(name_filter);
                if (std::regex_match(opIt->getName(), exp))
                    mcStrategy = s.get<std::string>("strategy");
                graphOptimizerAssignedStategies.second = mcStrategy;
            }
            break;
        }
    }
    return graphOptimizerAssignedStategies;
}

std::pair<size_t, double> fullWeightsSizeForOpandOptimalKStreaming(std::string multiclusterStrategy, size_t weightsPerClusterforOp, size_t minWeightsPerClusterPerChain, bool isKStreaming, int numberOfkStreams, int nClusters) 
{
    size_t fullWeightsSize = 0;
    size_t optimalNumberOfKStreams =0;
    std::pair<size_t, double> toReturn;
    size_t minWeightsPerClusterPerChainConstant = 66560; // This value was derived from emperical testing 

    // Calculate the optimal number of K streams
    // First calculate the full weight size
    // Then divide by the minStreamSize * nclusters to get the optimal K streams
    if (isKStreaming && multiclusterStrategy == "SplitOverK") {

        fullWeightsSize = weightsPerClusterforOp * nClusters * numberOfkStreams;

        if(minWeightsPerClusterPerChain <= minWeightsPerClusterPerChainConstant)
            minWeightsPerClusterPerChain = minWeightsPerClusterPerChainConstant;

        optimalNumberOfKStreams = std::round(fullWeightsSize / (minWeightsPerClusterPerChain * nClusters));
       
    } 
    else if (isKStreaming && multiclusterStrategy == "Clustering")
     {
        fullWeightsSize = weightsPerClusterforOp * numberOfkStreams;

        if(minWeightsPerClusterPerChain <= minWeightsPerClusterPerChainConstant)
            minWeightsPerClusterPerChain = minWeightsPerClusterPerChainConstant;

        optimalNumberOfKStreams = std::round(fullWeightsSize / minWeightsPerClusterPerChain);
        
    } 
    else if (multiclusterStrategy == "SplitOverK" && !isKStreaming) 
    {
        fullWeightsSize = weightsPerClusterforOp * nClusters;

        if(minWeightsPerClusterPerChain <= minWeightsPerClusterPerChainConstant)
            minWeightsPerClusterPerChain = minWeightsPerClusterPerChainConstant;
            
        optimalNumberOfKStreams = std::round(fullWeightsSize / (minWeightsPerClusterPerChain * nClusters));
        
    }

    // Ensure K streams is never 0
    if (optimalNumberOfKStreams < 1)
        optimalNumberOfKStreams = 1;
    
    toReturn.first = fullWeightsSize;
    toReturn.second = optimalNumberOfKStreams;

    return toReturn;
}

 auto checkIfStrategyIsInCompilationDescriptor = [](std::vector<mv::Attribute>& vec, const std::string& str) -> bool {
                    for (const auto elem : vec)
                        if (str == elem.get<std::string>())
                            return true;
                    return false;
                };

 void evaluateAndAssignStrategies(std::list<subgraph_t>& chainSubgraphs, const mv::pass::PassEntry& pass,
                                  mv::ComputationModel& model,
                                  std::map<size_t, size_t>& minWeightsPerClusterPerChain,
                                  std::map<std::string, size_t>& weightsPerClusterPerOp, FILE* fptr = stdout) {
     unsigned chainID = 0;
     mv::OpModel om(model);
     std::shared_ptr<mv::Element> globalParams = model.getGlobalConfigParams();
     size_t nClusters = globalParams->get<int>("Number_of_Clusters");
     std::vector<mv::Element> strategyList = globalParams->get<std::vector<mv::Element>>("streaming_strategy");
     std::vector<mv::Element> streamingStrategyList = globalParams->get<std::vector<mv::Element>>("streaming_strategy");
     std::vector<mv::Element> multiClusterStrategyList = globalParams->get<std::vector<mv::Element>>("split_strategy");
     std::shared_ptr<mv::Element> compDesc = model.getGlobalConfigParams();
     std::map<std::string, size_t> minOutputChannels = {{"SplitOverK", 64},
                                                        {"Clustering", 16},
                                                        {"SplitOverH", 16},
                                                        {"HKSwitch", 16}};

     std::string clustering;
     size_t fullweightsSize = 0;
     double maxpossibleStreams = 0.0;
     double optimalNumberOfKStreams = 0;
     std::vector<mv::Element> graphOptimizerStreamingStrategy;
     std::vector<mv::Element> overWrittenStreamingStrategies;
     std::vector<mv::Element> allStreamingStrategies;
     bool clusteringStrategyFoundinCompilationDescriptor = false;
     bool streamingStrategyFoundinCompilationDescriptor = false;
     std::pair<size_t, double> fullWeightsSizeOptimalKStreaming = {};
     std::size_t minStreamSize = 0;
     size_t alignedFullOutputChannels = 0;
     size_t weightsPerCluster = 0;
     size_t fullWeightsSize = 0;
     size_t minWeightsPerClusterPerChainConstant = 66560; // This value was derived from emperical testing 

     // create header for report file
     createHeaderforReportFile(fptr);

     for (subgraph_t chain_subgraph : chainSubgraphs) {
         for (auto& op : chain_subgraph.dpu_chain_) {
             mv::Data::OpListIterator opIt = om.getOp(op->getName());

             // We only stream Convs more
             if (opIt->getOpType() == "Conv") {
                 optimalNumberOfKStreams = 0;

                 // Get the strategy assigned by GO for this operation
                 auto graphOptimizerAssignedStategies = getGraphOptimizerAssignedStategies(
                         streamingStrategyList, multiClusterStrategyList, model, opIt->getName());
                 
                 // Get the streaming and multicluster strategy assigned by GO for this operation 
                 auto graphOptimizerStreamingStrategy = graphOptimizerAssignedStategies.first;
                 auto graphOptimizerMultiClusterStrategy = graphOptimizerAssignedStategies.second;

                 bool isKStreaming = graphOptimizerStreamingStrategy[3].get<int>("K") > 1 ? true : false;
                 bool isHStreaming = graphOptimizerStreamingStrategy[1].get<int>("H") > 1 ? true : false;

                 // Get the output channels to determine the max possible K streams so we know the limit
                 alignedFullOutputChannels = mv::round_up(opIt->getOutputTensor(0)->getShape()[mv::IO_CHANNEL_DIMENSION], 16);

                 // Calculate the max possible K streams based on the multi-cluster strategy
                 maxpossibleStreams = floor(alignedFullOutputChannels / minOutputChannels[graphOptimizerMultiClusterStrategy]);
                
                 // Get the weights per cluster for this op
                 weightsPerCluster = weightsPerClusterPerOp.find(opIt->getName())->second;

                  // The operation must be already assigned stream over K and SOK and not be sream over H to be considered for a new K stream strategy 
                  if (isKStreaming && graphOptimizerMultiClusterStrategy == "SplitOverK" && !isHStreaming) {
                    
                    fullWeightsSizeOptimalKStreaming = {0,0};
                    if(minWeightsPerClusterPerChain[chainID] > 0)
                        fullWeightsSizeOptimalKStreaming = fullWeightsSizeForOpandOptimalKStreaming(graphOptimizerMultiClusterStrategy, weightsPerCluster, minWeightsPerClusterPerChain[chainID], isKStreaming,graphOptimizerStreamingStrategy[3].get<int>("K"), nClusters);
                    
                    fullWeightsSize = fullWeightsSizeOptimalKStreaming.first;
                    optimalNumberOfKStreams = fullWeightsSizeOptimalKStreaming.second;

                    // Assign the new streaming strategies
                    // The optimalNumberOfKStreams must be > 0, less than the max possible K streams and must not decrease the K streams assinged from the GO
                     if ((optimalNumberOfKStreams > 0) && (optimalNumberOfKStreams <= maxpossibleStreams) && (optimalNumberOfKStreams > graphOptimizerStreamingStrategy[3].get<int>("K"))) {

                        if(minWeightsPerClusterPerChain[chainID] < minWeightsPerClusterPerChainConstant)
                            minWeightsPerClusterPerChain[chainID] = minWeightsPerClusterPerChainConstant;

                         printInfoToFile(chainID, (opIt->getName()).c_str(), graphOptimizerStreamingStrategy[3].get<int>("K"),
                                         graphOptimizerStreamingStrategy[1].get<int>("H"), graphOptimizerMultiClusterStrategy.c_str(),
                                         fullWeightsSize, alignedFullOutputChannels,
                                         weightsPerClusterPerOp.find(opIt->getName())->second,
                                         minWeightsPerClusterPerChain[chainID], optimalNumberOfKStreams,
                                         maxpossibleStreams, optimalNumberOfKStreams, fptr);

                         opIt->set<unsigned>("optimalNumberOfKStreams", optimalNumberOfKStreams);

                     } 
                     // Else assign the max possible K streams for the layer
                     else if (optimalNumberOfKStreams > maxpossibleStreams) {

                        if(minWeightsPerClusterPerChain[chainID] < minWeightsPerClusterPerChainConstant)
                            minWeightsPerClusterPerChain[chainID] = minWeightsPerClusterPerChainConstant;
                         printInfoToFile(chainID, (opIt->getName()).c_str(), graphOptimizerStreamingStrategy[3].get<int>("K"),
                                         graphOptimizerStreamingStrategy[1].get<int>("H"), graphOptimizerMultiClusterStrategy.c_str(),
                                         fullWeightsSize, alignedFullOutputChannels,
                                         weightsPerClusterPerOp.find(opIt->getName())->second,
                                          minWeightsPerClusterPerChain[chainID], optimalNumberOfKStreams,
                                         maxpossibleStreams, maxpossibleStreams, fptr);
                         opIt->set<unsigned>("optimalNumberOfKStreams", maxpossibleStreams);
                     }
                 }
             }
         }
         chainID++;
     }
 }

 void validateStreamingOverKForChainPipeliningFnc(const mv::pass::PassEntry& pass, mv::ComputationModel& model,
                                             mv::TargetDescriptor&, mv::Element& passDesc, mv::Element&) {
     mv::OpModel om(model);
     typedef mv::scheduler::Pipeline_Chains pipeline_chains_t;
     typedef typename pipeline_chains_t::chain_subgraph_t subgraph_t;
     std::shared_ptr<mv::Element> globalParams = model.getGlobalConfigParams();
     std::vector<mv::Element> strategyList = globalParams->get<std::vector<mv::Element>>("streaming_strategy");
     auto multiClusterStrategyList = globalParams->get<std::vector<mv::Element>>("split_strategy");
     auto compDesc = model.getGlobalConfigParams();
     std::vector<mv::Element> newStreamingStrategies;
     std::map<std::string, size_t> weightsPerClusterPerOp;
     FILE* network_report_fptr = fopen("validateStreamingOverKForChainPipelining.txt", "w");

    if (!network_report_fptr)
        throw std::runtime_error("Cannot open validateStreamingOverKForChainPipelining.txt for write");
     
     pipeline_chains_t pipeliner(om);
     size_t pipeline_stages = 0UL;
     if (passDesc.hasAttr("select_stages")) {
         pipeline_stages = (size_t)passDesc.get<int>("select_stages");
     }

     // Step 1: Get the subgraph chains
     auto chainSubgraphs = pipeliner.get_chain_subgraphs(pipeline_stages);

     // Step 2: Get the min weights per cluster in a chain
     auto minWeightsPerClusterPerChain = getMinWeightsPerClusterSizePerChain(
             chainSubgraphs, pass, model, weightsPerClusterPerOp, network_report_fptr);

     // Step 3: Calculate more optimal streaming over K strategies
     if(!minWeightsPerClusterPerChain.empty())
     {
        evaluateAndAssignStrategies(chainSubgraphs, pass, model, minWeightsPerClusterPerChain,
                                 weightsPerClusterPerOp, network_report_fptr);

        // Step4: Assign the new strategies
        assignNewSrategies(model, newStreamingStrategies, globalParams);

        // Step5: Save the new strategies
        compDesc->set("streaming_strategy", newStreamingStrategies);
        saveNewStreamingStrategiesToJson(pass, newStreamingStrategies);
     }
 }

// Note: The idea of this pass is to increase streaming over the height dimension in specific cases
// to increase performance. Specifically, we consider DPU tasks (convs, dw, maxpool) that have their
// input tensor in DDR. Performance increase results because smaller DMA of input slices leads to 
// earlier start to compute, and the resulting smaller pieces are often easier for the scheduler 
// to schedule efficiently.
//
// Side Note: There are several reasons an input tensor might be in DDR, it could be the network
// input, or a spilled activation due to tensor size or need to change clustering strategy. In this
// pass we don't care why the tensor is in DDR, we just use the GO pass' prediction for where the 
// tensor will be located. We skip extra streams in the case that the GO can't predict tensor location
// such as after an explicit concat (could be CMXed later). For simplicity, we also only consider ops
// that were already streaming over H, but this pass could be extended to consider non-streaming ops.
void addActivationStreamingFcn(const mv::pass::PassEntry& pass, mv::ComputationModel& model, mv::TargetDescriptor&, mv::Element&, mv::Element&)
{
    mv::OpModel om(model);
    auto globalParams = model.getGlobalConfigParams();
    if (!globalParams->hasAttr("split_strategy"))
    {
        pass.log(mv::Logger::MessageType::Debug, "No custom splitting strategy provided, exiting...");
        return;
    }
    // This pass will work for ZM when input not a dag issue resolved
    bool enableChannelMajorConv = globalParams->get<bool>("enable_channel_major_conv");
    if(!enableChannelMajorConv) return;

    auto streamingStrategies = globalParams->get<std::vector<mv::Element>>("streaming_strategy");
    std::vector<mv::Element> newStreamingStrategies;

    for(auto streamingStrategy : streamingStrategies)
    {
        std::string nodeName = streamingStrategy.get<std::string>("name_filter");
        // In case of manual strategy
        if (!om.checkOp(nodeName))
            continue;

        auto opIt = om.getOp(nodeName);
        bool updated = false;
        auto streams = streamingStrategy.get<std::vector<mv::Element>>("splits");

        // Step 0. Decide if we can insert activation streaming for this op
        if(isStreamOptimizable(model, opIt, streams))
        {
            size_t originalHStream = streams[1].get<int>("H");
            
            // Step 1. Choose optimal stream over H number for this op
            auto newHstream = findOptimalStream(model, opIt, originalHStream);

            // Step 2. Create the new streaming strategy and add to vector
            if(newHstream != originalHStream)
            {
                pass.log(mv::Logger::MessageType::Debug, "Op " + nodeName + " H stream strategy updated");
                mv::Element element(""); 
                element.set("name_filter",nodeName);

                std::vector<mv::Element> copySplits(streams.size(),mv::Element(""));
                copySplits[0].set<int>("W", 1);
                copySplits[1].set<int>("H", newHstream);
                copySplits[2].set<int>("C", 1);
                copySplits[3].set<int>("K", 1);
                copySplits[4].set<int>("N", 1);
                element.set("splits",copySplits);

                newStreamingStrategies.emplace_back(std::move(element));
                updated = true;
            }
        }

        //Step 3. Keep streaming stratgies that don't change too!
        if(!updated)
        {
            newStreamingStrategies.emplace_back(std::move(streamingStrategy));
        }
    }

    //Step 4. Save the streaming strategies into the compilation descriptor to be read by the streaming pass
    globalParams->set<std::vector<mv::Element>>("streaming_strategy", newStreamingStrategies);
    saveNewStreamingStrategiesToJson(pass, newStreamingStrategies);
}