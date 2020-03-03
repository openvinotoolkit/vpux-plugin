#include "include/mcm/pass/pass_registry.hpp"
#include "include/mcm/op_model.hpp"
#include "include/mcm/computation/model/data_model.hpp"
#include "include/mcm/tensor/math.hpp"
#include "include/mcm/utils/custom_strings.hpp"
#include "include/mcm/utils/warning_manager.hpp"
#include "include/mcm/pass/pass_utils.hpp"
#include <functional>

static const uint8_t ADD_INPUT_FLOWS = 2;

mv::Data::OpListIterator portDepthwise(mv::ComputationModel& model, mv::Data::TensorIterator inputTensor, mv::Data::OpListIterator task,
                        mv::QuantizationParams quantParams)
{
    mv::OpModel om(model);
    mv::Data::TensorIterator weights;
    std::vector<int64_t> zp = {255};
    std::vector<double> scale = {0.00392156862745098};
    std::vector<double> min = {-std::numeric_limits<double>::infinity()};
    std::vector<double> max = {std::numeric_limits<double>::infinity()};
    auto inputShape = inputTensor->getShape();
    std::string name = task->getName();

    mv::QuantizationParams weightsQuantParams(zp, scale, min, max);
    std::vector<int64_t> weightsData(inputShape[mv::IO_CHANNEL_DIMENSION], 0);
    weights = om.constantInt(weightsData,
                        {1, 1, inputShape[mv::IO_CHANNEL_DIMENSION], 1},
                        mv::DType("UInt8"),
                        mv::Order(mv::Order::getRowMajorID(4)),
                        weightsQuantParams);
    auto depthwise_conv = om.depthwiseConv(inputTensor, weights, {1,1}, {0,0,0,0}, 1,
                                           mv::DType("UInt8"), quantParams, name + "_PPEDepthwiseConv");
    auto depthwise_convOp = om.getSourceOp(depthwise_conv);
    auto weightsOp = om.getSourceOp(weights);
    unsigned currentOpId = task->get<unsigned>("opId");
    weightsOp->set<unsigned>("opId", currentOpId);
    depthwise_convOp->set<unsigned>("opId", currentOpId);

    return depthwise_convOp;
}

