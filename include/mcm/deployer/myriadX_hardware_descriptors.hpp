#ifndef _HWOPERATION_H_
#define _HWOPERATION_H_

typedef struct
{
    uint32_t linkAddress : 32;      // Offset Tracking
    uint32_t type : 3;              // Known
    uint32_t mode : 3;              // Needs Calculation (Basic)
    uint32_t interleavedInput : 1;  // Can be ignored first pass.  - Custom info in reserved field
    uint32_t interleavedOutput : 1; // Can be ignored first pass.  - Custom info in reserved field
    uint32_t id : 4;                // Can be ignored first pass.
    uint32_t it : 4;                // ?
    uint32_t cm : 3;                // Known
    uint32_t dm : 1;                // Known
    uint32_t disInt : 1;            // ?
    uint32_t rsvd1 : 11;            // N/A
}GeneralLinkStructure;

typedef struct
{
    GeneralLinkStructure Line0;
    uint32_t inputHeight        : 12;   // Needs Calculation (Basic)
    uint32_t topOutputJunk      : 4;    // Needs Calculation (Splitting) - Custom info in reserved field
    uint32_t inputWidth         : 12;   // Needs Calculation (Basic)
    uint32_t bottomOutputJunk   : 4;    // Needs Calculation (Splitting) -  Custom info in reserved field
    uint32_t inputChannels      : 11;   // Known
    uint32_t rsvd2              : 5;    // N/A
    uint32_t outputChannels     : 11;   // Known
    uint32_t rsvd3              : 5;    // N/A
    uint32_t chPerRamBlock      : 11;   // Needs Calculation ( Mode )
    uint32_t rsvd4              : 5;    // N/A
    uint32_t chStride           : 4;    // Known?
    uint32_t rsvd5              : 12;   // N/A
    uint32_t kernelWidth        : 4;    // Known - not used for Pooling
    uint32_t kernelHeight       : 4;    // Known - not used for Pooling
    uint32_t rsvd6              : 19;   // N/A
    uint32_t padType            : 4;    // Known
    uint32_t padEn              : 1;    // Known
    uint32_t poolEn             : 1;    // Needs Fusion
    uint32_t rsvd7              : 15;   // N/A
    uint32_t poolKernelHeight   : 8;    // Known
    uint32_t poolKernelWidth    : 8;    // Known
    uint32_t avgPoolX           : 16;   // Can be left for now - Average value for pooling OR X value for ReLuX
    uint32_t rsvd8              : 15;   // N/A
    uint32_t poolType           : 1;    // Known
    uint32_t dataBaseAddr       : 32;   // Needs integration with Tensor Logic
    uint32_t t0                 : 10;   // Can be left for now - not used for Pooling
    uint32_t a0                 : 10;   // Can be left for now -  not used for Pooling
    uint32_t a1                 : 10;   // Can be left for now -  not used for Pooling
    uint32_t reluxEn            : 1;    // Can be left for now - not used for Pooling
    uint32_t reluEn             : 1;    // Can be left for now - not used for Pooling
    uint32_t dataChStr          : 32;   // Known (Reshaped)
    uint32_t dataLnStr          : 32;   // Known (Reshaped)
    uint32_t coeffBaseAddr      : 32;   // Needs integration with Tensor Logic
    uint32_t coeffChStrOut      : 32;   // Known (Reshaped)
    uint32_t coeffChStrIn       : 32;   // Known (Reshaped) - not used for Pooling
    uint32_t outLnStr           : 32;   // Known (Reshaped)
    uint32_t outBaseAddr        : 32;   // Needs integration with Tensor Logic
    uint32_t outChStr           : 32;   // Known (Reshaped)
    uint32_t localLs            : 9;    // ????????????????????
    uint32_t rsvd9              : 7;    // N/A
    uint32_t localCs            : 13;   // ??????????????????????????
    uint32_t rsvd10             : 3;    // N/A
    uint32_t linesPerCh         : 9;    // Needs Calculation (?)
    uint32_t sodGroup           : 22;   // Needs Calculation (Splitting) - Custom info (split-over-depth group id) in reserved field
    uint32_t rud                : 1;    // Can be left for now
    uint32_t minLines           : 9;    // Needs Calculation (mode)
    uint32_t sohGroup           : 23;   // Needs Calculation (Splitting) Custom info (split-over-height group id) in reserved field
    uint32_t coeffLpb           : 8;    // Needs Calculation (?) - not used for Pooling
    uint32_t css                : 8;    // ???????????? - not used for Pooling
    uint32_t outputX            : 12;   // Known??? -
    uint32_t rsvd13             : 4;    // N/A -
    uint32_t biasBaseAddr       : 32;   // Needs integration with Tensor Logic
    uint32_t scaleBaseAddr      : 32;   // Needs integration with Tensor Logic
    uint32_t p0                 : 16;   // N/A
    uint32_t p1                 : 16;   // N/A
    uint32_t p2                 : 16;   // N/A
    uint32_t p3                 : 16;   // N/A
    uint32_t p4                 : 16;   // N/A
    uint32_t p5                 : 16;   // N/A
    uint32_t p6                 : 16;   // N/A
    uint32_t p7                 : 16;   // N/A
    uint32_t p8                 : 16;   // N/A
    uint32_t p9                 : 16;   // N/A
    uint32_t p10                : 16;   // N/A
    uint32_t p11                : 16;   // N/A
    uint32_t p12                : 16;   // N/A
    uint32_t p13                : 16;   // N/A
    uint32_t p14                : 16;   // N/A
    uint32_t p15                : 16;   // N/A
} cnnConvolutionPoolStructure;

