//
// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: Apache 2.0
//
#include "include/mcm/tensor/tensor.hpp"
#include "include/mcm/tensor/math.hpp"
#include "include/mcm/utils/custom_math.hpp"
#include "include/mcm/utils/custom_strings.hpp"

namespace {

std::map<std::string, mv::Tensor::MemoryLocation::Location> createNamingMap() {
        return {{"NNCMX",mv::Tensor::MemoryLocation::NNCMX},
                {"UPACMX",mv::Tensor::MemoryLocation::UPACMX},
                {"DDR",mv::Tensor::MemoryLocation::DDR},
                {"INPUT",mv::Tensor::MemoryLocation::INPUT},
                {"OUTPUT",mv::Tensor::MemoryLocation::OUTPUT},
                {"BLOB",mv::Tensor::MemoryLocation::BLOB},
                {"VIRTUAL",mv::Tensor::MemoryLocation::VIRTUAL},
                {"CSRAM",mv::Tensor::MemoryLocation::CSRAM},
                {"PROFILING",mv::Tensor::MemoryLocation::PROFILING},
                {"DEFAULT",mv::Tensor::MemoryLocation::DEFAULT}
        };
}

std::map<std::string, mv::Tensor::MemoryLocation::Location> namingMap = createNamingMap();

}  // namespace

mv::Tensor::MemoryLocation::MemoryLocation(const std::string& location)
    : location_(namingMap[location]), forced_(false)
{}

mv::Tensor::MemoryLocation::MemoryLocation(const std::string& location, bool forced)
    : location_(namingMap[location]), forced_(forced)
{}

bool mv::Tensor::MemoryLocation::operator==(std::string& other) const
{
    return (location_ == namingMap[other]);
}

bool mv::Tensor::MemoryLocation::operator!=(std::string& other) const
{
    return (location_ != namingMap[other]);
}

bool mv::Tensor::MemoryLocation::relocate(std::string& newPlace)
{
    return relocate( namingMap[newPlace]);
}

std::string mv::Tensor::MemoryLocation::toString() const
{
    for( auto it = namingMap.begin(); it != namingMap.end(); ++it )
    {
        if(it->second == location_)
            return it->first;
    }
    throw ValueError(*this, "Memory location cannot be found in Map!!");
}

mv::Tensor::Tensor(const std::string &name, const Shape &shape, DType dType, Order order):
Element(name),
shape_(shape),
internalOrder_(Order(order.toString())),
blockSize_(shape[shape.ndims() - 1])
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BASE)

    if(order.size() != shape.ndims())
        throw OrderError(*this, "Order and shape size are mismatching " + std::to_string(order.size()) + " vs " + std::to_string(shape.ndims()));
    set<Order>("order", order);
    if(dType == mv::DType("Default"))
        throw std::runtime_error("Tensors cannot be instantiated with default DType");
    set<DType>("dType", dType);
    set<bool>("populated", false);
    set<MemoryLocation>("Location",MemoryLocation::DEFAULT);
}

mv::Tensor::Tensor(const std::string &name, const Shape &shape, DType dType, Order order, const mv::QuantizationParams &quantParams):
Tensor(name, shape, dType, order)
{
    set<mv::QuantizationParams>("quantParams", quantParams);
}

mv::Tensor::Tensor(const std::string &name, const Shape &shape, DType dType, Order order, const std::vector<double>& data) :
Tensor(name, shape, dType, order)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BASE)
    populate(data, order);
}

mv::Tensor::Tensor(const std::string &name, const Shape &shape, DType dType, Order order, const std::vector<double>& data, const mv::QuantizationParams &quantParams) :
Tensor(name, shape, dType, order, quantParams)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BASE)
    populate(data, order);
}

mv::Tensor::Tensor(const std::string &name, const Shape &shape, DType dType, Order order, const std::vector<int64_t>& data) :
Tensor(name, shape, dType, order)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BASE)
    populate(data, order);
}

mv::Tensor::Tensor(const std::string &name, const Shape &shape, DType dType, Order order, const std::vector<int64_t>& data, const mv::QuantizationParams &quantParams) :
Tensor(name, shape, dType, order, quantParams)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BASE)
    populate(data, order);
}

mv::Tensor::Tensor(const std::string &name, const Shape &shape, DType dType, Order order, const std::vector<mv::DataElement>& data) :
Tensor(name, shape, dType, order)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BASE)
    populate(data, order);
}

mv::Tensor::Tensor(const std::string &name, const Shape &shape, DType dType, Order order, const std::vector<mv::DataElement>& data, const mv::QuantizationParams &quantParams):
Tensor(name, shape, dType, order, quantParams)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BASE)
    populate(data, order);
}

mv::Tensor::Tensor(const Tensor &other) :
Element(other),
shape_(other.shape_),
internalOrder_(other.internalOrder_),
blockSize_(other.blockSize_),
sparsityMap_(),
storageElement_(),
subTensors_(),
kernelDataOffsets_(other.kernelDataOffsets_),
noneZeroElements_(other.noneZeroElements_)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BASE)

    if (other.hasSubTensors())
    {
        for (std::size_t i = 0; i < other.numSubTensors(); i++)
        {
            subTensors_.push_back(std::make_shared<mv::Tensor>(*other.subTensors_[i]));
            subTensors_.back()->setName(getName() + "sub" + std::to_string(i));
        }
    }


    if (other.isPopulated())
    {
        data_ = other.data_;
        blocks_ = other.blocks_;
    }

    if (other.isSparse()) {
        set<bool>("sparse", false);
        for (size_t i = 0; i < other.numSubTensors(); i++) {
            subTensors_[i]->set<bool>("sparse", false);
        }
        setSparse();
        if (isPopulated()) {
            sparsityMap_ = std::make_shared<Tensor>(*other.sparsityMap_);
            sparsityMap_->setName(createSparsityMapName(getName()));
        }
        if (other.hasAttr("address")) {
            setAddress(other.get<size_t>("address"));
        }
    }
}

mv::Tensor::~Tensor()
{
    data_.clear();
}

std::vector<std::size_t> mv::Tensor::indToSub_(const Shape& s, size_t index) const
{
    return internalOrder_.indToSub(s, index);
}

unsigned mv::Tensor::subToInd_(const Shape& s, const std::vector<std::size_t>& sub) const
{

    if (hasAttr("master"))
    {
        Shape correctedShape(s);
        std::vector<std::size_t> correctedSub(sub);
        if (hasAttr("leftPadding"))
        {
            auto padding = get<std::vector<std::size_t>>("leftPadding");
            for (std::size_t i = 0; i < padding.size(); ++i)
            {
                correctedShape[i] += padding[i];
                correctedSub[i] += padding[i];
            }
        }
        if (hasAttr("rightPadding"))
        {
            auto padding = get<std::vector<std::size_t>>("rightPadding");
            for (std::size_t i = 0; i < padding.size(); ++i)
                correctedShape[i] += padding[i];

        }

        return internalOrder_.subToInd(correctedShape, correctedSub);

    }

    return internalOrder_.subToInd(s, sub);

}