mv::Data::OpListIterator portConv(mv::ComputationModel& model, mv::Data::OpListIterator task,
                mv::Data::OpListIterator previousConv, uint8_t branch)
{
    mv::OpModel om(model);
    mv::DataModel dm(model);
    mv::Data::TensorIterator weightsTensor, biasTensor, inputTensor;
    inputTensor = previousConv->getInputTensor(0);
    bool hasBias = false;
    if (previousConv->getOpType() == "Conv")
    {
        weightsTensor = previousConv->getInputTensor(1);
        hasBias = previousConv->hasAttr("bias");
        if (hasBias)
            biasTensor = dm.getTensor(previousConv->get<std::string>("bias"));
    }
    std::vector<double> min = {-std::numeric_limits<double>::infinity()};
    std::vector<double> max = {std::numeric_limits<double>::infinity()};
    double max_inf = std::numeric_limits<double>::infinity();
    double min_inf = -std::numeric_limits<double>::infinity();
    auto inputShape = inputTensor->getShape();
    std::string name = task->getName();
    std::vector<double> scale(1, 1.0);
    std::vector<int64_t> zp(1, 0);
    auto kernelShape = weightsTensor->getShape();
    mv::QuantizationParams quantParams = task->get<mv::QuantizationParams>("quantParams");

    double alpha = task->get<double>("alpha");
    std::vector<int64_t> weightsData;
    //Note: Quantization of weights
    if (branch == 0)
    {
        scale = weightsTensor->get<mv::QuantizationParams>("quantParams").getScale();
        zp = weightsTensor->get<mv::QuantizationParams>("quantParams").getZeroPoint();
        weightsData = weightsTensor->getIntData();
    }
    else
    {
        int64_t new_zero_point;
        double new_scale;
        std::vector <double> old_scale = weightsTensor->get<mv::QuantizationParams>("quantParams").getScale();
        std::vector <int64_t> old_zp = weightsTensor->get<mv::QuantizationParams>("quantParams").getZeroPoint();
        //NOTE: The order of the functions was moved in order to validate the per tensor quantization in weights first
//        updateInfMinMaxPerChannel(weightsTensor);
        updateInfMinMaxPerTensor(weightsTensor);
        min.clear();
        max.clear();
        scale.clear();
        zp.clear();
        min.push_back(-alpha * weightsTensor->get<mv::QuantizationParams>("quantParams").getMax()[0]);
        max.push_back(-alpha * weightsTensor->get<mv::QuantizationParams>("quantParams").getMin()[0]);
        calcZeroPointAndScalePerTensor(max[0], min[0], new_scale, new_zero_point);

        for (size_t k = 0; k < kernelShape[mv::KERNEL_OUTPUT_CHANNELS]; k++)
        {
            //compute maximum, minimum with FUNCTIONS
//            min.push_back(-alpha * weightsTensor->get<mv::QuantizationParams>("quantParams").getMax()[0]);
//            max.push_back(-alpha * weightsTensor->get<mv::QuantizationParams>("quantParams").getMin()[0]);

            for (size_t c = 0; c < kernelShape[mv::KERNEL_INPUT_CHANNELS]; c++)
            {
                for (size_t h = 0; h < kernelShape[mv::KERNEL_HEIGHT]; h++)
                {
                    for (size_t w = 0; w < kernelShape[mv::KERNEL_WIDTH]; w++)
                    {
                        auto currWeight = (int64_t)weightsTensor->at({w,h,c,k});
                        double real_weight = ((int64_t)currWeight - old_zp[0]) * old_scale[0];
                        real_weight = -alpha * real_weight;
                        auto newQuantizedValue = std::round(real_weight/new_scale) + new_zero_point;
                        if (newQuantizedValue > 255)
                            newQuantizedValue = 255;
                        else if (newQuantizedValue < 0)
                            newQuantizedValue = 0;
                        weightsData.push_back(newQuantizedValue);
                    }
                }
            }
        }
        scale.push_back(new_scale);
        zp.push_back(new_zero_point);
    }
    mv::QuantizationParams weightsQuantParams(zp, scale, min, max);

    auto weights = om.constantInt(weightsData,
                        {weightsTensor->getShape()[mv::KERNEL_WIDTH], weightsTensor->getShape()[mv::KERNEL_HEIGHT],
                        weightsTensor->getShape()[mv::KERNEL_INPUT_CHANNELS], weightsTensor->getShape()[mv::KERNEL_OUTPUT_CHANNELS]},
                        mv::DType("UInt8"),
                        mv::Order(mv::Order::getRowMajorID(4)),
                        weightsQuantParams);

//    if (branch == 1)
//    {
//        updateInfMinMaxPerTensor(task->getOutputTensor(0));
//        double_t minConv = task->getOutputTensor(0)->get<mv::QuantizationParams>("quantParams").getMin()[0];
//        double_t maxConv = task->getOutputTensor(0)->get<mv::QuantizationParams>("quantParams").getMax()[0];
//        int64_t out_zero_point;
//        double out_scale;
//        double_t maxConvNew, minConvNew;
//        minConvNew = minConv/alpha;
//        maxConvNew = maxConv * alpha;
//        calcZeroPointAndScalePerTensor(maxConvNew, minConvNew, out_scale, out_zero_point);
//        quantParams = {{out_zero_point},{out_scale},{minConvNew},{maxConvNew}};
//    }
    auto conv = om.conv(inputTensor, weights, previousConv->get<std::array<unsigned short, 2>>("stride"),
                             previousConv->get<std::array<unsigned short, 4>>("padding"), previousConv->get<unsigned>("dilationFactor"),
                             previousConv->get<unsigned>("group"),
                        mv::DType("UInt8"), quantParams,
                        name + std::to_string(branch) + "_PPEConv");
    auto convOp = om.getSourceOp(conv);

    if (hasBias)
    {
        if (branch == 0)
        {
            //NOTE: Normally should use the scale computation but is unused
            biasTensor->set<mv::QuantizationParams>("quantParams", {{0},{1.0},{min_inf},{max_inf}});
            om.addAttr(convOp, "bias", biasTensor->getName());
        }
        else
        {
            double biasOldScale, real_bias;
            std::vector <int64_t> biasData;
            std::vector <double> biasScale;
            for (size_t k = 0; k < kernelShape[mv::KERNEL_OUTPUT_CHANNELS]; k++)
            {
                biasOldScale = weightsTensor->get<mv::QuantizationParams>("quantParams").getScale()[k]
                        * inputTensor->get<mv::QuantizationParams>("quantParams").getScale()[0];
                real_bias = ((int64_t) biasTensor->at(k)) * biasOldScale;
                real_bias = -alpha * real_bias;
                auto newQuantizedValue = std::round(real_bias
                                               /(scale[k] * inputTensor->get<mv::QuantizationParams>("quantParams").getScale()[0]));
                if (newQuantizedValue > 2147483647)
                    newQuantizedValue = 2147483647;
                else if (newQuantizedValue < -2147483648)
                    newQuantizedValue = -2147483648;
                biasData.push_back(newQuantizedValue);
                biasScale.push_back(scale[k] * inputTensor->get<mv::QuantizationParams>("quantParams").getScale()[0]);
            }
            mv::QuantizationParams biasQuant = mv::QuantizationParams({{0},biasScale,
                                        {-min_inf},{max_inf}});

            mv::Data::TensorIterator branch2biasTensor;
            std::string branch2biasTensorName = mv::createBiasName(convOp->getName());
            branch2biasTensor = dm.defineTensor(mv::Tensor(branch2biasTensorName, biasTensor->getShape(),
                            biasTensor->getDType(), biasTensor->getOrder(), biasData, biasQuant));
            om.addAttr(convOp, "bias", branch2biasTensorName);
        }
    }

    auto weightsOp = om.getSourceOp(weights);
    unsigned currentOpId = task->get<unsigned>("opId");
    weightsOp->set<unsigned>("opId", currentOpId);
    convOp->set<unsigned>("opId", currentOpId);

    return convOp;
}