typedef struct
{
    GeneralLinkStructure Line0;
    uint32_t inputWidth         : 12;
    uint32_t rsvd0              : 20;
    uint32_t vectors            : 8;
    uint32_t rsvd1              : 8;
    uint32_t vectors2           : 8;
    uint32_t rsvd2              : 8;
    uint32_t dataPerRamBlock    : 9;
    uint32_t rsvd3              : 23;
    uint32_t rsvd4              : 32;
    uint32_t rsvd5              : 1;
    uint32_t actualOutChannels  : 8;    // Custom info (How many of the output channels contain useful info)
    uint32_t rsvd5_             : 23;
    uint32_t X                  : 16;
    uint32_t rsvd6              : 16;
    uint32_t dataBaseAddr       : 32;
    uint32_t t0                 : 10;
    uint32_t a0                 : 10;
    uint32_t a1                 : 10;
    uint32_t reluxEn            : 1;
    uint32_t reluEn             : 1;
    uint32_t dataChStr          : 32;
    uint32_t dataLnStr          : 32;
    uint32_t vectorBaseAddr     : 32;
    uint32_t vectorStrOut       : 32;
    uint32_t vectorStrIn        : 32;
    uint32_t outLnStr           : 32;
    uint32_t outBaseAddr        : 32;
    uint32_t outChStr           : 32;
    uint32_t localLs            : 9;
    uint32_t rsvd7              : 7;
    uint32_t localBs            : 13;
    uint32_t rsvd8              : 3;
    uint32_t rsvd9              : 31;
    uint32_t rud                : 1;
    uint32_t rsvd10             : 16;
    uint32_t acc                : 1;
    uint32_t rsvd11             : 15;
    uint32_t vectorLPB          : 8;
    uint32_t rsvd12             : 8;
    uint32_t outputX            : 12;   // Due to a hardware bug, outputX for FC must be set to 1
    uint32_t rsvd12_            : 4;
    uint32_t biasBaseAddr       : 32;
    uint32_t scaleBaseAddr      : 32;
    uint32_t p0                 : 16;
    uint32_t p1                 : 16;
    uint32_t p2                 : 16;
    uint32_t p3                 : 16;
    uint32_t p4                 : 16;
    uint32_t p5                 : 16;
    uint32_t p6                 : 16;
    uint32_t p7                 : 16;
    uint32_t p8                 : 16;
    uint32_t p9                 : 16;
    uint32_t p10                : 16;
    uint32_t p11                : 16;
    uint32_t p12                : 16;
    uint32_t p13                : 16;
    uint32_t p14                : 16;
    uint32_t p15                : 16;
} cnnFCStructure;

#endif