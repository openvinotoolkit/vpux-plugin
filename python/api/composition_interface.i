
/// SWIG Interface File.

// Include the external numpy swig bindings.
%{
#define SWIG_FILE_WITH_INIT
%}
%include "numpy.i"
%init %{
import_array();
%}

%module composition_api
%{
    #include <include/mcm/compiler/compilation_unit.hpp>
    #include <include/mcm/utils/compositional_model_recorder.hpp>
    #include <math.h>
    #include <iostream>

    int testSWIG(){
        /// A simple test to ensure the connection between Python and C++ is working
        int test = 1;
        return test;
    }

    mv::CompilationUnit* getCompilationUnit()
    {

        auto unit = new mv::CompilationUnit("pySwigCU");
        unit->loadTargetDescriptor(mv::Target::ma2480);

        // Define the manadatory arguments for passes using compilation descriptor obtained from compilation unit
        unit->compilationDescriptor()["GenerateDot"]["output"] = std::string("pycm.dot");
        unit->compilationDescriptor()["GenerateDot"]["scope"] = std::string("ExecOpControlModel");
        unit->compilationDescriptor()["GenerateDot"]["content"] = std::string("full");
        unit->compilationDescriptor()["GenerateDot"]["html"] = true;
        unit->compilationDescriptor()["GenerateJson"]["output"] = std::string("cpp.json");
        unit->compilationDescriptor()["GenerateBlob"]["output"] = std::string("cpp.blob");
        return unit;

    }

    mv::CompositionalModel* getModel(mv::CompilationUnit *unit)
    {
        return &unit->recordedModel();
    }

    int compile(mv::CompilationUnit *unit)
    {
        unit->initialize();
        auto compOutput = unit->run();
        return (int)compOutput["passes"].last()["blobSize"].get<long long>();
    }

    // TODO: Create Generic Vector Calls
    std::array<unsigned short, 2> * get2DVector(int x, int y){
        std::array<unsigned short, 2> *arr = new std::array<unsigned short, 2>();
        arr->at(0) = x;
        arr->at(1) = y;
        return arr;
    }

    std::array<unsigned short, 4> * get4DVector(int w, int x, int y, int z){
        std::array<unsigned short, 4> *arr = new std::array<unsigned short, 4>();
        arr->at(0) = w;
        arr->at(1) = x;
        arr->at(2) = y;
        arr->at(3) = z;
        return arr;
    }

    mv::Shape * getShape(std::size_t x){
        /// Create a c++ shape object from a passed in set of dimension sizes
        mv::Shape* a = new mv::Shape({x});
        return a;
    }

    mv::Shape * getShape(std::size_t x, std::size_t y){
        /// Create a c++ shape object from a passed in set of dimension sizes
        mv::Shape* a = new mv::Shape({x, y});
        return a;
    }

    mv::Shape * getShape(std::size_t x, std::size_t y, std::size_t z){
        /// Create a c++ shape object from a passed in set of dimension sizes
        mv::Shape* a = new mv::Shape({x, y, z});
        return a;
    }

    mv::Shape * getShape(std::size_t b, std::size_t x, std::size_t y, std::size_t z){
        /// Create a c++ shape object from a passed in set of dimension sizes
        mv::Shape* a = new mv::Shape({b, x, y, z});
        return a;
    }

    std::vector<double> * getData(double * d, std::size_t len){
        /// Populate a Vector with a numpy array.
        std::vector<double> * weightsData = new std::vector<double>(d, d + len);
        return weightsData;
    }

    int testConv(
        mv::Data::OpListIterator &target,
        unsigned exp_strideX,
        unsigned exp_strideY,
        unsigned exp_padX,
        unsigned exp_padY
        ){
        /// A couple of simple checks to ensure we have loaded the items correctly.

        int ret_val = 0;    // Success
        std::array<unsigned short, 2> stride = target->get<std::array<unsigned short, 2>>("stride");
        std::array<unsigned short, 4> pad = target->get<std::array<unsigned short, 4>>("padding");
        if(stride.at(0) != exp_strideX)
            ret_val = 1;
        if(stride.at(1) != exp_strideY)
            ret_val = 2;
        if(pad.at(1) != exp_padX)
            ret_val = 3;
        if(pad.at(3) != exp_padY)
            ret_val = 4;
        // TODO Consider assymetric padding

        return ret_val;
    }

    mv::Data::TensorIterator input(mv::CompositionalModel& o, const mv::Shape &shape){
        /// Add an Input Layer to the OpModel and return the relevant iterator
        return o.input(shape, mv::DTypeType::Float16, mv::OrderType::RowMajorPlanar);
    }

    mv::Data::TensorIterator output(mv::CompositionalModel& o, mv::Data::TensorIterator input){
        /// Add an Output Layer to the OpModel and return the relevant iterator
        return o.output(input);
    }

    mv::Data::TensorIterator maxpool2D(mv::CompositionalModel& o, mv::Data::TensorIterator input, short unsigned kernelSizeX,
        short unsigned kernelSizeY, short unsigned strideX, short unsigned strideY, short unsigned padX, short unsigned padY){
        /// Add a Max Pooling Layer to the OpModel and return the relevant iterator
        return o.maxpool2D(input, {kernelSizeX, kernelSizeY}, {strideX, strideY},
            {padX, padX, padY, padY});
    }

    mv::Data::TensorIterator maxpool2D_caffe(mv::CompositionalModel& o, mv::Data::TensorIterator input, short unsigned kernelSizeX,
        short unsigned kernelSizeY, short unsigned strideX, short unsigned strideY, short unsigned padX, short unsigned padY){

        /// This differs from the above because caffe calculates output sizes differently.
        /// To compensate, we add values to pad.
        /// See: https://github.com/BVLC/caffe/issues/1318

        short unsigned int adj_X = 0, adj_Y = 0;

        mv::Shape i = input->getShape();

        double inner_x_calc = double(i[0] + padX + padX - kernelSizeX);
        double inner_y_calc = double(i[1] + padY + padY - kernelSizeY);

        double caffe_x = ceil(inner_x_calc / strideX) + 1;
        double caffe_y = ceil(inner_y_calc / strideX) + 1;

        double tensorflow_x = ceil((inner_x_calc +1) / strideX);
        double tensorflow_y = ceil((inner_y_calc +1) / strideX);

        adj_X = caffe_x - tensorflow_x;
        adj_Y = caffe_y - tensorflow_y;

        return o.maxpool2D(input, {kernelSizeX, kernelSizeY}, {strideX, strideY},
            {padX, (short unsigned int)(padX + adj_X), padY, (short unsigned int)(padY + adj_Y)});
    }

    mv::Data::TensorIterator concat(mv::CompositionalModel& o, mv::Data::TensorIterator input0, mv::Data::TensorIterator input1){
        /// Add a Concat Layer to the OpModel and return the relevant iterator.
        /// Allows only two inputs at a time. More must cascade
        return o.concat(input0, input1);
    }

    mv::Data::TensorIterator conv2D(mv::CompositionalModel& o, mv::Data::TensorIterator input, mv::Data::TensorIterator filters,
        short unsigned strideX, short unsigned strideY, short unsigned padX, short unsigned padY){
        /// Add a Convolutional Layer to the OpModel and return the relevant iterator
        return o.conv2D(input, filters, {strideX, strideY}, {padX, padX, padY, padY});
    }

    mv::Data::TensorIterator conv2D_caffe(mv::CompositionalModel& o, mv::Data::TensorIterator input, mv::Data::TensorIterator filters,
        short unsigned strideX, short unsigned strideY, short unsigned padX, short unsigned padY){
        /// This differs from the above because caffe calculates output sizes differently.
        /// To compensate, we add values to pad.

        short unsigned int adj_X = 0, adj_Y = 0;

        mv::Shape i = input->getShape();
        mv::Shape k = filters->getShape();

        int kernelSizeX =  k[0];
        int kernelSizeY =  k[1];

        double inner_x_calc = double(i[0] + padX + padX - kernelSizeX);
        double inner_y_calc = double(i[1] + padY + padY - kernelSizeY);

        double caffe_x = ceil(inner_x_calc / strideX) + 1;
        double caffe_y = ceil(inner_y_calc / strideX) + 1;

        double tensorflow_x = ceil((inner_x_calc +1) / strideX);
        double tensorflow_y = ceil((inner_y_calc +1) / strideX);

        adj_X = caffe_x - tensorflow_x;
        adj_Y = caffe_y - tensorflow_y;

        return o.conv2D(input, filters, {strideX, strideY}, {padX , (short unsigned )(padX- adj_X), padY, (short unsigned )(padY - adj_Y)});
    }

    mv::Data::TensorIterator constant(mv::CompositionalModel& o, const std::vector<double>& data, const mv::Shape &shape){
        /// Add a Constant Layer to the CompositionalModel and return the relevant iterator
        return o.constant(data, shape, mv::DTypeType::Float16, mv::OrderType::RowMajorPlanar);
    }

    mv::Data::OpListIterator getSourceOp(mv::CompositionalModel& o, mv::Data::TensorIterator tensor){
        // Get source operation of a tensor
        return o.getSourceOp(tensor);
    }

    mv::Data::TensorIterator matMul(mv::CompositionalModel& o, mv::Data::TensorIterator input, mv::Data::TensorIterator weights){
        return o.matMul(input, weights);
    }

    mv::Data::TensorIterator avgpool2D(mv::CompositionalModel& o, mv::Data::TensorIterator input, std::array<unsigned short, 2> kernelSize, std::array<unsigned short, 2> stride, std::array<unsigned short, 4> padding){
        return o.avgpool2D(input, kernelSize, stride, padding);
    }

    mv::Data::TensorIterator avgpool2D_caffe(mv::CompositionalModel& o, mv::Data::TensorIterator input, short unsigned kernelSizeX,
        short unsigned kernelSizeY, short unsigned strideX, short unsigned strideY, short unsigned padX, short unsigned padY){

        /// This differs from the above because caffe calculates output sizes differently.
        /// To compensate, we add values to pad.
        /// See: https://github.com/BVLC/caffe/issues/1318

        short unsigned int adj_X = 0, adj_Y = 0;

        mv::Shape i = input->getShape();

        double inner_x_calc = double(i[0] + padX + padX - kernelSizeX);
        double inner_y_calc = double(i[1] + padY + padY - kernelSizeY);

        double caffe_x = ceil(inner_x_calc / strideX) + 1;
        double caffe_y = ceil(inner_y_calc / strideX) + 1;

        double tensorflow_x = ceil((inner_x_calc +1) / strideX);
        double tensorflow_y = ceil((inner_y_calc +1) / strideX);

        adj_X = caffe_x - tensorflow_x;
        adj_Y = caffe_y - tensorflow_y;

        return o.avgpool2D(input, {kernelSizeX, kernelSizeY}, {strideX, strideY},
            {padX, (short unsigned )(padX+ adj_X), padY, (short unsigned )(padY+ adj_Y)});
    }

    mv::Data::TensorIterator batchNorm(mv::CompositionalModel& o,mv::Data::TensorIterator input, mv::Data::TensorIterator mean, mv::Data::TensorIterator variance, mv::Data::TensorIterator offset, mv::Data::TensorIterator scale, double varianceEps){
        return o.batchNorm(input, mean, variance, offset, scale, varianceEps);
    }
    mv::Data::TensorIterator scale(mv::CompositionalModel& o,mv::Data::TensorIterator input, mv::Data::TensorIterator scale){
        return o.scale(input, scale);
    }
    mv::Data::TensorIterator relu(mv::CompositionalModel& o, mv::Data::TensorIterator input){
        return o.relu(input);
    }
    mv::Data::TensorIterator prelu(mv::CompositionalModel& o, mv::Data::TensorIterator input, mv::Data::TensorIterator negative_slope){
        return o.prelu(input, negative_slope);
    }
    mv::Data::TensorIterator softmax(mv::CompositionalModel& o,mv::Data::TensorIterator input){
        return o.softmax(input);
    }
    mv::Data::TensorIterator add(mv::CompositionalModel& o,mv::Data::TensorIterator input0, mv::Data::TensorIterator input1){
        return o.add(input0, input1);
    }
    mv::Data::TensorIterator subtract(mv::CompositionalModel& o,mv::Data::TensorIterator input0, mv::Data::TensorIterator input1){
        return o.subtract(input0, input1);
    }
    mv::Data::TensorIterator multiply(mv::CompositionalModel& o,mv::Data::TensorIterator input0, mv::Data::TensorIterator input1){
        return o.multiply(input0, input1);
    }
    mv::Data::TensorIterator fullyConnected(mv::CompositionalModel& o,mv::Data::TensorIterator input0, mv::Data::TensorIterator input1){
        return o.fullyConnected(input0, input1);
    }

    mv::Data::TensorIterator divide(mv::CompositionalModel& o,mv::Data::TensorIterator input0, mv::Data::TensorIterator input1){
        return o.divide(input0, input1);
    }
    mv::Data::TensorIterator reshape(mv::CompositionalModel& o,mv::Data::TensorIterator input, const mv::Shape& shape){
        return o.reshape(input, shape);
    }
    mv::Data::TensorIterator bias(mv::CompositionalModel& o, mv::Data::TensorIterator input, mv::Data::TensorIterator bias_values){
        return o.bias(input, bias_values);
    }

    bool isValid(mv::CompositionalModel& o){
    	return o.isValid();
    }

 %}