void mv::Tensor::populate(const std::vector<double>& data)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BULD)
    if (!isDoubleType() && getDType() != mv::DType("Float16") && getDType() != mv::DType("BFloat16"))
        throw ArgumentError(*this, "data vector", "type double", "Unable to populate, data type is not double"
            "DType of tensor is " + getDType().toString() + " but populating with double data");

    if(getDType() == mv::DType("Float16") || getDType() == mv::DType("BFloat16"))
    {
        bool fp16DType = getDType() == mv::DType("Float16");
        //todo convert data to int
        std::vector<int64_t> intData(data.size());
        for(std::size_t i = 0; i < data.size(); i++)
        {
            if (fp16DType)
                intData[i] = mv::fp32_to_fp16(data[i]);
            else
                intData[i] = mv::fp32_to_bf16(data[i]);

        }
        populate(intData);
        return;
    }
    if (data.size() != shape_.totalSize())
        throw ArgumentError(*this, "data vector", std::to_string(data.size()), "Unable to populate, data vector size"
            "does not match total size the tensor (" + std::to_string(shape_.totalSize()) + ")");

    auto dataSize = shape_.totalSize()/blockSize_;
    for(std::size_t t=data_.size(); t<dataSize; t++){
        data_.push_back(std::make_shared<std::vector<DataElement>>(blockSize_, DataElement(true)));
    }

    if (getOrder() != internalOrder_){
        for(std::size_t j = 0; j < data.size();++j){
            std::vector<std::size_t> sub = getOrder().indToSub(shape_, j);
            auto idx = internalOrder_.subToInd(shape_, sub);
            auto b = idx / blockSize_;
            auto i = idx % blockSize_;
            data_[b]->at(i) =  data[j];
        }
    }
    else{
        for(std::size_t m = 0; m < data.size();++m){
            auto d = m / blockSize_;
            auto e = m % blockSize_;
            data_[d]->at(e) = data[m];
        }
    }

    set("populated", true);

    //if sparse then call sparsify
    if (isSparse())
        populateSparsityMapTensor_();
}

void mv::Tensor::populate(const std::vector<mv::DataElement>& data)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BULD)
    if (data.size() != shape_.totalSize())
        throw ArgumentError(*this, "data vector", std::to_string(data.size()), "Unable to populate, data vector size"
            "does not match total size the tensor (" + std::to_string(shape_.totalSize()) + ")");

    auto dataSize = shape_.totalSize()/blockSize_;
    for(std::size_t t=data_.size(); t<dataSize; t++){
        data_.push_back(std::make_shared<std::vector<DataElement>>(blockSize_, data[0].isDouble()));
    }

    if (getOrder() != internalOrder_){
        for(std::size_t j = 0; j < data.size();++j){
            std::vector<std::size_t> sub = getOrder().indToSub(shape_, j);
            auto idx = internalOrder_.subToInd(shape_, sub);
            auto b = idx / blockSize_;
            auto i = idx % blockSize_;
            data_[b]->at(i) =  data[j];
        }
    }
    else{
        for(std::size_t m = 0; m < data.size();++m){
            auto d = m / blockSize_;
            auto e = m % blockSize_;
            data_[d]->at(e) = data[m];
        }
    }
    set("populated", true);

    //if sparse then call sparsify
    if (isSparse())
        populateSparsityMapTensor_();
}


void mv::Tensor::populate(const std::vector<int64_t>& data)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BULD)
    if (isDoubleType())
        throw ArgumentError(*this, "data vector", "type int", "Unable to populate, data type is not int"
            "DType of tensor is " + getDType().toString() + " but populating with int data");

    if (data.size() != shape_.totalSize())
    {
        throw ArgumentError(*this, "data vector", std::to_string(data.size()), "Unable to populate, data vector size"
            "does not match total size the tensor (" + std::to_string(shape_.totalSize()) + ")");
    }

    auto dataSize = shape_.totalSize()/blockSize_;
    for(std::size_t t=data_.size(); t<dataSize; t++)
        data_.push_back(std::make_shared<std::vector<DataElement>>(blockSize_, false));

    if (getOrder() != internalOrder_){
        for(std::size_t j = 0; j < data.size();++j){
            std::vector<std::size_t> sub = getOrder().indToSub(shape_, j);
            auto idx = internalOrder_.subToInd(shape_, sub);
            auto b = idx / blockSize_;
            auto i = idx % blockSize_;
            data_[b]->at(i) =  data[j];
        }
    }
    else{
        for(std::size_t m = 0; m < data.size();++m){
            auto d = m / blockSize_;
            auto e = m % blockSize_;
            data_[d]->at(e) = data[m];
        }
    }

    set("populated", true);

    //if sparse then call sparsify
    if (isSparse())
        populateSparsityMapTensor_();
}

void mv::Tensor::matchSubTensors()
{
    auto data = getIntData();
    auto sum = subTensors_[0]->getShape().totalSize();
    for (size_t tIdx = 1; tIdx < subTensors_.size(); tIdx++)
    {
        for(size_t m = 0; m < subTensors_[tIdx]->getShape().totalSize(); ++m)
            data.at(m + sum) = data.at(m);
        sum += subTensors_[tIdx]->getShape().totalSize();
    }
}

void mv::Tensor::populate(const std::vector<mv::DataElement>& data, Order order)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BULD)
    set<Order>("order", order);
    populate(data);
}

void mv::Tensor::populate(const std::vector<double>& data, Order order)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BULD)
    set<Order>("order", order);
    populate(data);
}

void mv::Tensor::populate(const std::vector<int64_t>& data, Order order)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BULD)
    set<Order>("order", order);
    populate(data);
}

void mv::Tensor::unpopulate()
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BULD)
    if (!isPopulated())
        return;

    for(unsigned i=0; i<data_.size();++i)
        data_[i].reset();

    set<bool>("populated", false);

    //sparsity map tensor need to unpopulate as well
    if (isSparse())
    {
        sparsityMap_->unpopulate();
        kernelDataOffsets_.clear();
    }

}

// NOTE: why a reference? Otherwise return *this trick doesn't work
mv::Tensor& mv::Tensor::getSubTensor(uint8_t cluster)
{
    if (cluster < subTensors_.size())
        return *subTensors_[cluster];
    return *this;
}

std::shared_ptr<mv::Tensor> mv::Tensor::getSparsityMap() const
{
    if (!isSparse())
        throw ArgumentError(*this, "currentTensor", "SparsityMap" , " tensor not sparse, cannot get Sparsity Map");
    return sparsityMap_;
}

std::shared_ptr<mv::Tensor> mv::Tensor::getStorageElement() const
{
    if (!isSparse())
        throw ArgumentError(*this, "currentTensor", "storageElement" , " tensor not sparse, cannot get storage element");

    return storageElement_;
}

std::vector<int64_t> mv::Tensor::getZeroPointsPerChannel() const
{
    //default all zeropoints to zero
    std::vector<int64_t> zeroPoint(shape_[mv::KERNEL_OUTPUT_CHANNELS]);
    if (isQuantized())
    {
        auto quantParams = get<mv::QuantizationParams>("quantParams");
        for (size_t t=0; t < zeroPoint.size(); t++)
        {
            //Note: For now support of 1 channel zero point
            zeroPoint[t] = quantParams.getZeroPoint(0);
        }
    }
    return zeroPoint;
}

const mv::Order& mv::Tensor::getInternalOrder() const
{
    return internalOrder_;
}

// Auxiliary function to write sparsity map entry and reset all the variables
void writeMapEntry(std::vector<int64_t>& sparsityMapData, std::size_t& sparsityMapIdx, uint8_t& map, int& shift)
{
    sparsityMapData.at(sparsityMapIdx++) = map;
    map = 0;
    shift = 0;
}

