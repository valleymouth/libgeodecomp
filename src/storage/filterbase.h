#ifndef LIBGEODECOMP_STORAGE_FILTERBASE_H
#define LIBGEODECOMP_STORAGE_FILTERBASE_H

#include <libgeodecomp/config.h>

#ifdef LIBGEODECOMP_WITH_MPI
#include <libgeodecomp/communication/typemaps.h>
#endif

namespace LibGeoDecomp {

/**
 * Base class for adding user-defined data filters to a Selector.
 * This can be used to do on-the-fly data extraction, scale
 * conversion for live output etc. without having to rewrite a
 * complete ParallelWriter output plugin.
 *
 * It is suggested to derive from Filter instead of FilterBase, as
 * the latter has some convenience functionality already in place.
 */
template<typename CELL>
class FilterBase
{
public:
    friend class Serialization;

    virtual ~FilterBase()
    {}

    virtual std::size_t sizeOf() const = 0;
#ifdef LIBGEODECOMP_WITH_SILO
    virtual int siloTypeID() const = 0;
#endif
#ifdef LIBGEODECOMP_WITH_MPI
    /**
     * Yields the member's MPI data type (or that of its external
     * representation). May source from APITraits or fall back to
     * Typemaps. If neither yields, no compiler error will follow
     * as it is assumed that such code is still valid (e.g. if a
     * Selector is instantiated for the SiloWriter, so that
     * mpiDatatype() is never called).
     */
    virtual MPI_Datatype mpiDatatype() const = 0;
#endif
    virtual std::string typeName() const = 0;
    virtual int arity() const = 0;
    virtual void copyStreakIn(const char *first, const char *last, char *target) = 0;
    virtual void copyStreakOut(const char *first, const char *last, char *target) = 0;
    virtual void copyMemberIn(
        const char *source, CELL *target, int num, char CELL:: *memberPointer) = 0;
    virtual void copyMemberOut(
        const CELL *source, char *target, int num, char CELL:: *memberPointer) = 0;
    virtual bool checkExternalTypeID(const std::type_info& otherID) const = 0;
};

}

#endif