#include <include/mcm/computation/model/control_model.hpp>
#include <include/mcm/deployer/serializer.hpp>

// The section below is exposing the functions within the included files,
// or the ones defined above in the module.

namespace mv
{
    namespace Data
    {
        class TensorIterator
        {
        public:
            ~TensorIterator();
        };

        class OpListIterator
        {
        public:
            ~OpListIterator();
        };

    }
}

int testSWIG();
mv::CompilationUnit* getCompilationUnit();
mv::CompositionalModel* getModel(mv::CompilationUnit *unit);
int compile(mv::CompilationUnit *unit);
mv::Shape * getShape(int x);
mv::Shape * getShape(int x, int y);
mv::Shape * getShape(int x, int y, int z);
mv::Shape * getShape(int b, int x, int y, int z);
std::array<unsigned short, 2> * get2DVector(int x, int y);
std::array<unsigned short, 4> * get4DVector(int w, int x, int y, int z);

// Expand a numpy array to a data pointer and a length
%include "stdint.i"
%apply (double* INPLACE_ARRAY1, std::size_t DIM1) {(double* d, std::size_t len)}
std::vector<double> * getData(double * d, std::size_t len);


mv::Data::TensorIterator input(mv::CompositionalModel& o, const mv::Shape &shape);
mv::Data::TensorIterator output(mv::CompositionalModel& o, mv::Data::TensorIterator input);
mv::Data::TensorIterator conv2D(mv::CompositionalModel& o, mv::Data::TensorIterator input, mv::Data::TensorIterator filters,
    short unsigned strideX, short unsigned strideY, short unsigned padX, short unsigned padY);
