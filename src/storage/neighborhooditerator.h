#ifndef LIBGEODECOMP_STORAGE_NEIGHBORHOODITERATOR_H
#define LIBGEODECOMP_STORAGE_NEIGHBORHOODITERATOR_H

#include <libgeodecomp/geometry/coord.h>
#include <libgeodecomp/geometry/coordbox.h>
#include <libgeodecomp/storage/collectioninterface.h>

namespace LibGeoDecomp {

/**
 * This class is meant to be used with BoxCell and alike to interface
 * MD and n-body codes with our standard neighborhood types. It allows
 * models to transparently traverse all particles in their neighboring
 * containers.
 */
template<class NEIGHBORHOOD, typename CARGO, int DIM>
class NeighborhoodIterator
{
public:
    friend class NeighborhoodIteratorTest;

    typedef NEIGHBORHOOD Neighborhood;
    typedef typename Neighborhood::Cell Cell;
    typedef typename Cell::const_iterator CellIterator;
    typedef typename Cell::value_type Particle;

    inline NeighborhoodIterator(
        const Neighborhood& hood,
        const Coord<DIM>& coord,
        const CellIterator& iterator) :
        hood(hood),
        boxIterator(
            typename CoordBox<DIM>::Iterator(
                Coord<DIM>::diagonal(-1),
                coord,
                Coord<DIM>::diagonal(3))),
        endIterator(
            CoordBox<DIM>(
                Coord<DIM>::diagonal(-1),
                Coord<DIM>::diagonal(3)).end()),
        cell(&hood[coord]),
        iterator(iterator)
    {}

    static inline NeighborhoodIterator begin(const Neighborhood& hood)
    {
        CoordBox<DIM> box(Coord<DIM>::diagonal(-1), Coord<DIM>::diagonal(3));

        for (typename CoordBox<DIM>::Iterator i = box.begin();
             i != box.end();
             ++i) {
            if (hood[*i].size() > 0) {
                return NeighborhoodIterator(hood, *i, hood[*i].begin());
            }
        }

        Coord<DIM> endCoord = Coord<DIM>::diagonal(1);
        return NeighborhoodIterator(hood, endCoord, hood[endCoord].end());
    }

    static inline NeighborhoodIterator end(const Neighborhood& hood)
    {
        return NeighborhoodIterator(
            hood,
            Coord<DIM>::diagonal(1),
            hood[Coord<DIM>::diagonal(1)].end());
    }

    inline const Particle& operator*() const
    {
        return *iterator;
    }

    inline CellIterator operator->() const
    {
        return iterator;
    }

    inline void operator++()
    {
        ++iterator;

        while (iterator == cell->end()) {
            ++boxIterator;

            // this check is required to avoid dereferentiation of the
            // neighborhood with an out-of-range coordinate.
            if (boxIterator == endIterator) {
                return;
            }

            cell = &hood[*boxIterator];
            iterator = cell->begin();
        }
    }

    inline bool operator==(const NeighborhoodIterator& other) const
    {
        return (cell == other.cell) && (iterator == other.iterator);
    }


    inline bool operator!=(const NeighborhoodIterator& other) const
    {
        return !(*this == other);
    }

private:
    const Neighborhood& hood;
    typename CoordBox<DIM>::Iterator boxIterator;
    typename CoordBox<DIM>::Iterator endIterator;
    const Cell *cell;
    CellIterator iterator;

};

}

#endif
