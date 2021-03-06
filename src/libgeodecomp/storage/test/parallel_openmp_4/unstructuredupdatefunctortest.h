#include <cxxtest/TestSuite.h>

#include <libgeodecomp/config.h>
#include <libgeodecomp/misc/apitraits.h>
#include <libgeodecomp/storage/sellcsigmasparsematrixcontainer.h>
#include <libgeodecomp/storage/updatefunctor.h>
#include <libgeodecomp/storage/unstructuredgrid.h>
#include <libgeodecomp/storage/unstructuredlooppeeler.h>
#include <libgeodecomp/storage/unstructuredsoagrid.h>
#include <libgeodecomp/storage/unstructuredupdatefunctor.h>

#include <libflatarray/api_traits.hpp>
#include <libflatarray/macros.hpp>
#include <libflatarray/soa_accessor.hpp>
#include <libflatarray/short_vec.hpp>

#include <vector>
#include <map>

using namespace LibGeoDecomp;
using namespace LibFlatArray;

#ifdef LIBGEODECOMP_WITH_CPP14

class EmptyUnstructuredTestCellAPI
{};

class DefaultUnstructuredTestCellAPI : public APITraits::HasUpdateLineX
{};

template<int SIGMA, typename ADDITIONAL_API = DefaultUnstructuredTestCellAPI>
class SimpleUnstructuredTestCell
{
public:
    class API :
        public ADDITIONAL_API,
        public APITraits::HasUnstructuredTopology,
        public APITraits::HasSellType<double>,
        public APITraits::HasSellMatrices<1>,
        public APITraits::HasSellC<4>,
        public APITraits::HasSellSigma<SIGMA>
    {};

    inline explicit SimpleUnstructuredTestCell(double v = 0) :
        value(v), sum(0)
    {}

    template<typename HOOD_NEW, typename HOOD_OLD>
    static void updateLineX(HOOD_NEW& hoodNew, int indexEnd, HOOD_OLD& hoodOld, unsigned /* nanoStep */)
    {
        for (; hoodOld.index() < indexEnd; ++hoodOld) {
            hoodNew->sum = 0.0;
            for (const auto& j: hoodOld.weights(0)) {
                hoodNew->sum += hoodOld[j.first()].value * j.second();
            }
            ++hoodNew;
        }
    }

    template<typename NEIGHBORHOOD>
    void update(NEIGHBORHOOD& neighborhood, unsigned /* nanoStep */)
    {
        sum = 0.;
        for (const auto& j: neighborhood.weights(0)) {
            sum += neighborhood[j.first()].value * j.second();
        }
    }

    inline bool operator==(const SimpleUnstructuredTestCell& other)
    {
        return sum == other.sum;
    }

    inline bool operator!=(const SimpleUnstructuredTestCell& other)
    {
        return !(*this == other);
    }

    double value;
    double sum;
};

template<int SIGMA>
class SimpleUnstructuredSoATestCell
{
public:
    typedef short_vec<double, 4> ShortVec;

    class API :
        public APITraits::HasUpdateLineX,
        public APITraits::HasSoA,
        public APITraits::HasUnstructuredTopology,
        public APITraits::HasPredefinedMPIDataType<double>,
        public APITraits::HasSellType<double>,
        public APITraits::HasSellMatrices<1>,
        public APITraits::HasSellC<4>,
        public APITraits::HasSellSigma<SIGMA>,
        public APITraits::HasThreadedUpdate<16>,
        public LibFlatArray::api_traits::has_default_1d_sizes
    {};

    inline
    explicit SimpleUnstructuredSoATestCell(double v = 0) :
        value(v),
        sum(0)
    {}

    template<typename HOOD_NEW, typename HOOD_OLD>
    static void updateLineX(HOOD_NEW& hoodNew, int indexEnd, HOOD_OLD& hoodOld, unsigned /* nanoStep */)
    {
        unstructuredLoopPeeler<ShortVec>(
            &hoodNew.index(),
            indexEnd,
            hoodOld,
            [&hoodNew](auto REAL, auto *counter, const auto& end, auto& hoodOld) {
                typedef decltype(REAL) ShortVec;
                for (; hoodNew.index() < end; hoodNew += ShortVec::ARITY) {
                    ShortVec tmp;
                    tmp.load_aligned(&hoodNew->sum());

                    for (const auto& j: hoodOld.weights()) {
                        ShortVec weights, values;
                        weights.load_aligned(j.second());
                        values.gather(&hoodOld->value(), j.first());
                        tmp += values * weights;

                    }

                    &hoodNew->sum() << tmp;
                    ++hoodOld;
                }
            });
    }

    template<typename NEIGHBORHOOD>
    void update(NEIGHBORHOOD& neighborhood, unsigned /* nanoStep */)
    {
        sum = 0.;
        for (const auto& j: neighborhood.weights(0)) {
            sum += neighborhood[j.first()].value * j.second();
        }
    }

    inline bool operator==(const SimpleUnstructuredSoATestCell& cell) const
    {
        return cell.sum == sum;
    }

