﻿#include "include/mcm/pass/pass_registry.hpp"
#include "include/mcm/computation/model/op_model.hpp"
#include "include/mcm/computation/model/control_model.hpp"
#include "include/mcm/computation/model/data_model.hpp"
#include "include/mcm/computation/resource/nce1.hpp"
#include "include/mcm/computation/resource/nce1_utils.hpp"

static void modeSelection(mv::ComputationModel& model, mv::TargetDescriptor&, mv::json::Object&, mv::json::Object&);

namespace mv
{

    namespace pass
    {

        MV_REGISTER_PASS(ModeSelection)
        .setFunc(modeSelection)
        .setGenre(PassGenre::Finalization)
        .setDescription(
            "This pass selects the appropriate mode for each convolution executable by NCE"
        );
    }
}


void write_hardware_attributes(mv::OpModel& om, mv::Data::OpListIterator convIterator, mv::ModeSelectionResult& modes_to_use, mv::Nce1& nce)
{
    // ASSUMPTION: all the splits in modes_to_use are are equal (This assumption is true now, and will be routines to assure it's trueness in the future)

    // Scalar attributes first
    auto input_tensor = convIterator->getInputTensor(0);
    auto input_tensor_dimensions = input_tensor->getShape();

    auto output_tensor = convIterator->getOutputTensor(0);
    auto output_tensor_dimensions = output_tensor->getShape();

    auto weight_tensor = convIterator->getInputTensor(1);
    auto weight_tensor_dimensions = weight_tensor->getShape();

    // Take biggest mode, necessary for getting the actual input channels
    unsigned num_modes_to_use = modes_to_use.distances.size();
    std::vector<std::size_t> modes(num_modes_to_use);
    unsigned max_mode = modes_to_use.distances[0].mode;
    for(unsigned i = 0; i < num_modes_to_use; ++i)
    {
        modes[i] = modes_to_use.distances[i].mode;
        if(max_mode < modes[i])
            max_mode = modes[i];
    }

    // Getting real dimensions
    std::size_t input_width = input_tensor_dimensions[0];
    std::size_t input_height = input_tensor_dimensions[1];
    std::size_t input_channels = input_tensor_dimensions[2];
    std::size_t output_width = output_tensor_dimensions[0];
    std::size_t output_height = output_tensor_dimensions[1];
    std::size_t output_channels = output_tensor_dimensions[2];
    std::size_t kernel_height = weight_tensor_dimensions[0];

    //TEMPORARY HACK: Prepass takes care of everything
    if(input_tensor->hasAttr("NCE1_Paddings"))
    {
        std::vector<std::size_t> input_tensor_paddings = input_tensor->get<std::vector<std::size_t>>("NCE1_Paddings");
        input_width += input_tensor_paddings[0];
        input_height += input_tensor_paddings[1];
        input_channels += input_tensor_paddings[2];
    }

    if(output_tensor->hasAttr("NCE1_Paddings"))
    {
        std::vector<std::size_t> output_tensor_paddings = output_tensor->get<std::vector<std::size_t>>("NCE1_Paddings");
        output_width += output_tensor_paddings[0];
        output_height += output_tensor_paddings[1];
        output_channels += output_tensor_paddings[2];
    }

    // Check if any split over input channel is needed
    unsigned splits_over_input_channels = modes_to_use.distances[0].num_splits; //num_splits must be equal for every mode, see above assumption
    unsigned splitted_input_channels = input_channels / splits_over_input_channels;

    // Compute local line stride
    unsigned local_line_stride = nce.computeLocalLineStride(input_width);
    om.addAttr(convIterator, "NCE1_LocalLineStride", (std::size_t)local_line_stride);

    // TODO: Streaming mask
    unsigned streaming_mask = 0; // For DDR streaming
    om.addAttr(convIterator, "NCE1_StreamingMask", (std::size_t)streaming_mask);

    // Compute DescriptorsSplits
    unsigned splits_over_height = 1; //Until nce.computeSplitsOverH(total_tensor_size) is fixed;
    om.addAttr(convIterator, "NCE1_SplitsOverH", (std::size_t)splits_over_height);
    om.addAttr(convIterator, "NCE1_SplitsOverC", (std::size_t)splits_over_input_channels);

    unsigned descriptor_splits = nce.computeDescriptorSplits(splits_over_height, splits_over_input_channels, output_channels, modes);
    om.addAttr(convIterator, "NCE1_DescriptorSplits", (std::size_t)descriptor_splits);

    unsigned top_output_junk = 0;
    unsigned bottom_output_junk = 0;

    if(splits_over_height == 1)
    {
        om.addAttr(convIterator, "NCE1_TopOutputJunk", (std::size_t)top_output_junk);
        om.addAttr(convIterator, "NCE1_BottomOutputJunk", (std::size_t)bottom_output_junk);
    }
    else
    {
        om.addAttr(convIterator, "NCE1_TopOutputJunk", kernel_height - 1);
        om.addAttr(convIterator, "NCE1_BottomOutputJunk", kernel_height - 1);
    }
    // -------------------VECTOR ATTRIBUTES----------------
    std::vector<std::size_t> input_channels_per_ram_block(num_modes_to_use);
    std::vector<std::size_t> lines_per_channel(num_modes_to_use);
    std::vector<std::size_t> local_channel_stride(num_modes_to_use);
    std::vector<std::size_t> min_lines(num_modes_to_use);
    for(unsigned i = 0; i < num_modes_to_use; ++i)
    {
        input_channels_per_ram_block[i] = nce.computeInputChannelsPerRamBlock(splitted_input_channels, modes[i]);
        lines_per_channel[i] = nce.computeLinesPerChannel(splitted_input_channels, local_line_stride, modes[i]);
        local_channel_stride[i] = lines_per_channel[i] * local_line_stride;

        min_lines[i] = 0;
        bool poolEn = false;
        if(poolEn)
            min_lines[i] = 0; //TODO
        else
            min_lines[i] = std::min(kernel_height + 1, lines_per_channel[i]);
    }
    om.addAttr(convIterator, "NCE1_Modes", modes);
    om.addAttr(convIterator, "NCE1_InputChannelsRamBlock", input_channels_per_ram_block);
    om.addAttr(convIterator, "NCE1_LinesPerChannel", lines_per_channel);
    om.addAttr(convIterator, "NCE1_LocalChannelStride", local_channel_stride);
    om.addAttr(convIterator, "NCE1_MinLines", min_lines);
}