void mv::Tensor::populateSparsityMapTensor_()
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BULD)
    const auto shape = getShape();

    std::vector<int64_t> zeroPoint = getZeroPointsPerChannel();
    std::vector<int64_t> sparsityMapData(sparsityMap_->shape_.totalSize(), 0);
    std::vector<size_t> sub(shape.ndims());
    uint8_t map = 0;
    std::size_t sparsityMapIdx = 0;
    int shift = 0;
    noneZeroElements_ = 0;

    const auto outputChannels = shape[mv::KERNEL_OUTPUT_CHANNELS];
    const auto outputChannelSize = shape.totalSize() / outputChannels;

    for (std::size_t k = 0; k < outputChannels; ++k)
    {
        // Add elements from an output channel to the sparsity map so that
        // each bit represents one element (8 elements per byte)
        if (getOrder() == internalOrder_)
        {
            for (std::size_t i = 0; i < outputChannelSize; i++)
            {
                const auto idx = k*outputChannelSize + i;
                const auto b = idx/blockSize_;
                const auto d = idx%blockSize_;
                if (static_cast<int64_t>(data_[b]->at(d)) != zeroPoint[k])
                    map ^= (1 << shift);
                if (++shift == 8)
                    writeMapEntry(sparsityMapData, sparsityMapIdx, map, shift);
            }
        }
        else
        {
            for (std::size_t i = 0; i < outputChannelSize; i++)
            {
                sub = getOrder().indToSub(shape, k*outputChannelSize + i);
                const auto idx = internalOrder_.subToInd(shape, sub);
                const auto b = idx/blockSize_;
                const auto d = idx%blockSize_;
                if (static_cast<int64_t>(data_[b]->at(d)) != zeroPoint[k])
                    map ^= (1 << shift);
                if (++shift == 8)
                    writeMapEntry(sparsityMapData, sparsityMapIdx, map, shift);
            }
        }

        // This is needed in the case when tensor dimensions are not multiple of 8
        // This should never happen because weights sets are padded to have alignment to 16
        if (shift != 0)
            writeMapEntry(sparsityMapData, sparsityMapIdx, map, shift);

        sparsityMapIdx = mv::round_up(sparsityMapIdx, 16);
    }

    // Following the above reasoning, this should never happen
    if (shift != 0)
        writeMapEntry(sparsityMapData, sparsityMapIdx, map, shift);

    sparsityMap_->populate(sparsityMapData);
}

void mv::Tensor::setAddress(int64_t address)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BULD)
    set<std::size_t>("address", address);
    if (isSparse())
    {
        size_t tensorSize;

        tensorSize = isPopulated() ? getClusterSize() : computeTotalSize();

        // Order assumed for unpopulated: Tensor - Storage Element - Sparsity Map
        // Order assumed for populated: Tensor (packed data) - Sparsity Map
        auto sparsitySize = sparsityMap_->getClusterSize();
        //NOTE: if the tensor is fused, sparsity maps might take totally different addresses
        if (hasAttr("sparsityMapAddress"))
            sparsityMap_->setAddress(get<std::size_t>("sparsityMapAddress"));
        else if (!hasAttr("fusedOffset"))
            sparsityMap_->setAddress(address + (tensorSize - sparsitySize));

        if(!isPopulated())
        {
            auto storageElementSize = storageElement_->getClusterSize();
            storageElement_->setAddress(address + (tensorSize - storageElementSize - sparsitySize));
        }
    }

    for (size_t tIdx = 0; tIdx < subTensors_.size(); tIdx++)
    {
        subTensors_[tIdx]->setAddress(address);
        // Handle A0 SOH sparsity addressing
        if (subTensors_[tIdx]->hasAttr("use_sub0_addrs") && subTensors_[tIdx]->get<bool>("use_sub0_addrs")) {
            auto sm_addr = subTensors_[0]->getSparsityMap()->getAddress();
            auto se_addr = subTensors_[0]->getStorageElement()->getAddress();
            subTensors_[tIdx]->getSparsityMap()->setAddress(sm_addr);
            subTensors_[tIdx]->getStorageElement()->setAddress(se_addr);
        }
    }
}

bool mv::Tensor::setSparse()
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BULD)
    mv::Order order =  getOrder();
    if (!order.isZMajor())
        throw ArgumentError(*this, "Order", order.toString() , " Sparsity requires ZMajor layout (NHWC)");

    if (order.size() < 3)
        throw ArgumentError(*this, "Order", order.toString() , " Sparsity requires order of size >= 3");

    if(hasAttr("sparse") && get<bool>("sparse") == true)
        return false;

    set<bool>("sparse", true);

    auto shape = getShape();
    size_t N = shape[shape.ndims()-1];

    //Sparsity map
    //we choose layout as internal layout, no need to reorder
    mv::Shape mapShape({shape[0], shape[1], static_cast<std::size_t>(std::ceil(shape[2] / 8.0)), N});
    if (isPopulated())
    {
        //pad the shape
        auto paddedDim = mv::round_up(static_cast<std::size_t>(std::ceil(shape[0] / 8.0)), 16);
        mapShape = {paddedDim, 1, 1, N};
    }
    //std::cout << "Total bytes of sparsity map for " << getName() << " " << mapShape.totalSize() * mv::DType("UInt8").getSizeInBits() / 8 << std::endl;
    sparsityMap_ = std::make_shared<Tensor>(createSparsityMapName(getName()), mapShape, mv::DType("UInt8"), Order("NHWC"));
    sparsityMap_->set<bool>("sparsityMap", true);
    noneZeroElements_ = 0;



    //populate sparsity map
    if (isPopulated())
    {
        populateSparsityMapTensor_();

        // NOTE: Necessary to initialize correctly noneZeroElements_
        getDataPacked();
    }
    else
    {
        mv::Shape storageElementShape({shape[0], shape[1], 1, N});
        //std::cout << "Total bytes of storage element for " << getName() << " " << storageElementShape.totalSize() * mv::DType("Int32").getSizeInBits() / 8 << std::endl;
        storageElement_  = std::make_shared<Tensor>(createStorageElementName(getName()), storageElementShape, mv::DType("Int32"), order);
    }

    unsigned sm_offset_byte_index = 0UL, se_offset_byte_index = 0UL;
    for (size_t tIdx = 0; tIdx < subTensors_.size(); tIdx++) {

        subTensors_[tIdx]->setSparse();
        if (!isPopulated()) {
          auto sub_sparsity_map = subTensors_[tIdx]->getSparsityMap();
          // set offset_byte_index //
          sub_sparsity_map->set<unsigned>("offset_byte_index",
              sm_offset_byte_index );
          sm_offset_byte_index += sub_sparsity_map->getClusterSize();
          sparsityMap_->subTensors_.push_back(sub_sparsity_map);


          auto sub_storage_element = subTensors_[tIdx]->getStorageElement();
          sub_storage_element->set<unsigned>("offset_byte_index",
              se_offset_byte_index);
          se_offset_byte_index += sub_storage_element->getClusterSize();
          storageElement_->subTensors_.push_back(sub_storage_element);
        }
    }

    return true;
}

void mv::Tensor::bindData(Tensor& other, const std::vector<std::size_t>& leftPadding, const std::vector<std::size_t> &rightPadding)
{
    if (leftPadding.size() != other.shape_.ndims() && !leftPadding.empty())
        throw ArgumentError(*this, "leftPadding::size", std::to_string(leftPadding.size()), "Invalid dimension of the left padding vector,"
            " must be equal to the dimensionality of the master tensor, which is " + std::to_string(other.shape_.ndims()));

    if (rightPadding.size() != other.shape_.ndims() && !rightPadding.empty())
        throw ArgumentError(*this, "rightPadding::size", std::to_string(rightPadding.size()), "Invalid dimension of the right padding vector,"
            " must be equal to the dimensionality of the master tensor, which is " + std::to_string(shape_.ndims()));

    if (other.isPopulated())
    {
        data_ = other.data_;
        set<bool>("populated", true);
    }

    if (!leftPadding.empty())
        set<std::vector<std::size_t>>("leftPadding", leftPadding);
    if (!rightPadding.empty())
        set<std::vector<std::size_t>>("rightPadding", rightPadding);

    Shape newShape(other.shape_);

    for (std::size_t i = 0; i < leftPadding.size(); ++i)
        newShape[i] -= leftPadding[i] + rightPadding[i];

    set<Shape>("shape", newShape);
    set<Order>("order", other.getOrder());
    set<DType>("dType", other.getDType());
    set<std::string>("master", other.getName());
    if (!other.hasAttr("slave"))
        other.set<std::vector<std::string>>("slave", { getName() });
    else
        other.get<std::vector<std::string>>("slave").push_back(getName());

    if (other.hasAttr("quantParams"))
        set<mv::QuantizationParams>("quantParams", other.get<mv::QuantizationParams>("quantParams"));
}