mv::Data::TensorIterator conv2D_caffe(mv::CompositionalModel& o, mv::Data::TensorIterator input, mv::Data::TensorIterator filters,
    short unsigned strideX, short unsigned strideY, short unsigned padX, short unsigned padY);
mv::Data::TensorIterator maxpool2D(mv::CompositionalModel& o, mv::Data::TensorIterator input, short unsigned kernelSizeX,
    short unsigned kernelSizeY, short unsigned strideX, short unsigned strideY, short unsigned padX, short unsigned padY);
mv::Data::TensorIterator maxpool2D_caffe(mv::CompositionalModel& o, mv::Data::TensorIterator input, short unsigned kernelSizeX,
    short unsigned kernelSizeY, short unsigned strideX, short unsigned strideY, short unsigned padX, short unsigned padY);
mv::Data::TensorIterator avgpool2D_caffe(mv::CompositionalModel& o, mv::Data::TensorIterator input, short unsigned kernelSizeX,
    short unsigned kernelSizeY, short unsigned strideX, short unsigned strideY, short unsigned padX, short unsigned padY);
mv::Data::TensorIterator concat(mv::CompositionalModel& o, mv::Data::TensorIterator input0, mv::Data::TensorIterator input1);
mv::Data::OpListIterator getSourceOp(mv::CompositionalModel& o, mv::Data::TensorIterator tensor);