    inline bool operator!=(const SimpleUnstructuredSoATestCell& cell) const
    {
        return !(*this == cell);
    }

    double value;
    double sum;
};

LIBFLATARRAY_REGISTER_SOA(SimpleUnstructuredSoATestCell<1 >, ((double)(sum))((double)(value)))
LIBFLATARRAY_REGISTER_SOA(SimpleUnstructuredSoATestCell<60>, ((double)(sum))((double)(value)))
#endif

namespace LibGeoDecomp {

class UnstructuredUpdateFunctorTest : public CxxTest::TestSuite
{
public:
    void testBasicSansUpdateLineXConcurrencyFalseFalse()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        Coord<1> dim(DIM);
        Region<1> boundingRegion;
        boundingRegion << CoordBox<1>(Coord<1>(0), dim);

        typedef SimpleUnstructuredTestCell<1, EmptyUnstructuredTestCellAPI> TestCellType;
        TestCellType defaultCell(211);
        TestCellType edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredGrid<TestCellType, 1, double, 4, 1> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        Region<1> region;
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(40),   60);
        region << Streak<1>(Coord<1>(100), 150);

        // weights matrix looks like this: 1 0 1 0 1 0 ...
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < DIM; col += 2) {
                matrix << std::make_pair(Coord<2>(row, col), 1);
            }
        }
        gridOld.setWeights(0, matrix);

        UnstructuredUpdateFunctor<TestCellType > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(false, false);
        APITraits::SelectThreadedUpdate<TestCellType>::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (((coord.x() >=  10) && (coord.x() <  30)) ||
                ((coord.x() >=  40) && (coord.x() <  60)) ||
                ((coord.x() >= 100) && (coord.x() < 150))) {
                const double sum = (DIM / 2.0) * 211.0;
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testBasicSansUpdateLineXConcurrencyFalseTrue()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        Coord<1> dim(DIM);
        Region<1> boundingRegion;
        boundingRegion << CoordBox<1>(Coord<1>(0), dim);

        typedef SimpleUnstructuredTestCell<1, EmptyUnstructuredTestCellAPI> TestCellType;
        TestCellType defaultCell(222);
        TestCellType edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredGrid<TestCellType, 1, double, 4, 1> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        Region<1> region;
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(40),   60);
        region << Streak<1>(Coord<1>(100), 150);

        // weights matrix looks like this: 1 0 1 0 1 0 ...
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < DIM; col += 2) {
                matrix << std::make_pair(Coord<2>(row, col), 1);
            }
        }
        gridOld.setWeights(0, matrix);

        UnstructuredUpdateFunctor<TestCellType > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(false, true);
        APITraits::SelectThreadedUpdate<TestCellType>::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (((coord.x() >=  10) && (coord.x() <  30)) ||
                ((coord.x() >=  40) && (coord.x() <  60)) ||
                ((coord.x() >= 100) && (coord.x() < 150))) {
                const double sum = (DIM / 2.0) * 222.0;
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testBasicSansUpdateLineXConcurrencyTrueFalse()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        Coord<1> dim(DIM);
        Region<1> boundingRegion;
        boundingRegion << CoordBox<1>(Coord<1>(0), dim);

        typedef SimpleUnstructuredTestCell<1, EmptyUnstructuredTestCellAPI> TestCellType;
        TestCellType defaultCell(233);
        TestCellType edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredGrid<TestCellType, 1, double, 4, 1> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        Region<1> region;
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(40),   60);
        region << Streak<1>(Coord<1>(100), 150);

        // weights matrix looks like this: 1 0 1 0 1 0 ...
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < DIM; col += 2) {
                matrix << std::make_pair(Coord<2>(row, col), 1);
            }
        }
        gridOld.setWeights(0, matrix);

        UnstructuredUpdateFunctor<TestCellType > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(true, false);
        APITraits::SelectThreadedUpdate<TestCellType>::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (((coord.x() >=  10) && (coord.x() <  30)) ||
                ((coord.x() >=  40) && (coord.x() <  60)) ||
                ((coord.x() >= 100) && (coord.x() < 150))) {
                const double sum = (DIM / 2.0) * 233.0;
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testBasicSansUpdateLineXConcurrencyTrueTrue()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        Coord<1> dim(DIM);
        Region<1> boundingRegion;
        boundingRegion << CoordBox<1>(Coord<1>(0), dim);

        typedef SimpleUnstructuredTestCell<1, EmptyUnstructuredTestCellAPI> TestCellType;
        TestCellType defaultCell(244);
        TestCellType edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredGrid<TestCellType, 1, double, 4, 1> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        Region<1> region;
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(40),   60);
        region << Streak<1>(Coord<1>(100), 150);

        // weights matrix looks like this: 1 0 1 0 1 0 ...
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < DIM; col += 2) {
                matrix << std::make_pair(Coord<2>(row, col), 1);
            }
        }
        gridOld.setWeights(0, matrix);

        UnstructuredUpdateFunctor<TestCellType > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(true, true);
        APITraits::SelectThreadedUpdate<TestCellType>::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (((coord.x() >=  10) && (coord.x() <  30)) ||
                ((coord.x() >=  40) && (coord.x() <  60)) ||
                ((coord.x() >= 100) && (coord.x() < 150))) {
                const double sum = (DIM / 2.0) * 244.0;
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testBasicWithUpdateLineXConcurrencyFalseFalse()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        Coord<1> dim(DIM);
        Region<1> boundingRegion;
        boundingRegion << CoordBox<1>(Coord<1>(0), dim);

        typedef SimpleUnstructuredTestCell<1> TestCellType;
        TestCellType defaultCell(255);
        TestCellType edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredGrid<TestCellType, 1, double, 4, 1> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        Region<1> region;
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(40),   60);
        region << Streak<1>(Coord<1>(100), 150);

        // weights matrix looks like this: 1 0 1 0 1 0 ...
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < DIM; col += 2) {
                matrix << std::make_pair(Coord<2>(row, col), 1);
            }
        }
        gridOld.setWeights(0, matrix);

        UnstructuredUpdateFunctor<TestCellType > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(false, false);
        APITraits::SelectThreadedUpdate<TestCellType>::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (((coord.x() >=  10) && (coord.x() <  30)) ||
                ((coord.x() >=  40) && (coord.x() <  60)) ||
                ((coord.x() >= 100) && (coord.x() < 150))) {
                const double sum = (DIM / 2.0) * 255.0;
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testBasicWithUpdateLineXConcurrencyFalseTrue()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        Coord<1> dim(DIM);
        Region<1> boundingRegion;
        boundingRegion << CoordBox<1>(Coord<1>(0), dim);

        typedef SimpleUnstructuredTestCell<1> TestCellType;
        TestCellType defaultCell(266);
        TestCellType edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredGrid<TestCellType, 1, double, 4, 1> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        Region<1> region;
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(40),   60);
        region << Streak<1>(Coord<1>(100), 150);

        // weights matrix looks like this: 1 0 1 0 1 0 ...
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < DIM; col += 2) {
                matrix << std::make_pair(Coord<2>(row, col), 1);
            }
        }
        gridOld.setWeights(0, matrix);

        UnstructuredUpdateFunctor<TestCellType > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(false, true);
        APITraits::SelectThreadedUpdate<TestCellType>::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (((coord.x() >=  10) && (coord.x() <  30)) ||
                ((coord.x() >=  40) && (coord.x() <  60)) ||
                ((coord.x() >= 100) && (coord.x() < 150))) {
                const double sum = (DIM / 2.0) * 266.0;
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testBasicWithUpdateLineXConcurrencyTrueFalse()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        Coord<1> dim(DIM);
        Region<1> boundingRegion;
        boundingRegion << CoordBox<1>(Coord<1>(0), dim);

        typedef SimpleUnstructuredTestCell<1> TestCellType;
        TestCellType defaultCell(277);
        TestCellType edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredGrid<TestCellType, 1, double, 4, 1> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        Region<1> region;
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(40),   60);
        region << Streak<1>(Coord<1>(100), 150);

        // weights matrix looks like this: 1 0 1 0 1 0 ...
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < DIM; col += 2) {
                matrix << std::make_pair(Coord<2>(row, col), 1);
            }
        }
        gridOld.setWeights(0, matrix);

        UnstructuredUpdateFunctor<TestCellType > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(true, false);
        APITraits::SelectThreadedUpdate<TestCellType>::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (((coord.x() >=  10) && (coord.x() <  30)) ||
                ((coord.x() >=  40) && (coord.x() <  60)) ||
                ((coord.x() >= 100) && (coord.x() < 150))) {
                const double sum = (DIM / 2.0) * 277.0;
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testBasicWithUpdateLineXConcurrencyTrueTrue()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        Coord<1> dim(DIM);
        Region<1> boundingRegion;
        boundingRegion << CoordBox<1>(Coord<1>(0), dim);

        typedef SimpleUnstructuredTestCell<1> TestCellType;
        TestCellType defaultCell(288);
        TestCellType edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredGrid<TestCellType, 1, double, 4, 1> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        Region<1> region;
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(40),   60);
        region << Streak<1>(Coord<1>(100), 150);

        // weights matrix looks like this: 1 0 1 0 1 0 ...
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < DIM; col += 2) {
                matrix << std::make_pair(Coord<2>(row, col), 1);
            }
        }
        gridOld.setWeights(0, matrix);

        UnstructuredUpdateFunctor<TestCellType > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(true, true);
        APITraits::SelectThreadedUpdate<TestCellType>::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (((coord.x() >=  10) && (coord.x() <  30)) ||
                ((coord.x() >=  40) && (coord.x() <  60)) ||
                ((coord.x() >= 100) && (coord.x() < 150))) {
                const double sum = (DIM / 2.0) * 288.0;
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testBasicWithSigmaButSansUpdateLineXConcurrencyFalseFalse()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        Coord<1> dim(DIM);
        Region<1> boundingRegion;
        boundingRegion << CoordBox<1>(Coord<1>(0), dim);

        typedef SimpleUnstructuredTestCell<128, EmptyUnstructuredTestCellAPI> TestCellType;
        TestCellType defaultCell(311);
        TestCellType edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredGrid<TestCellType, 1, double, 4, 128> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        // weights matrix looks like this:
        // 0
        // 1
        // 1 1
        // 1 1 1
        // ...
        // -> force sorting
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < row; ++col) {
                matrix << std::make_pair(Coord<2>(row, col), 1);
            }
        }
        gridOld.setWeights(0, matrix);
        gridNew.setWeights(0, matrix);

        Region<1> region;
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(40),   60);
        region << Streak<1>(Coord<1>(100), 150);
        region = gridOld.remapRegion(region);

        UnstructuredUpdateFunctor<TestCellType > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(false, false);
        APITraits::SelectThreadedUpdate<TestCellType>::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (((coord.x() >=  10) && (coord.x() <  30)) ||
                ((coord.x() >=  40) && (coord.x() <  60)) ||
                ((coord.x() >= 100) && (coord.x() < 150))) {
                const double sum = coord.x() * 311.0;
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testBasicWithSigmaButSansUpdateLineXConcurrencyFalseTrue()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        Coord<1> dim(DIM);
        Region<1> boundingRegion;
        boundingRegion << CoordBox<1>(Coord<1>(0), dim);

        typedef SimpleUnstructuredTestCell<128, EmptyUnstructuredTestCellAPI> TestCellType;
        TestCellType defaultCell(322);
        TestCellType edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredGrid<TestCellType, 1, double, 4, 128> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        // weights matrix looks like this:
        // 0
        // 1
        // 1 1
        // 1 1 1
        // ...
        // -> force sorting
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < row; ++col) {
                matrix << std::make_pair(Coord<2>(row, col), 1);
            }
        }
        gridOld.setWeights(0, matrix);
        gridNew.setWeights(0, matrix);

        Region<1> region;
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(40),   60);
        region << Streak<1>(Coord<1>(100), 150);
        region = gridOld.remapRegion(region);

        UnstructuredUpdateFunctor<TestCellType > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(false, true);
        APITraits::SelectThreadedUpdate<TestCellType>::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (((coord.x() >=  10) && (coord.x() <  30)) ||
                ((coord.x() >=  40) && (coord.x() <  60)) ||
                ((coord.x() >= 100) && (coord.x() < 150))) {
                const double sum = coord.x() * 322.0;
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testBasicWithSigmaButSansUpdateLineXConcurrencyTrueFalse()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        Coord<1> dim(DIM);
        Region<1> boundingRegion;
        boundingRegion << CoordBox<1>(Coord<1>(0), dim);

        typedef SimpleUnstructuredTestCell<128, EmptyUnstructuredTestCellAPI> TestCellType;
        TestCellType defaultCell(333);
        TestCellType edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredGrid<TestCellType, 1, double, 4, 128> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        // weights matrix looks like this:
        // 0
        // 1
        // 1 1
        // 1 1 1
        // ...
        // -> force sorting
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < row; ++col) {
                matrix << std::make_pair(Coord<2>(row, col), 1);
            }
        }
        gridOld.setWeights(0, matrix);
        gridNew.setWeights(0, matrix);

        Region<1> region;
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(40),   60);
        region << Streak<1>(Coord<1>(100), 150);
        region = gridOld.remapRegion(region);

        UnstructuredUpdateFunctor<TestCellType > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(true, false);
        APITraits::SelectThreadedUpdate<TestCellType>::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (((coord.x() >=  10) && (coord.x() <  30)) ||
                ((coord.x() >=  40) && (coord.x() <  60)) ||
                ((coord.x() >= 100) && (coord.x() < 150))) {
                const double sum = coord.x() * 333.0;
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testBasicWithSigmaButSansUpdateLineXConcurrencyTrueTrue()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        Coord<1> dim(DIM);
        Region<1> boundingRegion;
        boundingRegion << CoordBox<1>(Coord<1>(0), dim);

        typedef SimpleUnstructuredTestCell<128, EmptyUnstructuredTestCellAPI> TestCellType;
        TestCellType defaultCell(344);
        TestCellType edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredGrid<TestCellType, 1, double, 4, 128> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        // weights matrix looks like this:
        // 0
        // 1
        // 1 1
        // 1 1 1
        // ...
        // -> force sorting
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < row; ++col) {
                matrix << std::make_pair(Coord<2>(row, col), 1);
            }
        }
        gridOld.setWeights(0, matrix);
        gridNew.setWeights(0, matrix);

        Region<1> region;
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(40),   60);
        region << Streak<1>(Coord<1>(100), 150);
        region = gridOld.remapRegion(region);

        UnstructuredUpdateFunctor<TestCellType > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(true, true);
        APITraits::SelectThreadedUpdate<TestCellType>::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (((coord.x() >=  10) && (coord.x() <  30)) ||
                ((coord.x() >=  40) && (coord.x() <  60)) ||
                ((coord.x() >= 100) && (coord.x() < 150))) {
                const double sum = coord.x() * 344.0;
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testBasicWithSigmaAndWithUpdateLineXConcurrencyFalseFalse()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        Coord<1> dim(DIM);
        Region<1> boundingRegion;
        boundingRegion << CoordBox<1>(Coord<1>(0), dim);

        typedef SimpleUnstructuredTestCell<128> TestCellType;
        TestCellType defaultCell(411);
        TestCellType edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredGrid<TestCellType, 1, double, 4, 128> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        // weights matrix looks like this:
        // 0
        // 1
        // 1 1
        // 1 1 1
        // ...
        // -> force sorting
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < row; ++col) {
                matrix << std::make_pair(Coord<2>(row, col), 1);
            }
        }
        gridOld.setWeights(0, matrix);
        gridNew.setWeights(0, matrix);

        Region<1> region;
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(40),   60);
        region << Streak<1>(Coord<1>(100), 150);
        region = gridOld.remapRegion(region);

        UnstructuredUpdateFunctor<TestCellType > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(false, false);
        APITraits::SelectThreadedUpdate<TestCellType>::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (((coord.x() >=  10) && (coord.x() <  30)) ||
                ((coord.x() >=  40) && (coord.x() <  60)) ||
                ((coord.x() >= 100) && (coord.x() < 150))) {

                const double sum = coord.x() * 411;
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testBasicWithSigmaAndWithUpdateLineXConcurrencyFalseTrue()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        Coord<1> dim(DIM);
        Region<1> boundingRegion;
        boundingRegion << CoordBox<1>(Coord<1>(0), dim);

        typedef SimpleUnstructuredTestCell<128> TestCellType;
        TestCellType defaultCell(422);
        TestCellType edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredGrid<TestCellType, 1, double, 4, 128> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        // weights matrix looks like this:
        // 0
        // 1
        // 1 1
        // 1 1 1
        // ...
        // -> force sorting
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < row; ++col) {
                matrix << std::make_pair(Coord<2>(row, col), 1);
            }
        }
        gridOld.setWeights(0, matrix);
        gridNew.setWeights(0, matrix);

        Region<1> region;
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(40),   60);
        region << Streak<1>(Coord<1>(100), 150);
        region = gridOld.remapRegion(region);

        UnstructuredUpdateFunctor<TestCellType > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(false, true);
        APITraits::SelectThreadedUpdate<TestCellType>::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (((coord.x() >=  10) && (coord.x() <  30)) ||
                ((coord.x() >=  40) && (coord.x() <  60)) ||
                ((coord.x() >= 100) && (coord.x() < 150))) {

                const double sum = coord.x() * 422;
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testBasicWithSigmaAndWithUpdateLineXConcurrencyTrueFalse()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        Coord<1> dim(DIM);
        Region<1> boundingRegion;
        boundingRegion << CoordBox<1>(Coord<1>(0), dim);

        typedef SimpleUnstructuredTestCell<128> TestCellType;
        TestCellType defaultCell(433);
        TestCellType edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredGrid<TestCellType, 1, double, 4, 128> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        // weights matrix looks like this:
        // 0
        // 1
        // 1 1
        // 1 1 1
        // ...
        // -> force sorting
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < row; ++col) {
                matrix << std::make_pair(Coord<2>(row, col), 1);
            }
        }
        gridOld.setWeights(0, matrix);
        gridNew.setWeights(0, matrix);

        Region<1> region;
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(40),   60);
        region << Streak<1>(Coord<1>(100), 150);
        region = gridOld.remapRegion(region);

        UnstructuredUpdateFunctor<TestCellType > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(true, false);
        APITraits::SelectThreadedUpdate<TestCellType>::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (((coord.x() >=  10) && (coord.x() <  30)) ||
                ((coord.x() >=  40) && (coord.x() <  60)) ||
                ((coord.x() >= 100) && (coord.x() < 150))) {

                const double sum = coord.x() * 433;
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testBasicWithSigmaAndWithUpdateLineXConcurrencyTrueTrue()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        Coord<1> dim(DIM);
        Region<1> boundingRegion;
        boundingRegion << CoordBox<1>(Coord<1>(0), dim);

        typedef SimpleUnstructuredTestCell<128> TestCellType;
        TestCellType defaultCell(444);
        TestCellType edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredGrid<TestCellType, 1, double, 4, 128> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        // weights matrix looks like this:
        // 0
        // 1
        // 1 1
        // 1 1 1
        // ...
        // -> force sorting
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < row; ++col) {
                matrix << std::make_pair(Coord<2>(row, col), 1);
            }
        }
        gridOld.setWeights(0, matrix);
        gridNew.setWeights(0, matrix);

        Region<1> region;
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(40),   60);
        region << Streak<1>(Coord<1>(100), 150);
        region = gridOld.remapRegion(region);

        UnstructuredUpdateFunctor<TestCellType > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(true, true);
        APITraits::SelectThreadedUpdate<TestCellType>::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (((coord.x() >=  10) && (coord.x() <  30)) ||
                ((coord.x() >=  40) && (coord.x() <  60)) ||
                ((coord.x() >= 100) && (coord.x() < 150))) {

                const double sum = coord.x() * 444;
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testSoAConcurrencyFalseFalse()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        CoordBox<1> dim(Coord<1>(0), Coord<1>(DIM));
        Region<1> boundingRegion;
        boundingRegion << dim;

        SimpleUnstructuredSoATestCell<1> defaultCell(200);
        SimpleUnstructuredSoATestCell<1> edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredSoAGrid<SimpleUnstructuredSoATestCell<1>, 1, double, 4, 1> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        for (int i = 0; i < DIM; ++i) {
            gridOld.set(Coord<1>(i), SimpleUnstructuredSoATestCell<1>(2111 + i));
        }

        // weights matrix looks like this:
        // 0
        // 1
        // 2 12
        // 3 13 23
        // ...
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < row; ++col) {
                matrix << std::make_pair(Coord<2>(row, col), row + col * 10);
            }
        }
        gridOld.setWeights(0, matrix);
        gridNew.setWeights(0, matrix);

        Region<1> region;
        // loop peeling in first and last chunk
        region << Streak<1>(Coord<1>(10),   30);
        // loop peeling in first chunk
        region << Streak<1>(Coord<1>(37),   60);
        // "normal" streak
        region << Streak<1>(Coord<1>(64),   80);
        // loop peeling in last chunk
        region << Streak<1>(Coord<1>(100), 149);
        region = gridOld.remapRegion(region);

        UnstructuredUpdateFunctor<SimpleUnstructuredSoATestCell<1> > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(false, false);
        APITraits::SelectThreadedUpdate<SimpleUnstructuredSoATestCell<1> >::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (region.count(coord)) {
                double sum = 0;
                for (int i = 0; i < coord.x(); ++i) {
                    double weight = coord.x() + i * 10;
                    sum += weight * (2111 + i);
                }
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testSoAConcurrencyFalseTrue()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        CoordBox<1> dim(Coord<1>(0), Coord<1>(DIM));
        Region<1> boundingRegion;
        boundingRegion << dim;

        SimpleUnstructuredSoATestCell<1> defaultCell(200);
        SimpleUnstructuredSoATestCell<1> edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredSoAGrid<SimpleUnstructuredSoATestCell<1>, 1, double, 4, 1> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        for (int i = 0; i < DIM; ++i) {
            gridOld.set(Coord<1>(i), SimpleUnstructuredSoATestCell<1>(2222 + i));
        }

        // weights matrix looks like this:
        // 0
        // 1
        // 2 12
        // 3 13 23
        // ...
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < row; ++col) {
                matrix << std::make_pair(Coord<2>(row, col), row + col * 10);
            }
        }
        gridOld.setWeights(0, matrix);
        gridNew.setWeights(0, matrix);

        Region<1> region;
        // loop peeling in first and last chunk
        region << Streak<1>(Coord<1>(10),   30);
        // loop peeling in first chunk
        region << Streak<1>(Coord<1>(37),   60);
        // "normal" streak
        region << Streak<1>(Coord<1>(64),   80);
        // loop peeling in last chunk
        region << Streak<1>(Coord<1>(100), 149);
        region = gridOld.remapRegion(region);

        UnstructuredUpdateFunctor<SimpleUnstructuredSoATestCell<1> > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(false, true);
        APITraits::SelectThreadedUpdate<SimpleUnstructuredSoATestCell<1> >::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (region.count(coord)) {
                double sum = 0;
                for (int i = 0; i < coord.x(); ++i) {
                    double weight = coord.x() + i * 10;
                    sum += weight * (2222 + i);
                }
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testSoAConcurrencyTrueFalse()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        CoordBox<1> dim(Coord<1>(0), Coord<1>(DIM));
        Region<1> boundingRegion;
        boundingRegion << dim;

        SimpleUnstructuredSoATestCell<1> defaultCell(200);
        SimpleUnstructuredSoATestCell<1> edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredSoAGrid<SimpleUnstructuredSoATestCell<1>, 1, double, 4, 1> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        for (int i = 0; i < DIM; ++i) {
            gridOld.set(Coord<1>(i), SimpleUnstructuredSoATestCell<1>(2333 + i));
        }

        // weights matrix looks like this:
        // 0
        // 1
        // 2 12
        // 3 13 23
        // ...
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < row; ++col) {
                matrix << std::make_pair(Coord<2>(row, col), row + col * 10);
            }
        }
        gridOld.setWeights(0, matrix);
        gridNew.setWeights(0, matrix);

        Region<1> region;
        // loop peeling in first and last chunk
        region << Streak<1>(Coord<1>(10),   30);
        // loop peeling in first chunk
        region << Streak<1>(Coord<1>(37),   60);
        // "normal" streak
        region << Streak<1>(Coord<1>(64),   80);
        // loop peeling in last chunk
        region << Streak<1>(Coord<1>(100), 149);
        region = gridOld.remapRegion(region);

        UnstructuredUpdateFunctor<SimpleUnstructuredSoATestCell<1> > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(true, false);
        APITraits::SelectThreadedUpdate<SimpleUnstructuredSoATestCell<1> >::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (region.count(coord)) {
                double sum = 0;
                for (int i = 0; i < coord.x(); ++i) {
                    double weight = coord.x() + i * 10;
                    sum += weight * (2333 + i);
                }
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testSoAConcurrencyTrueTrue()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        CoordBox<1> dim(Coord<1>(0), Coord<1>(DIM));
        Region<1> boundingRegion;
        boundingRegion << dim;

        SimpleUnstructuredSoATestCell<1> defaultCell(200);
        SimpleUnstructuredSoATestCell<1> edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredSoAGrid<SimpleUnstructuredSoATestCell<1>, 1, double, 4, 1> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        for (int i = 0; i < DIM; ++i) {
            gridOld.set(Coord<1>(i), SimpleUnstructuredSoATestCell<1>(2444 + i));
        }

        // weights matrix looks like this:
        // 0
        // 1
        // 2 12
        // 3 13 23
        // ...
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < row; ++col) {
                matrix << std::make_pair(Coord<2>(row, col), row + col * 10);
            }
        }
        gridOld.setWeights(0, matrix);
        gridNew.setWeights(0, matrix);

        Region<1> region;
        // loop peeling in first and last chunk
        region << Streak<1>(Coord<1>(10),   30);
        // loop peeling in first chunk
        region << Streak<1>(Coord<1>(37),   60);
        // "normal" streak
        region << Streak<1>(Coord<1>(64),   80);
        // loop peeling in last chunk
        region << Streak<1>(Coord<1>(100), 149);
        region = gridOld.remapRegion(region);

        UnstructuredUpdateFunctor<SimpleUnstructuredSoATestCell<1> > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(true, true);
        APITraits::SelectThreadedUpdate<SimpleUnstructuredSoATestCell<1> >::Value modelThreadingSpec;

        functor(region, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (region.count(coord)) {
                double sum = 0;
                for (int i = 0; i < coord.x(); ++i) {
                    double weight = coord.x() + i * 10;
                    sum += weight * (2444 + i);
                }
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testSoAWithSIGMAConcurrencyFalseFalse()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        CoordBox<1> dim(Coord<1>(0), Coord<1>(DIM));
        Region<1> boundingRegion;
        boundingRegion << dim;

        SimpleUnstructuredSoATestCell<60> defaultCell(200);
        SimpleUnstructuredSoATestCell<60> edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredSoAGrid<SimpleUnstructuredSoATestCell<60>, 1, double, 4, 60> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        for (int i = 0; i < DIM; ++i) {
            gridOld.set(Coord<1>(i), SimpleUnstructuredSoATestCell<60>(3111 + i));
        }

        // weights matrix looks like this:
        // 0
        // 1
        // 2 12
        // 3 13 23
        // ...
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < row; ++col) {
                matrix << std::make_pair(Coord<2>(row, col), row + col * 100);
            }
        }
        gridOld.setWeights(0, matrix);
        gridNew.setWeights(0, matrix);

        Region<1> region;
        // use the same variation of Streaks as in the previous
        // example, even though they will not correspond to the same
        // special cases (with regard to starting/ending on chunk
        // boundaries) as the Region get's remapped anyway.
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(37),   60);
        region << Streak<1>(Coord<1>(64),   80);
        region << Streak<1>(Coord<1>(100), 149);
        Region<1> updateRegion = gridOld.remapRegion(region);

        UnstructuredUpdateFunctor<SimpleUnstructuredSoATestCell<60> > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(false, false);
        APITraits::SelectThreadedUpdate<SimpleUnstructuredSoATestCell<60> >::Value modelThreadingSpec;

        functor(updateRegion, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (region.count(coord)) {
                double sum = 0;
                for (int i = 0; i < coord.x(); ++i) {
                    double weight = coord.x() + i * 100;
                    sum += weight * (3111 + i);
                }
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testSoAWithSIGMAConcurrencyFalseTrue()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        CoordBox<1> dim(Coord<1>(0), Coord<1>(DIM));
        Region<1> boundingRegion;
        boundingRegion << dim;

        SimpleUnstructuredSoATestCell<60> defaultCell(200);
        SimpleUnstructuredSoATestCell<60> edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredSoAGrid<SimpleUnstructuredSoATestCell<60>, 1, double, 4, 60> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        for (int i = 0; i < DIM; ++i) {
            gridOld.set(Coord<1>(i), SimpleUnstructuredSoATestCell<60>(3222 + i));
        }

        // weights matrix looks like this:
        // 0
        // 1
        // 2 12
        // 3 13 23
        // ...
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < row; ++col) {
                matrix << std::make_pair(Coord<2>(row, col), row + col * 100);
            }
        }
        gridOld.setWeights(0, matrix);
        gridNew.setWeights(0, matrix);

        Region<1> region;
        // use the same variation of Streaks as in the previous
        // example, even though they will not correspond to the same
        // special cases (with regard to starting/ending on chunk
        // boundaries) as the Region get's remapped anyway.
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(37),   60);
        region << Streak<1>(Coord<1>(64),   80);
        region << Streak<1>(Coord<1>(100), 149);
        Region<1> updateRegion = gridOld.remapRegion(region);

        UnstructuredUpdateFunctor<SimpleUnstructuredSoATestCell<60> > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(false, true);
        APITraits::SelectThreadedUpdate<SimpleUnstructuredSoATestCell<60> >::Value modelThreadingSpec;

        functor(updateRegion, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (region.count(coord)) {
                double sum = 0;
                for (int i = 0; i < coord.x(); ++i) {
                    double weight = coord.x() + i * 100;
                    sum += weight * (3222 + i);
                }
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testSoAWithSIGMAConcurrencyTrueFalse()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        CoordBox<1> dim(Coord<1>(0), Coord<1>(DIM));
        Region<1> boundingRegion;
        boundingRegion << dim;

        SimpleUnstructuredSoATestCell<60> defaultCell(200);
        SimpleUnstructuredSoATestCell<60> edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredSoAGrid<SimpleUnstructuredSoATestCell<60>, 1, double, 4, 60> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        for (int i = 0; i < DIM; ++i) {
            gridOld.set(Coord<1>(i), SimpleUnstructuredSoATestCell<60>(3333 + i));
        }

        // weights matrix looks like this:
        // 0
        // 1
        // 2 12
        // 3 13 23
        // ...
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < row; ++col) {
                matrix << std::make_pair(Coord<2>(row, col), row + col * 100);
            }
        }
        gridOld.setWeights(0, matrix);
        gridNew.setWeights(0, matrix);

        Region<1> region;
        // use the same variation of Streaks as in the previous
        // example, even though they will not correspond to the same
        // special cases (with regard to starting/ending on chunk
        // boundaries) as the Region get's remapped anyway.
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(37),   60);
        region << Streak<1>(Coord<1>(64),   80);
        region << Streak<1>(Coord<1>(100), 149);
        Region<1> updateRegion = gridOld.remapRegion(region);

        UnstructuredUpdateFunctor<SimpleUnstructuredSoATestCell<60> > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(true, false);
        APITraits::SelectThreadedUpdate<SimpleUnstructuredSoATestCell<60> >::Value modelThreadingSpec;

        functor(updateRegion, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (region.count(coord)) {
                double sum = 0;
                for (int i = 0; i < coord.x(); ++i) {
                    double weight = coord.x() + i * 100;
                    sum += weight * (3333 + i);
                }
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }

    void testSoAWithSIGMAConcurrencyTrueTrue()
    {
#ifdef LIBGEODECOMP_WITH_CPP14
        const int DIM = 150;
        CoordBox<1> dim(Coord<1>(0), Coord<1>(DIM));
        Region<1> boundingRegion;
        boundingRegion << dim;

        SimpleUnstructuredSoATestCell<60> defaultCell(200);
        SimpleUnstructuredSoATestCell<60> edgeCell(-1);

        typedef ReorderingUnstructuredGrid<UnstructuredSoAGrid<SimpleUnstructuredSoATestCell<60>, 1, double, 4, 60> > GridType;
        GridType gridOld(boundingRegion, defaultCell, edgeCell);
        GridType gridNew(boundingRegion, defaultCell, edgeCell);

        for (int i = 0; i < DIM; ++i) {
            gridOld.set(Coord<1>(i), SimpleUnstructuredSoATestCell<60>(3444 + i));
        }

        // weights matrix looks like this:
        // 0
        // 1
        // 2 12
        // 3 13 23
        // ...
        GridType::SparseMatrix matrix;
        for (int row = 0; row < DIM; ++row) {
            for (int col = 0; col < row; ++col) {
                matrix << std::make_pair(Coord<2>(row, col), row + col * 100);
            }
        }
        gridOld.setWeights(0, matrix);
        gridNew.setWeights(0, matrix);

        Region<1> region;
        // use the same variation of Streaks as in the previous
        // example, even though they will not correspond to the same
        // special cases (with regard to starting/ending on chunk
        // boundaries) as the Region get's remapped anyway.
        region << Streak<1>(Coord<1>(10),   30);
        region << Streak<1>(Coord<1>(37),   60);
        region << Streak<1>(Coord<1>(64),   80);
        region << Streak<1>(Coord<1>(100), 149);
        Region<1> updateRegion = gridOld.remapRegion(region);

        UnstructuredUpdateFunctor<SimpleUnstructuredSoATestCell<60> > functor;
        UpdateFunctorHelpers::ConcurrencyEnableOpenMP concurrencySpec(true, true);
        APITraits::SelectThreadedUpdate<SimpleUnstructuredSoATestCell<60> >::Value modelThreadingSpec;

        functor(updateRegion, gridOld, &gridNew, 0, concurrencySpec, modelThreadingSpec);

        for (Coord<1> coord(0); coord < Coord<1>(150); ++coord.x()) {
            if (region.count(coord)) {
                double sum = 0;
                for (int i = 0; i < coord.x(); ++i) {
                    double weight = coord.x() + i * 100;
                    sum += weight * (3444 + i);
                }
                TS_ASSERT_EQUALS(sum, gridNew.get(coord).sum);
            } else {
                TS_ASSERT_EQUALS(0.0, gridNew.get(coord).sum);
            }
        }
#endif
    }
};

}