void mv::Tensor::setOrder(Order order, bool updateSubtensors)
{
    if(order != getOrder())
    {
        set<Order>("order", order);

        //If the data order changes, the packed data is invalid
        if (updateSubtensors)
            setSubtensorsOrder_(order);
    }
}

void mv::Tensor::setPlaced(bool placed, const Shape& masterDim)
{
    set<bool>("placed", placed);
    set<mv::Shape>("masterDim", masterDim);
}

void mv::Tensor::setSubtensorsOrder_(Order order)
{
    for (size_t tIdx = 0; tIdx < subTensors_.size(); tIdx++)
        subTensors_[tIdx]->setOrder(order);
}

void mv::Tensor::setDType(DType dtype)
{

    set<DType>("dType", dtype);
    for (size_t tIdx = 0; tIdx < subTensors_.size(); tIdx++)
        subTensors_[tIdx]->setDType(dtype);
    return;

}

void mv::Tensor::setShape(const Shape& shape)
{
    if(isPopulated())
    {
        if(shape.totalSize() != getShape().totalSize())
            throw ArgumentError(*this, "CurrentTensor", "shape", "Unable to change shape of a populated tensor");
    }
    shape_ = shape;
    return;
}
void mv::Tensor::broadcast(const Shape& shape)
{

    if (!isPopulated())
        throw ValueError(*this, "Broadcastring of an unpopulated tensor is undefined");

    if (hasAttr("master") || hasAttr("slave"))
        throw ValueError(*this, "Unable to broadcast a bound tensor");

    Shape s1 = shape_, s2 = shape;

    if (s1.ndims() == 0 || s2.ndims() == 0)
        throw ValueError(*this, "Unable to perfom element-wise operation using 0-dimensional tensor");

    //TODO - Currently broadcast function is not being called/utilized but will need to verify the calc below with data_ (vector of vector) when this function is enabled

    if (s1 != s2)
    {
        // Will throw on error
        Shape sO = Shape::broadcast(s1, s2);
        std::vector<DataElement> dataBuf = std::vector<DataElement>(sO.totalSize(), DataElement(isDoubleType()));

        if (s1.ndims() > s2.ndims())
        {
            s2 = Shape::augment(s2, s1.ndims());
        }
        else if (s2.ndims() > s1.ndims())
            s1 = Shape::augment(s1, s2.ndims());

        if (sO.totalSize() != s1.totalSize()){
           if (sO.totalSize()/blockSize_ > data_.size())
           {
                 auto newNumBlocks = ceil(sO.totalSize()/blockSize_);
                 data_[data_.size() - 1]->resize(blockSize_, isDoubleType());
                 for ( auto i = data_.size(); i <  newNumBlocks; i++)
                        data_.push_back(std::make_shared<std::vector<DataElement>>(blockSize_, DataElement(true)));
           }
        }

        for (unsigned i = 0; i < dataBuf.size(); ++i)
        {
            std::vector<std::size_t> sub = indToSub_(sO, i);

            for (unsigned j = 0; j < sub.size(); ++j)
            {
                if (s1[j] == 1 && sO[j] > 1)
                    sub[j] = 0;
            }

            dataBuf[i] = at(subToInd_(s1, sub));
        }

        set<Shape>("shape", sO);
        shape_ = sO;
        for (unsigned i = 0; i < dataBuf.size(); ++i){
            auto id = i/blockSize_;
            auto id_b = i%blockSize_;
            data_[id]->at(id_b) = dataBuf[i];
        }
    }

}

mv::QuantizationParams mv::Tensor::getQuantParams() {
    if (!hasAttr("quantParams"))
        return mv::QuantizationParams::empty();
    return get<mv::QuantizationParams>("quantParams");
}

void mv::Tensor::setQuantParams(const mv::QuantizationParams& quantParams) {
    if (!quantParams.isEmpty())
        set<mv::QuantizationParams>("quantParams", quantParams);
}

// TODO - Handle the case when tensor got deleted, by the reference is still in use
std::vector<double> mv::Tensor::getDoubleData()
{
    if (!isPopulated())
        throw ValueError(*this, "Attempt of restoring data from an unpopulated tensor");

    if (!isDoubleType())
        throw ValueError(*this, "Attempt of reading double data from an int type tensor");

    std::vector<double> orderedData(shape_.totalSize());

    auto temp_dataTotal = data_.size() * blockSize_;
    if (getOrder() != internalOrder_)
    {
        for (std::size_t i = 0; i < temp_dataTotal; ++i)
        {
            const auto t = i/blockSize_;
            const auto u = i%blockSize_;
            const auto sub = internalOrder_.indToSub(shape_, i);
            const auto idx = getOrder().subToInd(shape_, sub);
            orderedData[idx] = data_[t]->at(u);
        }
    }
    else
    {
        for (std::size_t i = 0; i < temp_dataTotal; ++i)
        {
            const auto t = i/blockSize_;
            const auto u = i%blockSize_;
            orderedData[i] = data_[t]->at(u);
        }
    }
    return orderedData;
}

std::vector<mv::DataElement> mv::Tensor::getData()
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BULD)
    if (!isPopulated())
        throw ValueError(*this, "Attempt of restoring data from an unpopulated tensor");

    std::vector<DataElement> orderedData(shape_.totalSize(), DataElement(isDoubleType()));

    const auto temp_dataTotal = shape_.totalSize();
    if (getOrder() != internalOrder_)
    {
        for (std::size_t i = 0; i < temp_dataTotal; ++i)
        {
            const auto t = i/blockSize_;
            const auto u = i%blockSize_;
            const auto sub = internalOrder_.indToSub(shape_, i);
            const auto idx = getOrder().subToInd(shape_, sub);
            orderedData[idx] = data_[t]->at(u);
        }
    }
    else
    {
        for (std::size_t i = 0; i < temp_dataTotal; ++i)
        {
            const auto t = i/blockSize_;
            const auto u = i%blockSize_;
            orderedData[i] = data_[t]->at(u);
        }
    }

    return orderedData;
}

const std::vector<int64_t>& mv::Tensor::getKernelDataOffsets()
{
    if(kernelDataOffsets_.empty())
        getDataPacked();
    return kernelDataOffsets_;
}

// NOTE: For now this can handle only integer tensors
// This is because we have to make quantParams (currently int64) dataElements as well.