mv::Data::TensorIterator matMul(mv::CompositionalModel& o, mv::Data::TensorIterator input, mv::Data::TensorIterator weights);
mv::Data::TensorIterator avgpool2D(mv::CompositionalModel& o, mv::Data::TensorIterator input, std::array<unsigned short, 2> kernelSize, std::array<unsigned short, 2> stride, std::array<unsigned short, 4> padding);
mv::Data::TensorIterator batchNorm(mv::CompositionalModel& o,mv::Data::TensorIterator input, mv::Data::TensorIterator mean, mv::Data::TensorIterator variance, mv::Data::TensorIterator offset, mv::Data::TensorIterator scale, double varianceEps);
mv::Data::TensorIterator scale(mv::CompositionalModel& o,mv::Data::TensorIterator input, mv::Data::TensorIterator scale);
mv::Data::TensorIterator relu(mv::CompositionalModel& o,mv::Data::TensorIterator input);
mv::Data::TensorIterator softmax(mv::CompositionalModel& o,mv::Data::TensorIterator input);
mv::Data::TensorIterator add(mv::CompositionalModel& o,mv::Data::TensorIterator input0, mv::Data::TensorIterator input1);
mv::Data::TensorIterator subtract(mv::CompositionalModel& o,mv::Data::TensorIterator input0, mv::Data::TensorIterator input1);
mv::Data::TensorIterator multiply(mv::CompositionalModel& o,mv::Data::TensorIterator input0, mv::Data::TensorIterator input1);
mv::Data::TensorIterator divide(mv::CompositionalModel& o,mv::Data::TensorIterator input0, mv::Data::TensorIterator input1);
mv::Data::TensorIterator reshape(mv::CompositionalModel& o,mv::Data::TensorIterator input, const mv::Shape& shape);
mv::Data::TensorIterator bias(mv::CompositionalModel& o, mv::Data::TensorIterator input, mv::Data::TensorIterator bias_values);
mv::Data::TensorIterator fullyConnected(mv::CompositionalModel& o,mv::Data::TensorIterator input0, mv::Data::TensorIterator input1);
mv::Data::TensorIterator constant(mv::CompositionalModel&  o, const std::vector<double>& data, const mv::Shape &shape);
mv::Data::TensorIterator prelu(mv::CompositionalModel& o, mv::Data::TensorIterator input, mv::Data::TensorIterator negative_slope);
bool isValid(mv::CompositionalModel& o);

int testConv(
    mv::Data::OpListIterator &target,
    int exp_strideX,
    int exp_strideY,
    int exp_padX,
    int exp_padY
);