mv::Data::OpListIterator portRelu(mv::ComputationModel& model, mv::Data::TensorIterator inputTensor, mv::Data::OpListIterator task, mv::QuantizationParams quantParams)
{
    mv::OpModel om(model);
    std::string name = task->getName();
    auto relu0 = om.relu(inputTensor, mv::DType("UInt8"), quantParams, name + "RELU");
    auto relu_op = om.getSourceOp(relu0);
    unsigned currentOpId = task->get<unsigned>("opId");
    relu_op->set<unsigned>("opId", currentOpId);
    return relu_op;
}

mv::Data::OpListIterator portAdd(mv::ComputationModel& model, std::vector <mv::Data::TensorIterator> inputs ,mv::Data::OpListIterator task, mv::QuantizationParams quantParams)
{
    mv::OpModel om(model);
    std::string name = task->getName();
    auto eltwise = om.eltwise(inputs, "Add", mv::DType("UInt8"), quantParams, name + "PPEeltwise");
    auto eltwise_op = om.getSourceOp(eltwise);
    unsigned currentOpId = task->get<unsigned>("opId");
    eltwise_op->set<unsigned>("opId", currentOpId);
    return eltwise_op;
}

static std::vector<mv::Data::OpListIterator> findSinkLayers(mv::DataModel &dataModel, const mv::Data::TensorIterator &tensor)
{
    std::vector<mv::Data::OpListIterator> sinkOperations;
    auto flowsNames = (tensor)->get<std::set<std::string>>("flows");
    for(auto flowName : flowsNames)
    {
        auto df = dataModel.getDataFlow(flowName);
        sinkOperations.push_back(df.sink());
    }
    return sinkOperations;
}