// NOTE2: This method works properly only on tensors of the form (W, 1, 1, N)
// The order has to be an order in which the W comes after the N, examples:
// NHWC, NHCW
const std::vector<int64_t> mv::Tensor::getDataPacked()
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BULD)
    if (!isPopulated())
        throw ValueError(*this, "Attempt of restoring data from an unpopulated tensor");

    // TODO: Make it work on doubles - Or maybe not considering that FP16 is integer type
    if(isDoubleType())
        throw ValueError(*this, "Attempt of getting data packed from populated double tensor");


    std::vector<int64_t> orderedDataPacked;

    // NOTE: noneZeroElements_ has a misleading name. We pad the packed data
    // and that actually is memorized in graphfile and transferred. So it's
    // counter must be updated for both elements != ZeroPoint and for each
    // ZeroPoint of padding that we put in orderedDataPacked
    noneZeroElements_ = 0;

    auto shape = getShape();
    std::vector<std::size_t> sub(shape.ndims());
    std::vector<int64_t> zeroPoint = getZeroPointsPerChannel();

    size_t outputChannels = shape[mv::KERNEL_OUTPUT_CHANNELS];
    size_t outputChannelSize = shape.totalSize() / outputChannels;
    kernelDataOffsets_.resize(outputChannels);
    size_t offset = 0;

    // Filling the data on a per channel basis
    for (std::size_t k = 0; k < outputChannels; ++k)
    {
        kernelDataOffsets_[k] = offset;
        const size_t prevNumOfElements = orderedDataPacked.size();
        if (getOrder() == internalOrder_)
        {
            if (isSparse())
            {
                for (std::size_t i = 0; i < outputChannelSize; i++)
                {
                    const auto idx = k*outputChannelSize + i;
                    const auto b = idx/blockSize_;
                    const auto d = idx%blockSize_;
                    const auto datai = static_cast<int64_t>(data_[b]->at(d));
                    if (datai != zeroPoint[k])
                        orderedDataPacked.push_back(datai);
                }
                // Add padding. Needed only for sparse cases as weights are already aligned
                const auto padSize = mv::round_up(orderedDataPacked.size(), 16) - orderedDataPacked.size();
                std::fill_n(std::back_inserter(orderedDataPacked), padSize, zeroPoint[k]);
            }
            else
            {
                for (std::size_t i = 0; i < outputChannelSize; i++)
                {
                    const auto idx = k*outputChannelSize + i;
                    const auto b = idx/blockSize_;
                    const auto d = idx%blockSize_;
                    orderedDataPacked.push_back(static_cast<int64_t>(data_[b]->at(d)));
                }
            }
        }
        else
        {
            if (isSparse())
            {
                for (std::size_t i = 0; i < outputChannelSize; i++)
                {
                    sub = getOrder().indToSub(shape, k*outputChannelSize + i);
                    const auto idx = internalOrder_.subToInd(shape, sub);
                    const auto b = idx/blockSize_;
                    const auto d = idx%blockSize_;
                    const auto datai = static_cast<int64_t>(data_[b]->at(d));
                    if (datai != zeroPoint[sub[mv::KERNEL_OUTPUT_CHANNELS]])
                        orderedDataPacked.push_back(datai);
                }
                // Add padding. Needed only for sparse cases as weights are already aligned
                const auto padSize = mv::round_up(orderedDataPacked.size(), 16) - orderedDataPacked.size();
                std::fill_n(std::back_inserter(orderedDataPacked), padSize, zeroPoint[k]);
            }
            else
            {
                for (std::size_t i = 0; i < outputChannelSize; i++)
                {
                    sub = getOrder().indToSub(shape, k*outputChannelSize + i);
                    const auto idx = internalOrder_.subToInd(shape, sub);
                    const auto b = idx/blockSize_;
                    const auto d = idx%blockSize_;
                    orderedDataPacked.push_back(static_cast<int64_t>(data_[b]->at(d)));
                }
            }
        }
        size_t numberOfElementsInKernel = orderedDataPacked.size() - prevNumOfElements; // Include padding
        offset += numberOfElementsInKernel * std::ceil(getDType().getSizeInBits()/8.0);
    }
    noneZeroElements_ = orderedDataPacked.size();

    return orderedDataPacked;
}

// Note: returns the number of ZeroPoints in a quantized tensor
int mv::Tensor::getZeroValuesCount()
{
    int numZeroPoints = 0;

    auto shape = getShape();
    std::vector<int64_t> zeroPoint = getZeroPointsPerChannel();

    int64_t datai;
    size_t outputChannels = shape[mv::KERNEL_OUTPUT_CHANNELS];
    size_t outputChannelSize = shape.totalSize() / outputChannels;

    // Filling the data on a per channel basis
    for (std::size_t k = 0; k < outputChannels; ++k)
    {
        for (std::size_t i = 0; i < outputChannelSize; i++)
        {
            std::vector<std::size_t> sub = getOrder().indToSub(shape, k*outputChannelSize + i);
            auto idx = internalOrder_.subToInd(shape, sub);
            auto b = idx/blockSize_;
            auto d = idx%blockSize_;
            datai = static_cast<int64_t>(data_[b]->at(d));
            if ( datai == zeroPoint[sub[mv::KERNEL_OUTPUT_CHANNELS]] )
                numZeroPoints++;
        }
    }

    return numZeroPoints;
}

std::vector<int64_t> mv::Tensor::getIntData()
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BULD)
    if (!isPopulated())
        throw ValueError(*this, "Attempt of restoring data from an unpopulated tensor");

    if (isDoubleType())
        throw ValueError(*this, "Attempt of reading int data from an double type tensor");

    std::vector<int64_t> orderedData(shape_.totalSize());

    const auto temp_dataTotal = data_.size() * blockSize_;
    if (getOrder() != internalOrder_)
    {
        for (std::size_t i = 0; i < temp_dataTotal; ++i)
        {
            const auto t = i / blockSize_;
            const auto u = i % blockSize_;
            const auto sub = internalOrder_.indToSub(shape_, i);
            const auto temp = getOrder().subToInd(shape_, sub);
            orderedData[temp] = data_[t]->at(u);
        }
    }
    else
    {
        for (std::size_t i = 0; i < temp_dataTotal; ++i)
        {
            const auto t = i / blockSize_;
            const auto u = i % blockSize_;
            orderedData[i] = data_[t]->at(u);
        }
    }
    return orderedData;
}

mv::DType mv::Tensor::getDType() const
{
    return get<DType>("dType");
}

mv::Order mv::Tensor::getOrder() const
{
    return get<Order>("order");
}

std::string mv::Tensor::toString() const
{
    return getLogID() + Element::attrsToString_();
}

std::string mv::Tensor::subTensorInfo() const
{
    std::string toReturn;
    if (hasSubTensors())
    {
        for (std::size_t i = 0; i < numSubTensors(); i++)
        {
            toReturn += subTensors_[i]->getShape().toString();
        }
    }

    return toReturn;
}

bool mv::Tensor::elementWiseChecks_(const Tensor& other)
{
    if (!isPopulated() || !other.isPopulated())
        throw ValueError(*this, "Unable to perfom element-wise operation using unpopulated tensor");

    Shape s1 = shape_, s2 = other.shape_;

    if (s1.ndims() == 0)
        throw ArgumentError(*this, "tensor:Shape:ndims", std::to_string(s1.ndims()),
             "0-dimensional tensor is illegal for element-wise operation");

    if (s2.ndims() == 0)
        throw ArgumentError(*this, "input tensor:Shape:ndims", std::to_string(s2.ndims()),
             "0-dimensional tensor is illegal for element-wise operation");

    if (s2.ndims() > s1.ndims())
        throw ArgumentError(*this, "input tensor:Shape:ndims", s2.toString(),
            "Currently unsupported in element wise in combination with " + s1.toString());

    for (std::size_t i = 1; i <= s2.ndims(); ++i)
        if (s1[-i] != s2[-i])
            throw ArgumentError(*this, "input tensor:Shape", s2.toString(),
                "Currently unsupported in combination with " + s1.toString());

    return (s1 == s2);
}

void mv::Tensor::elementWiseInt_(const Tensor& other, const std::function<int64_t(int64_t, int64_t)>& opFunc)
{
    std::size_t firstIdx = 0;
    while (firstIdx < data_.size())
    {   //currently this function is not called anywhere, TODO: Transform function to align with data_ instead of blocks_
        for (std::size_t secondIdx = 0; secondIdx < other.data_.size(); ++secondIdx)
        {
            std::transform(blocks_.at(firstIdx), blocks_.at(firstIdx) + blockSize_, other.blocks_.at(secondIdx), blocks_.at(firstIdx), opFunc);
            ++firstIdx;
        }
    }

}
void mv::Tensor::elementWiseDouble_(const Tensor& other, const std::function<double(double, double)>& opFunc)
{
    std::size_t firstIdx = 0;
    while (firstIdx < data_.size())
    {
        //currently this function is not called anywhere, TODO: Transform function to align with data_ instead of blocks_
        for (std::size_t secondIdx = 0; secondIdx < other.blocks_.size(); ++secondIdx)
        {
            std::transform(blocks_.at(firstIdx), blocks_.at(firstIdx) + blockSize_, other.blocks_.at(secondIdx), blocks_.at(firstIdx), opFunc);
            ++firstIdx;
        }
    }

}

void mv::Tensor::add(const Tensor& other)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_MATH)
    if (isDoubleType())
        elementWiseDouble_(other, std::plus<double>());
    else
        elementWiseInt_(other, std::plus<int64_t>());
}

