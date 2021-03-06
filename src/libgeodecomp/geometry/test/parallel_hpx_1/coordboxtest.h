#include <cxxtest/TestSuite.h>
#include <libgeodecomp/communication/hpxserializationwrapper.h>
#include <libgeodecomp/geometry/coordbox.h>
#include <libgeodecomp/misc/sharedptr.h>

using namespace LibGeoDecomp;

namespace LibGeoDecomp {

class CoordBoxTest : public CxxTest::TestSuite
{
public:
    void testSerializationOfWriterByReference()
    {
        CoordBox<1> ca1(Coord<1>(1),       Coord<1>(2));
        CoordBox<2> ca2(Coord<2>(3, 4),    Coord<2>(5, 6));
        CoordBox<3> ca3(Coord<3>(7, 8, 9), Coord<3>(10, 11, 12));

        std::vector<char> buffer;
        hpx::serialization::output_archive outputArchive(buffer);

        outputArchive << ca1;
        outputArchive << ca2;
        outputArchive << ca3;

        CoordBox<1> cb1(Coord<1>(-1),         Coord<1>(-1));
        CoordBox<2> cb2(Coord<2>(-1, -1),     Coord<2>(-1, -1));
        CoordBox<3> cb3(Coord<3>(-1, -1, -1), Coord<3>(-1, -1, -1));

        hpx::serialization::input_archive inputArchive(buffer);
        inputArchive >> cb1;
        inputArchive >> cb2;
        inputArchive >> cb3;

        TS_ASSERT_EQUALS(ca1, cb1);
        TS_ASSERT_EQUALS(ca2, cb2);
        TS_ASSERT_EQUALS(ca3, cb3);
    }

    void testSerializationViaSharedPointer()
    {
        SharedPtr<CoordBox<1> >::Type ca1(new CoordBox<1>(Coord<1>(1),       Coord<1>(2)));
        SharedPtr<CoordBox<2> >::Type ca2(new CoordBox<2>(Coord<2>(3, 4),    Coord<2>(5, 6)));
        SharedPtr<CoordBox<3> >::Type ca3(new CoordBox<3>(Coord<3>(7, 8, 9), Coord<3>(10, 11, 12)));

        std::vector<char> buffer;
        hpx::serialization::output_archive outputArchive(buffer);
        outputArchive << ca1;
        outputArchive << ca2;
        outputArchive << ca3;

        SharedPtr<CoordBox<1> >::Type cb1;
        SharedPtr<CoordBox<2> >::Type cb2;
        SharedPtr<CoordBox<3> >::Type cb3;

        hpx::serialization::input_archive inputArchive(buffer);
        inputArchive >> cb1;
        inputArchive >> cb2;
        inputArchive >> cb3;

        TS_ASSERT_EQUALS(*ca1, *cb1);
        TS_ASSERT_EQUALS(*ca2, *cb2);
        TS_ASSERT_EQUALS(*ca3, *cb3);
    }
};

}
