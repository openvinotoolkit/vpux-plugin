#ifndef MV_DTYPE_ENTRY
#define MV_DTYPE_ENTRY

#include <vector>
#include <functional>
#include "include/mcm/tensor/binary_data.hpp"

namespace mv
{
    class DType;

    class DTypeEntry
    {

        std::string name_;
        std::function<BinaryData(const std::vector<double>&)> toBinaryFunc_;
        unsigned size_;

    public:

        DTypeEntry(const std::string& name);
        DTypeEntry& setToBinaryFunc(std::function<BinaryData(const std::vector<double>&)>& f);
        DTypeEntry& setSizeInBits(unsigned size);
        unsigned getSizeInBits() const;
        const std::function<BinaryData(const std::vector<double>&)>& getToBinaryFunc() const;
    };

}

#endif // MV_DTYPE_ENTRY