void optimize_convolution_nce1(mv::Nce1& nce, mv::Data::OpListIterator convIterator, mv::OpModel& om)
{
    mv::ModeSelectionNode source;
    source.parameters = mv::fillConvolutionParameters(convIterator);

    source.remaining_output_channels = source.parameters.output_channels;
    mv::ModeSelectionResult modes = nce.optimize_convolution(source);
    write_hardware_attributes(om, convIterator, modes, nce);
}

void modeSelection(mv::ComputationModel& model, mv::TargetDescriptor&, mv::json::Object& pobj, mv::json::Object&)
{
    mv::OpModel om(model);
    mv::Nce1 nce;
    std::queue<mv::Data::OpListIterator> to_be_optimized;

    for(auto opIterator = om.opBegin(); opIterator != om.opEnd(); ++opIterator)
    {
        if (opIterator->getOpType() == mv::OpType::Conv2D)
        {

            if(!opIterator->hasAttr("NCE1_Compatible"))
                continue;
            if(!opIterator->get<int>("NCE1_Compatible"))
                continue;
            to_be_optimized.push(opIterator);

            /*
            case mv::OpType::FullyConnected:
                //TODO: Write mode 0 or 4
                break;
            case mv::OpType::MaxPool2D:
            case mv::OpType::AvgPool2D:
                //TODO: Write mode 0 or 4
                break;
            */

        }

    }

    while(to_be_optimized.size() > 0)
    {

        auto opIterator = to_be_optimized.front();
        to_be_optimized.pop();
        optimize_convolution_nce1(nce, opIterator, om);

    }

}