void mv::Tensor::add(double val)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_MATH)
    if (!isPopulated())
        throw ValueError(*this, "Unable to perfom scalar addition operation for an unpopulated tensor");
    for(unsigned j = 0; j < data_.size(); ++j){
        for (unsigned i = 0; i < blockSize_; ++i)
            data_[j]->at(i) += val;
    }
}

void mv::Tensor::subtract(const Tensor& other)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_MATH)
    if (isDoubleType())
        elementWiseDouble_(other, std::minus<double>());
    else
        elementWiseInt_(other, std::minus<int64_t>());

}

void mv::Tensor::subtract(double val)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_MATH)
    if (!isPopulated())
        throw ValueError(*this, "Unable to perfom scalar subtraction operation for an unpopulated tensor");

    for (unsigned j = 0; j < data_.size(); ++j){
        for (unsigned i = 0; i < blockSize_; ++i)
            data_[j]->at(i) -= val;
    }
}

void mv::Tensor::multiply(const Tensor& other)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_MATH)
    if (isDoubleType())
        elementWiseDouble_(other, std::multiplies<double>());
    else
        elementWiseInt_(other, std::multiplies<int64_t>());

}

void mv::Tensor::multiply(double val)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_MATH)
    if (!isPopulated())
        throw ValueError(*this, "Unable to perfom scalar multiplication operation for an unpopulated tensor");
    for (unsigned j = 0; j < data_.size(); ++j){
        for(unsigned i = 0; i < blockSize_; ++i)
            data_[j]->at(i) *= val;
    }
}

void mv::Tensor::divide(double val)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_MATH)
    if (!isPopulated())
        throw ValueError(*this, "Unable to perfom scalar division operation for an unpopulated tensor");

    for (unsigned j = 0; j < data_.size(); ++j){
        for(unsigned i=0; i < blockSize_; ++i)
            data_[j]->at(i) /= val;
    }

}

void mv::Tensor::divide(const Tensor& other)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_MATH)
    if (isDoubleType())
        elementWiseDouble_(other, std::divides<double>());
    else
        elementWiseInt_(other, std::divides<int64_t>());

}

void mv::Tensor::sqrt()
{
    MV_PROFILED_FUNCTION(MV_PROFILE_MATH)
    if (!isPopulated())
        throw ValueError(*this, "Unable to perfom scalar square root operation for an unpopulated tensor");
    for (unsigned j = 0; j < data_.size(); ++j){
        for(unsigned i=0; i<blockSize_; ++i){
            if (isDoubleType())
                data_[j]->at(i) = std::sqrt(static_cast<double>(data_[j]->at(i)));
            else
                data_[j]->at(i) = std::sqrt(static_cast<int64_t>(data_[j]->at(i)));
        }
    }
}

mv::DataElement& mv::Tensor::at(const std::vector<std::size_t>& sub)
{
    if (!isPopulated())
        throw ValueError(*this, "Unable to access the data value for an unpopulated tensor");

    auto idx = subToInd(sub);
    auto b = idx/blockSize_;
    auto d = idx%blockSize_;
    return data_[b]->at(d);
}

const mv::DataElement& mv::Tensor::at(const std::vector<std::size_t>& sub) const
{
    if (!isPopulated())
        throw ValueError(*this, "Unable to access the data value for an unpopulated tensor");

    auto idx = subToInd(sub);
    auto b = idx/blockSize_;
    auto d = idx%blockSize_;
    return data_[b]->at(d);
}

mv::DataElement& mv::Tensor::at(std::size_t idx)
{
    return const_cast<DataElement&>(static_cast<const Tensor*>(this)->at(idx));
}

const mv::DataElement& mv::Tensor::at(std::size_t idx) const
{

    if (!isPopulated())
        throw ValueError(*this, "Unable to access the data value for an unpopulated tensor");
    auto temp_dataTotal = data_.size() * blockSize_;
    if (idx > temp_dataTotal)
        throw IndexError(*this, idx, "Exceeds the total lenght of data vector");

    if (hasAttr("master"))
    {
        std::vector<std::size_t> sub = indToSub(idx);
        auto temp = subToInd(sub);
        auto b = temp/blockSize_;
        auto d = temp%blockSize_;
        return data_[b]->at(d);
    }

    if (getOrder() == internalOrder_)
    {
        auto b = idx/blockSize_;
        auto d = idx%blockSize_;

        return data_[b]->at(d);
    }
    auto sub = getOrder().indToSub(shape_, idx);
    auto temp = internalOrder_.subToInd(shape_, sub);
    auto b = temp/blockSize_;
    auto d = temp%blockSize_;
    return data_[b]->at(d);

}

mv::DataElement& mv::Tensor::operator()(std::size_t idx)
{
    return at(idx);
}

const mv::DataElement& mv::Tensor::operator()(std::size_t idx) const
{
    return at(idx);
}

mv::DataElement& mv::Tensor::operator()(const std::vector<std::size_t>& sub)
{
    return at(sub);
}

mv::Tensor& mv::Tensor::operator=(const Tensor& other)
{
    MV_PROFILED_FUNCTION(MV_PROFILE_BASE)
    Element::operator=(other);
    shape_ = other.shape_;
    internalOrder_ = other.internalOrder_;
    blockSize_ = other.blockSize_;
    subTensors_ = other.subTensors_;
    kernelDataOffsets_ = other.kernelDataOffsets_;



    if (other.hasSubTensors())
    {
        for (std::size_t i = 0; i < other.numSubTensors(); i++)
        {
            subTensors_.push_back(std::make_shared<mv::Tensor>(*other.subTensors_[i]));
        }
    }

    if (other.isPopulated())
    {
        data_ = other.data_;
        blocks_ = other.blocks_;
    }

    if (other.isSparse()) {
        set<bool>("sparse", false);
        for (size_t i = 0; i < other.numSubTensors(); i++) {
            subTensors_[i]->set<bool>("sparse", false);
        }
        setSparse();
        if (isPopulated()) {
            sparsityMap_ = std::make_shared<Tensor>(*other.sparsityMap_);
            sparsityMap_->setName(createSparsityMapName(getName()));
        }
        if (other.hasAttr("address")) {
            setAddress(other.get<size_t>("address"));
        }
    }

    return *this;

}

const mv::DataElement& mv::Tensor::operator()(const std::vector<std::size_t>& sub) const
{
    return at(sub);
}

std::string mv::Tensor::getLogID() const
{
    return "Tensor:" + getName();
}

std::vector<float> mv::Tensor::computeNumericStrides() const
{
    return getOrder().computeByteStrides(shape_, getDType().getSizeInBits() / 8);
}


//isBase == true for implicit operations.
std::size_t mv::Tensor::getClusterSize(unsigned int alignment, bool isBase) const
{
    std::size_t res;
    if (!isBroadcasted())
    {
        res = 0;
        bool isTensorAligned = hasAttr("alignment") ? get<bool>("alignment") : false;

        for (size_t tIdx = 0; tIdx < subTensors_.size(); tIdx++)
        {
            auto size = subTensors_[tIdx]->computeTotalSize(alignment, isBase, isTensorAligned);
            if (size > res)
                res = size;
        }
    }
    else if (hasAttr("placedHSegmented") && get<bool>("placedHSegmented"))
    {
        res = 0;
        for (size_t tIdx = 0; tIdx < subTensors_.size(); tIdx++)
        {
            auto shapeToAllocate = get<mv::Shape>("shapeToAllocate");
            res = shapeToAllocate.totalSize() * std::ceil(getDType().getSizeInBits()/8.0);
        }
    }
    else
    {
        res = computeTotalSize(alignment, isBase);
    }

    return res;
}

