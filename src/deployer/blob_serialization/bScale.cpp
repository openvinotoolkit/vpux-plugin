#include <stdio.h>
#include "include/mcm/deployer/blob_serialization/bScale.hpp"

namespace mv
{

    void bScale::writeStageInfo(mv::OpModel * om, mv::Blob_buffer* b)
    {
        int fp16_size = 2;
        mv::DataModel dm(*om);
        mv::ControlModel cm(*om);


        if(this->bias_name != "")
            this->bias = dm.findTensor(this->bias_name);
        else
            this->bias = {} ;

        Blob_Tensor inputBlobTensor = Blob_Tensor(&dm, &cm, &b->reloc_table, &this->input);
        Blob_Tensor outputBlobTensor = Blob_Tensor(&dm, &cm, &b->reloc_table, &this->output);
        // Blob_Tensor tapsBlobTensor = Blob_Tensor(
        //     0x00,   // X
        //     1,   // Y
        //     1,   // Z
        //     2,     // X Stride
        //     2*it->getInputTensor(1)->getShape().totalSize(),    // Y Stride
        //     2,
        //     conv_pool_stage.TBOffset,
        //     3,
        //     conv_pool_stage.OutputDataType,
        //     3
        // );
        Blob_Tensor tapsBlobTensor = Blob_Tensor(&dm, &cm, &b->reloc_table, &this->taps);


        Blob_Tensor biasBlobTensor = Blob_Tensor(&dm, &cm, &b->reloc_table, &this->bias);

        inputBlobTensor.write(b);
        outputBlobTensor.write(b);
        tapsBlobTensor.write(b);
        biasBlobTensor.write(b);
    }

    bScale::bScale(mv::ComputationOp* it)
        :
          Blob_Op_Definition(),
          input((it->getInputTensor(0))),
          output((it->getOutputTensor(0))),
          taps((it->getInputTensor(1)))
    {

        if (it->hasAttr("bias"))
        {
            this->bias_name = it->getAttr("bias").getContent<std::string>();
            std::cout << "Conv has Bias" << std::endl;
        }
        else
        {
            this->bias_name = "";
            std::cout << "Conv has no Bias" <<  std::endl;
        }



        printf("Warning: Manual Override of Scale Software layer order\n");
        this->output->setOrder(Order::RowMajor);
        this->input->setOrder(Order::RowMajor);
        this->taps->setOrder(Order::RowMajorAlt);
    }
}
