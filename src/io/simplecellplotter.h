#ifndef LIBGEODECOMP_IO_SIMPLECELLPLOTTER_H
#define LIBGEODECOMP_IO_SIMPLECELLPLOTTER_H

#include <libgeodecomp/io/initializer.h>
#include <libgeodecomp/misc/palette.h>
#include <libgeodecomp/storage/filter.h>
#include <libgeodecomp/storage/selector.h>

#include <boost/shared_ptr.hpp>
#include <stdexcept>

namespace LibGeoDecomp {

namespace SimpleCellPlotterHelpers {

template<typename CELL, typename MEMBER, typename PALETTE>
class CellToColor : public Filter<CELL, MEMBER, Color>
{
public:
    CellToColor(const PALETTE& palette) :
        palette(palette)
    {}

    void copyStreakInImpl(const Color *first, const Color *last, MEMBER *target)
    {
        throw std::logic_error("undefined behavior: can only convert members to colors, not the other way around");
    }

    void copyStreakOutImpl(const MEMBER *first, const MEMBER *last, Color *target)
    {
        for (const MEMBER *i = first; i != last; ++i) {
            *target = palette[*i];
        }
    }

    void copyMemberInImpl(
        const Color *source, CELL *target, int num, MEMBER CELL:: *memberPointer)
    {

        throw std::logic_error("undefined behavior: can only convert cells to colors, not the other way around");
    }

    void copyMemberOutImpl(
        const CELL *source, Color *target, int num, MEMBER CELL:: *memberPointer)
    {
        for (int i = 0; i < num; ++i) {
            target[i] = palette[source[i].*memberPointer];
        }
    }

private:
    PALETTE palette;
};

}

/**
 * This is a convenience class which uses a Palette to map a single
 * value of a cell to a color range.
 */
template<typename CELL_TYPE>
class SimpleCellPlotter
{
public:
    template<typename MEMBER, typename PALETTE>
    explicit SimpleCellPlotter(MEMBER CELL_TYPE:: *memberPointer, const PALETTE& palette) :
        cellToColor(
            memberPointer,
            "unnamed parameter",
            boost::shared_ptr<FilterBase<CELL_TYPE> >(
                new SimpleCellPlotterHelpers::CellToColor<CELL_TYPE, MEMBER, PALETTE>(palette)))
    {}

    template<typename PAINTER>
    void operator()(
        const CELL_TYPE& cell,
	PAINTER& painter,
        const Coord<2>& cellDimensions) const
    {
        Color color;
        cellToColor.copyMemberOut(&cell, reinterpret_cast<char*>(&color), 1);

        painter.fillRect(
            0, 0,
            cellDimensions.x(), cellDimensions.y(),
            color);
    }

private:
    Selector<CELL_TYPE> cellToColor;
};

}

#endif