std::size_t mv::Tensor::computeTotalSize(unsigned int alignment, bool isBase, bool fatherTensorAligned
                                         , bool graphOptimizer, bool dilation) const
{
    std::size_t res;
    bool isTensorPlaced = hasAttr("placed") ? get<bool>("placed") : false;
    mv::Shape shape;
    if (!isTensorPlaced)
        shape = getShape();
    else if (dilation)
        shape = get<mv::Shape>("originalShape");
    else
    {
        if (hasAttr("masterDim"))
            shape = get<mv::Shape>("masterDim");
        else
            throw ArgumentError(*this, "tesnsor with ", getName(), "is placed and does not have masterDim");
    }

    //use shape of master
    if (!isBase && hasAttr("master"))
    {
        if (hasAttr("leftPadding"))
        {
            auto padding = get<std::vector<std::size_t>>("leftPadding");
            for (std::size_t i = 0; i < padding.size(); ++i)
            {
                shape[i] += padding[i];
            }
        }
        if (hasAttr("rightPadding"))
        {
            auto padding = get<std::vector<std::size_t>>("rightPadding");
            for (std::size_t i = 0; i < padding.size(); ++i)
                shape[i] += padding[i];

        }
    }
    //NOTE :: Graph Optimizer MemorySize does not know about the alignment of the input/output Channels
    bool isTensorAligned = hasAttr("alignment") ? get<bool>("alignment") : false;
    size_t totalSize = shape.totalSize();
    //TODO update that to proper alignment, if this needs to align to 32 (splitOverK, each cluster has the whole tensor size, but need it aligned to 16*numclusters)
    if (isTensorAligned || fatherTensorAligned)
    {
        auto pad = alignment;
        auto outputChannels = shape[mv::IO_CHANNEL_DIMENSION];
        if (outputChannels % pad != 0)
        {
            auto paddedOutputChannels = mv::round_up(outputChannels, pad);
            totalSize = totalSize / outputChannels * paddedOutputChannels;
        }
    }
    else if (graphOptimizer)
    {
        auto pad = alignment;
        auto outputChannels = shape[mv::IO_CHANNEL_DIMENSION];
        if (outputChannels % pad != 0)
        {
            auto paddedOutputChannels = mv::round_up(outputChannels, pad);
            totalSize = totalSize / outputChannels * paddedOutputChannels;
        }
    }
    if (isSparse())
    {
        if (isPopulated())
        {
            res = noneZeroElements_ * std::ceil(getDType().getSizeInBits()/8.0); //TODO check if we need ceil here?
            res += getSparsityMap()->computeTotalSize();
        }
        else
        {
            //TODO what happens in this case
            res = shape.totalSize() * std::ceil(getDType().getSizeInBits()/8.0); //TODO check if we need ceil here?
            res += getSparsityMap()->computeTotalSize();
            res += getStorageElement()->computeTotalSize();
        }
    }
    else
    {
        res = totalSize * std::ceil(getDType().getSizeInBits()/8.0); //TODO check if we need ceil here?
    }
    //Round up to align to (alignment) 16 bytes
    res = mv::round_up(res, alignment);
    return res;
}

void mv::Tensor::shareAcrossClusters(std::vector<mv::Workload> workloads, unsigned int numClusters, bool clustering)
{
    if (isPopulated())
    {
        //NOTE Shape of Populated will be aligned already, so I am using the shape not the workload
        for (auto wlItr = workloads.begin(); wlItr != workloads.end(); wlItr++)
        {
            size_t idx = wlItr - workloads.begin();

            if (clustering)
            {
                subTensors_.push_back(std::make_shared<mv::Tensor>(getName() + "sub" + std::to_string(idx),
                    shape_, getDType(), getOrder()));
            }
            if (hasAttr("quantizationParams"))
                subTensors_[idx]->set<mv::QuantizationParams>("quantizationParams", get<mv::QuantizationParams>("quantizationParams"));
            if (isSparse())
                subTensors_[idx]->setSparse();
        }
        set<bool>("broadcasted", clustering == true && (numClusters > 1));
    }
    else
    {
        for (auto wlItr = workloads.begin(); wlItr != workloads.end(); wlItr++)
        {
            size_t idx = wlItr - workloads.begin();
            unsigned width = wlItr->MaxX - wlItr->MinX;
            unsigned height = wlItr->MaxY - wlItr->MinY;
            unsigned channels = wlItr->MaxZ - wlItr->MinZ;
            if (clustering)
            {
                mv::Shape newShape = {width, height, channels, 1};
                subTensors_.push_back(std::make_shared<mv::Tensor>(getName() + "sub" + std::to_string(idx), newShape, getDType(), getOrder()));
           }

            if (hasAttr("quantizationParams"))
                subTensors_[idx]->set<mv::QuantizationParams>("quantizationParams", get<mv::QuantizationParams>("quantizationParams"));
            if (isSparse())
                subTensors_[idx]->setSparse();
            if (hasAttr("overwriteStrategy") && get<std::string>("overwriteStrategy") == "SoHToClustering") {
                std::vector<std::size_t> offset(numClusters, 0);
                subTensors_[idx]->set<std::vector<std::size_t>>("offset", offset);
            }
        }
        set<bool>("broadcasted", (clustering && (numClusters > 1)));
    }
}

void mv::Tensor::splitPopulatedActivationAcrossClusters(std::vector<mv::Workload> workloads, bool splitOverH, bool multicast)
{
    auto shape = getShape();
    for (auto wlItr = workloads.begin(); wlItr != workloads.end(); wlItr++)
    {
        size_t idx = wlItr - workloads.begin();
        auto width = wlItr->MaxX - wlItr->MinX + 1;
        auto height = wlItr->MaxY - wlItr->MinY + 1;
        if (splitOverH)
        {
            mv::Shape newShape = { static_cast<size_t>(width), static_cast<size_t>(height) ,shape[2], shape[3]};
            subTensors_.push_back(std::make_shared<mv::Tensor>(getName() + "sub" + std::to_string(idx), newShape, getDType(), getOrder()));
            std::vector<std::size_t> offset = {static_cast<size_t>(wlItr->MinX), static_cast<size_t>(wlItr->MinY), 0 , 0};
            subTensors_[idx]->set<std::vector<std::size_t>>("offset", offset);
        }
        else
        {
            mv::Shape newShape = { shape[0], shape[1] , static_cast<size_t>(width), static_cast<size_t>(height)};
            subTensors_.push_back(std::make_shared<mv::Tensor>(getName() + "sub" + std::to_string(idx), newShape, getDType(), getOrder()));
            std::vector<std::size_t> offset =  {0 , 0, static_cast<size_t>(wlItr->MinX), static_cast<size_t>(wlItr->MinY)};
            subTensors_[idx]->set<std::vector<std::size_t>>("offset", offset);
        }

        if (hasAttr("quantParams"))
            subTensors_[idx]->set<mv::QuantizationParams>("quantParams", get<mv::QuantizationParams>("quantParams"));
        if (isSparse())
            subTensors_[idx]->setSparse();

    }

    set<bool>("broadcasted", (!splitOverH || multicast));
}