void provideAccuracyinPPEs(mv::ComputationModel& model)
{
    mv::OpModel om(model);
    mv::DataModel dm(model);

    std::shared_ptr<mv::Element> globalParams = model.getGlobalConfigParams();
    bool PPEAccuracy = globalParams->hasAttr("PPEAccuracy") ? globalParams->get<bool>("PPEAccuracy") : false;
    if (!PPEAccuracy)
        return;
    auto leakyRelus = om.getOps("LeakyRelu");
    auto leakyRelu = leakyRelus.begin();

    while (leakyRelu != leakyRelus.end())
    {
        auto leakyReluOp = *leakyRelu;
        auto parentOp = om.getSourceOp(leakyReluOp->getInputTensor(0));
        auto leakyInputTensor = leakyReluOp->getInputTensor(0);
        auto leakyOutputTensor = leakyReluOp->getOutputTensor(0);
        auto leakyReluQuantParams = leakyReluOp->get<mv::QuantizationParams>("quantParams");
        uint8_t branch = 0;
        std::vector<mv::Data::OpListIterator> sinkOperators = findSinkLayers(dm, leakyOutputTensor);
        //NOTE: For now take into account that only the first sink
        //goes to concat but in general this needs to be searched
        if (sinkOperators[0]->getOpType() == "Concat")
        {
            for (uint8_t inputId = 0; inputId < sinkOperators[0]->getInputTensor().size(); inputId++)
            {
                if (sinkOperators[0]->getInputTensor()[inputId]->getName() == leakyOutputTensor->getName())
                    branch = inputId;
            }
        }

        mv::Data::OpListIterator relu0, conv0, conv1, depthwise1, relu1;
        for (uint8_t i = 0; i < ADD_INPUT_FLOWS; i++)
        {
            if (i == 0)
            {
                conv0 = portConv(model,leakyReluOp, parentOp, i);
                relu0 = portRelu(model, conv0->getOutputTensor(0), conv0, leakyReluQuantParams);
            }
            else
            {
                conv1 = portConv(model, leakyReluOp, parentOp, i);
                relu1 = portRelu(model, conv1->getOutputTensor(0), conv1, leakyReluQuantParams);
                depthwise1 = portDepthwise(model, relu1->getOutputTensor(0), leakyReluOp, leakyReluQuantParams);
            }
        }
        std::vector<mv::Data::TensorIterator> inputs;
        inputs.push_back(relu0->getOutputTensor(0));
        inputs.push_back(depthwise1->getOutputTensor(0));
        auto add0 = portAdd(om, inputs, leakyReluOp, leakyReluQuantParams);
        auto backup = parentOp.leftmostOutput();
        om.undefineFlow(backup);
        om.removeOp(om.getSourceOp(parentOp->getInputTensor()[1]));
        om.removeOp(parentOp);
        om.removeOp(leakyReluOp);
        for (std::size_t numberOfSink = 0; numberOfSink < sinkOperators.size(); numberOfSink++)
        {
            sinkOperators[numberOfSink]->setInputTensor(add0->getOutputTensor(0), branch, false);
            om.defineFlow(add0->getOutputTensor(0), sinkOperators[numberOfSink], branch);
            //NOTE: These lines are here for the positive branch....
//            sinkOperators[numberOfSink]->setInputTensor(relu0->getOutputTensor(0), branch, false);
//            om.defineFlow(relu0->getOutputTensor(0), sinkOperators[numberOfSink], branch);
        }
        leakyRelu++;
    }
}
