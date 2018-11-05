#ifndef MV_BLOB_SERIALIZER_HPP_
#define MV_BLOB_SERIALIZER_HPP_

/**
* serializer.hpp contains classes that output to file compute graph representations in various formats.
*
* @author Patrick Doyle, Ian Hunter
* @date 4/27/2018
*/
#include "include/mcm/computation/model/op_model.hpp"
#include "include/mcm/utils/serializer/Fp16Convert.h"
#include "include/mcm/utils/serializer/file_buffer.hpp"
#include "include/mcm/computation/model/control_model.hpp"
#include "include/mcm/deployer/blob_serialization/myriadX_hardware_descriptors.hpp"
#include "include/mcm/deployer/blob_serialization/bDefinition.hpp"
#include "include/mcm/deployer/blob_serialization/bTensor.hpp"
#include "include/mcm/deployer/blob_serialization/bConv_MX.hpp"
#include "include/mcm/deployer/blob_serialization/bDepthwiseConv.hpp"
#include "include/mcm/deployer/blob_serialization/bRelocation.hpp"
#include "include/mcm/deployer/blob_serialization/bPooling_MX.hpp"
#include "include/mcm/deployer/blob_serialization/bSoftmax.hpp"
#include "include/mcm/deployer/blob_serialization/bRelu.hpp"
#include "include/mcm/deployer/blob_serialization/bPRelu.hpp"
#include "include/mcm/deployer/blob_serialization/bScale.hpp"
#include "include/mcm/deployer/blob_serialization/bEltwise.hpp"
#include "include/mcm/deployer/blob_serialization/bInnerProduct.hpp"
#include "include/mcm/deployer/blob_serialization/bCompatibility.hpp"
#include <assert.h>

#define BLOB_VERSION_MAJOR 2
#define BLOB_VERSION_MINOR 3
#define BLOB_MAGIC_NUMBER 8708

#define BLOB_DEFAULT_IMPLEMENTATION 0x80000000

namespace mv
{

    class Blob_stage
    {
        public:
            std::uint32_t next ;
            std::uint32_t op_type ;
            std::uint32_t implementation  ;
            std::uint32_t preop_type  ;
            std::uint32_t postop_type ;

            std::uint32_t radixX;
            std::uint32_t radixY;
            std::uint32_t radixStrideX;
            std::uint32_t radixStrideY;
            std::uint32_t padX;
            std::uint32_t padY;
            std::uint32_t padStyle;
            std::uint32_t dilation;

            std::uint32_t InputDimX;
            std::uint32_t InputDimY;
            std::uint32_t InputDimZ;
            std::uint32_t InputStrideX;
            std::uint32_t InputStrideY;
            std::uint32_t InputStrideZ;
            std::uint32_t InputOffset;
            std::uint32_t InputLocation;
            std::uint32_t InputDataType;
            std::uint32_t InputOrder;
            std::uint32_t Input1Offset;
            std::uint32_t Input1Location;
            std::uint32_t TBOffset;

            std::uint32_t OutputDimX;
            std::uint32_t OutputDimY;
            std::uint32_t OutputDimZ;
            std::uint32_t OutputStrideX;
            std::uint32_t OutputStrideY;
            std::uint32_t OutputStrideZ;
            std::uint32_t OutputOffset;
            std::uint32_t OutputLocation;
            std::uint32_t OutputDataType;
            std::uint32_t OutputOrder;

            std::uint32_t TapsDimX;
            std::uint32_t TapsDimY;
            std::uint32_t TapsDimZ;
            std::uint32_t TapsStrideX;
            std::uint32_t TapsStrideY;
            std::uint32_t TapsStrideZ;
            std::uint32_t TapsOffset;
            std::uint32_t TapsLocation;
            std::uint32_t TapsDataType;
            std::uint32_t TapsOrder;