void mv::Tensor::splitAcrossClusters(std::vector<mv::Workload> workloads, bool splitOverH, bool multicast, bool doubleStrategy)
{

    if (isPopulated())
    {
        auto shape = getShape();
        for (auto wlItr = workloads.begin(); wlItr != workloads.end(); wlItr++)
        {
            size_t idx = wlItr - workloads.begin();
            auto width = wlItr->MaxX - wlItr->MinX + 1;
            auto height = wlItr->MaxY - wlItr->MinY + 1;
            if (splitOverH)
            {
                subTensors_.push_back(std::make_shared<mv::Tensor>(getName() + "sub" + std::to_string(idx),
                    shape_, getDType(), getOrder()));
            }
            else
            {
                mv::Shape newShape = { shape[0], shape[1] , static_cast<size_t>(width), static_cast<size_t>(height)};
                auto order = getOrder();

                // NOTE: Physically copying the data to subtensors is needed when the tensor is sparse
                // since we need to sparsify the subtensors to get the kernel data offsets.

                // NOTE: Physically copying the data to subtensors is needed for SOK weights
                // because we need to compress the subtensors indivdually as they are decompressed individually during the DMA to each cluster
                if (isSparse() || (hasAttr("splitStrategy") && this->get<std::string>("splitStrategy") == "SplitOverK"))
                {
                    std::vector<mv::DataElement> splittedData(newShape.totalSize(), mv::DataElement(this->isDoubleType()));
                    size_t nOffset = static_cast<size_t>(wlItr->MinY);
                    size_t cOffset = static_cast<size_t>(wlItr->MinX);
                    for (size_t n = 0; n < newShape[3]; n++)
                        for (size_t c = 0; c < newShape[2]; c++)
                            for (size_t h = 0; h < newShape[1]; h++)
                                for (size_t w = 0; w < newShape[0]; w++)
                                    splittedData[order.subToInd(newShape, {w, h, c, n})] = this->at({w , h, c+cOffset, n+nOffset});
                    subTensors_.push_back(std::make_shared<mv::Tensor>(getName() + "sub" + std::to_string(idx),
                        newShape, getDType(), order, splittedData));
                }
                else
                {
                    subTensors_.push_back(std::make_shared<mv::Tensor>(getName() + "sub" + std::to_string(idx),
                        newShape, getDType(), order));
                }
            }
            std::vector<std::size_t> offset = {0 , 0,
                static_cast<size_t>(wlItr->MinX), static_cast<size_t>(wlItr->MinY)};
            subTensors_[idx]->set<std::vector<std::size_t>>("offset", offset);

            if (hasAttr("quantParams"))
                subTensors_[idx]->set<mv::QuantizationParams>("quantParams", get<mv::QuantizationParams>("quantParams"));
            if (isSparse())
                subTensors_[idx]->setSparse();
        }
        set<bool>("broadcasted", (splitOverH == true));
    }
    else
    {
        auto shape = getShape();
        for (auto wlItr = workloads.begin(); wlItr != workloads.end(); wlItr++)
        {
            size_t idx = wlItr - workloads.begin();
            auto width = wlItr->MaxX - wlItr->MinX + 1;
            auto height = wlItr->MaxY - wlItr->MinY + 1;

            if (isSparse() && doubleStrategy)
            {
                mv::Shape newShape = { static_cast<size_t>(width), static_cast<size_t>(height) ,shape[2], shape[3]};
                subTensors_.push_back(std::make_shared<mv::Tensor>(getName() + "subSparse" + std::to_string(idx), newShape, getDType(), getOrder()));
                std::vector<std::size_t> offset = {static_cast<size_t>(wlItr->MinX), static_cast<size_t>(wlItr->MinY), 0 , 0};
                subTensors_[idx]->set<std::vector<std::size_t>>("offset", offset);
                auto is_last_subtensor = (workloads.size() - 1 == idx);
                auto is_sparse = (isSparse() || ((this->hasAttr("needs_sparse") && this->get<bool>("needs_sparse"))));

                if (is_sparse && is_last_subtensor){
                    subTensors_[idx]->set<bool>("use_sub0_addrs", true);
                }

                if (hasAttr("quantParams"))
                    subTensors_[idx + workloads.size()]->set<mv::QuantizationParams>("quantParams", get<mv::QuantizationParams>("quantParams"));
                if (isSparse())
                    subTensors_[idx + workloads.size()]->setSparse();

            }
            else if (splitOverH || doubleStrategy)
            {
                mv::Shape newShape = { static_cast<size_t>(width), static_cast<size_t>(height) ,shape[2], shape[3]};
                subTensors_.push_back(std::make_shared<mv::Tensor>(getName() + "sub" + std::to_string(idx), newShape, getDType(), getOrder()));
                std::vector<std::size_t> offset = {static_cast<size_t>(wlItr->MinX), static_cast<size_t>(wlItr->MinY), 0 , 0};
                subTensors_[idx]->set<std::vector<std::size_t>>("offset", offset);
                auto is_last_subtensor = (workloads.size() - 1 == idx);
                auto is_sparse = (isSparse() || ((this->hasAttr("needs_sparse") && this->get<bool>("needs_sparse"))));

                if (is_sparse && is_last_subtensor){
                    subTensors_[idx]->set<bool>("use_sub0_addrs", true);
                }
            }
            else
            {
                mv::Shape newShape = { shape[0], shape[1] , static_cast<size_t>(width), static_cast<size_t>(height)};
                subTensors_.push_back(std::make_shared<mv::Tensor>(getName() + "sub" + std::to_string(idx), newShape, getDType(), getOrder()));
                std::vector<std::size_t> offset =  {0 , 0, static_cast<size_t>(wlItr->MinX), static_cast<size_t>(wlItr->MinY)};
                subTensors_[idx]->set<std::vector<std::size_t>>("offset", offset);
            }

            if (hasAttr("quantParams"))
                subTensors_[idx]->set<mv::QuantizationParams>("quantParams", get<mv::QuantizationParams>("quantParams"));
            if (isSparse())
                subTensors_[idx]->setSparse();

            if (!splitOverH || multicast)
            {
                std::vector<std::size_t> leftPadding = subTensors_[idx]->get<std::vector<std::size_t>>("offset");
                std::vector<std::size_t> rightPadding(shape.ndims());
                for (size_t i = 0; i < rightPadding.size(); i++)
                    rightPadding[i] = shape[i] - leftPadding[i] - subTensors_[idx]->shape_[i];
                subTensors_[idx]->bindData(*this, leftPadding, rightPadding);
            }
        }

        set<bool>("broadcasted", (!splitOverH || multicast));
    }
}

void mv::Tensor::splitFusedAcrossClusters(std::vector<mv::Shape> workloads)
{

    if (isPopulated())
    {
        size_t nOffset = 0UL;
        for (std::size_t idx = 0; idx < workloads.size(); ++idx)
        {
            std::size_t unit = 1;
            std::size_t batch_shape = 0;
            batch_shape = workloads[idx][mv::IO_BATCH_DIMENSION];
            mv::Shape newShape = { unit, unit, unit, batch_shape};
            auto order = getOrder();

            if ((hasAttr("splitStrategy") && this->get<std::string>("splitStrategy") == "SplitOverK"))
            {
                std::vector<mv::DataElement> splittedData(newShape.totalSize(), mv::DataElement(this->isDoubleType()));
                for (size_t n = 0; n < newShape[3]; n++)
                    for (size_t c = 0; c < newShape[2]; c++)
                        for (size_t h = 0; h < newShape[1]; h++)
                            for (size_t w = 0; w < newShape[0]; w++)
                                splittedData[order.subToInd(newShape, {w, h, c, n})] = this->at({w , h, c, n+nOffset});
                subTensors_.push_back(std::make_shared<mv::Tensor>(getName() + "sub" + std::to_string(idx),
                    newShape, getDType(), order, splittedData));
                nOffset += batch_shape;
            }

            std::vector<std::size_t> offset = {0 , 0,
                0, idx * workloads[idx][mv::IO_BATCH_DIMENSION]};
            subTensors_[idx]->set<std::vector<std::size_t>>("offset", offset);

            if (hasAttr("quantParams"))
                subTensors_[idx]->set<mv::QuantizationParams>("quantParams", get<mv::QuantizationParams>("quantParams"));
        }
    }

    set<bool>("broadcasted", false);

}

void mv::Tensor::cleanSubtensors()
{
        subTensors_ = {};
}

mv::Shape mv::Tensor::getShape() const
{
    return shape_;
}

int mv::Tensor::computeAppropriatePadding() const
{
    int pad = 0;
    if (getDType() == mv::DType("Float16"))
        pad = 8;
    else if (getDType() == mv::DType("UInt8"))
        pad = 16;
    else
        throw DTypeError(*this, "Incorrect DType");
    return pad;
}

std::set<std::string> mv::Tensor::getFlowNames() const {
    return get<std::set<std::string>>("flows");
}
