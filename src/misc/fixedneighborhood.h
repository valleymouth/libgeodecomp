#ifndef LIBGEODECOMP_MISC_FIXEDNEIGHBORHOOD_H
#define LIBGEODECOMP_MISC_FIXEDNEIGHBORHOOD_H

#include <libflatarray/flat_array.hpp>
#include <libgeodecomp/misc/fixedcoord.h>

namespace LibGeoDecomp {

namespace FixedNeighborhoodHelpers {

template<int DIM, int DISTANCE>
class NormalizeCoord
{
public:
    
};

}

/**
 * Similar to LinePointerNeighborhood, this class serves as a proxy
 * for cells to acccess their neighbors during update. In contrast to
 * CoordMap and LinePointerNeighborhood, class requires grid
 * dimensions to be known at compile time. The benefit is that we can
 * significantly reduce runtime overhead.
 */
template<typename CELL, typename TOPOLOGY, int DIM_X, int DIM_Y, int DIM_Z, int INDEX>
class FixedNeighborhood
{
public:
    typedef LibFlatArray::soa_accessor<CELL, DIM_X, DIM_Y, DIM_Z, INDEX> SoAAccessor;

    __host__ __device__
    FixedNeighborhood(const SoAAccessor& accessor) :
        accessor(accessor)
    {}

    template<int X, int Y, int Z>
    __host__ __device__
   const LibFlatArray::soa_accessor<CELL, LIBFLATARRAY_PARAMS> operator[](FixedCoord<X, Y, Z>) const
    {
        return accessor[LibFlatArray::coord<X, Y, Z>()];
    }

private:
    const SoAAccessor& accessor;
};

}

#endif