            std::uint32_t BiasDimX;
            std::uint32_t BiasDimY;
            std::uint32_t BiasDimZ;
            std::uint32_t BiasStrideX;
            std::uint32_t BiasStrideY;
            std::uint32_t BiasStrideZ;
            std::uint32_t BiasOffset;
            std::uint32_t BiasLocation;
            std::uint32_t BiasDataType;
            std::uint32_t BiasOrder;

            Blob_stage()
            {
                next = 0x0000 ;
                op_type = 0x0000;
                implementation = 0x80000000 ;

                radixX = 3 ;
                radixY = 3 ;
                radixStrideX = 2 ;
                radixStrideY = 2 ;
                padX = 0 ;
                padY = 0 ;
                padStyle = 2 ;
                dilation = 1 ;

                InputDimX = 32 ;
                InputDimY = 32 ;
                InputDimZ = 3 ;
                InputStrideX = 2 ;
                InputStrideY = 64 ;
                InputStrideZ = 2 ;
                InputOffset = 0 ;
                InputLocation = 1 ;
                Input1Offset = 0;
                Input1Location = 1;
                InputDataType = 0 ;
                InputOrder = 0 ;

                OutputDimX = 16 ;
                OutputDimY = 16 ;
                OutputDimZ = 8 ;
                OutputStrideX = 2 ;
                OutputStrideY = 0x10 ;
                OutputStrideZ = 2 ;
                OutputOffset = 0 ;
                OutputLocation = 2 ;
                OutputDataType = 0 ;
                OutputOrder = 0 ;

                TapsDimX = 9 ;
                TapsDimY = 1 ;
                TapsDimZ = 1 ;
                TapsStrideX = 2 ;
                TapsStrideY = 2 ;
                TapsStrideZ = 2 ;
                TapsOffset = 0 ;
                TBOffset = 0 ;
                TapsLocation = 3 ;
                TapsDataType = 0 ;
                TapsOrder = 3 ;

                BiasDimX = 64 ;
                BiasDimY = 1 ;
                BiasDimZ = 1 ;
                BiasStrideX = 2 ;
                BiasStrideY = 128 ;
                BiasStrideZ = 128 ;
                BiasOffset = 0 ;
                BiasLocation = 3 ;
                BiasDataType = 0 ;
                BiasOrder = 1 ;

                preop_type = 5 ;
                postop_type = 5 ;
            }
    };

    struct blob_summary
    {
        std::uint32_t elf_header_size;
        std::uint32_t mv_header_size;
        std::uint32_t header_pad_size;
        std::uint32_t stage_section_size;
        std::uint32_t buffer_header_size;
        std::uint32_t buffer_data_size;
        std::uint32_t buffer_data_pad_size;
        std::uint32_t relocation_section_size;
        std::uint32_t weights_region_size;
        std::uint32_t bias_region_size;
        std::uint32_t weights_number_size;
        std::uint32_t tensor_number_size;
        std::uint32_t stage_count;
        std::uint32_t data_buffer_count;
        std::uint32_t elt_count;
        std::uint32_t input_size;
        std::uint32_t output_size;
        std::uint32_t blob_file_size;
        std::vector<std::uint32_t> relocbuf_list = {  } ;
        std::vector<std::uint32_t> relocadr_list = {  } ;
    };

    class Blob_buffer : public WBuffer
    {
        private:
            blob_summary blob_stats;

        public:
            RelocationTable reloc_table;
            Blob_buffer()
            {
                this->reloc_table = RelocationTable();
            }

            blob_summary getBlobSumm();


            // Calculate Blob Statistics
            std::size_t calc(mv::ControlModel& cm);

            void write_elf_header();

            void write_mv_header();

            void write_stage_section_header();

            void add_stage_IO_info(mv::Control::OpDFSIterator it, mv::Blob_stage conv_pool_stage);

            void write_stages(mv::ControlModel& cm);

            void write_buffer_section(mv::ControlModel& cm);

            void write_relocation_section(mv::ControlModel& cm);

            int get_blob_enum(mv::OpType o, bool NCE1=false);

    };   // end class blob_buffer

}

#endif // MV_BLOB_SERIALIZER_HPP_
