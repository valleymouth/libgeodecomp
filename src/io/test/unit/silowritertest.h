#include <libgeodecomp/config.h>
#include <libgeodecomp/io/selector.h>
#include <libgeodecomp/io/silowriter.h>
#include <libgeodecomp/misc/apitraits.h>
#include <libgeodecomp/misc/stdcontaineroverloads.h>
#include <libgeodecomp/misc/tempfile.h>
#include <libgeodecomp/storage/multicontainercell.h>

#ifdef LIBGEODECOMP_WITH_SILO
#include <Python.h>
#endif

#ifdef LIBGEODECOMP_WITH_QT
#include <QtGui/QApplication>
#include <QtGui/QColor>
#include <QtGui/QImage>
#endif

#include <fstream>

using namespace LibGeoDecomp;

class DummyParticle
{
public:
    DummyParticle(const FloatCoord<2> pos = FloatCoord<2>()) :
        pos(pos)
    {
        coords << pos + FloatCoord<2>(-5, -5)
               << pos + FloatCoord<2>( 5, -5)
               << pos + FloatCoord<2>( 5,  5)
               << pos + FloatCoord<2>(-5,  5);
    }

    FloatCoord<2> getPoint() const
    {
        return pos;
    }

    std::vector<FloatCoord<2> > getShape() const
    {
        return coords;
    }

    FloatCoord<2> pos;
    std::vector<FloatCoord<2> > coords;
};

typedef std::vector<DummyParticle> ParticleVec;

class CellWithPointMesh
{
public:
    typedef DummyParticle value_type;
    typedef ParticleVec::iterator iterator;
    typedef ParticleVec::const_iterator const_iterator;

    class API :
        public APITraits::HasCustomRegularGrid,
        public APITraits::HasPointMesh,
        public APITraits::HasUnstructuredGrid
    {
    public:
        static FloatCoord<2> getRegularGridSpacing()
        {
            return FloatCoord<2>(20, 10);
        }

        static FloatCoord<2> getRegularGridOrigin()
        {
            return FloatCoord<2>(0, 0);
        }
    };

    CellWithPointMesh(double dummyValue = 0) :
        dummyValue(dummyValue)
    {}

    ParticleVec::iterator begin()
    {
        return particles.begin();
    }

    ParticleVec::const_iterator begin() const
    {
        return particles.begin();
    }

    ParticleVec::iterator end()
    {
        return particles.end();
    }

    ParticleVec::const_iterator end() const
    {
        return particles.end();
    }

    std::size_t size() const
    {
        return particles.size();
    }

    ParticleVec particles;
    double dummyValue;
};

class SimpleCell
{
public:
    SimpleCell() :
        bar(++counter + 0.5)
    {}

    double bar;
    static int counter;
};

int SimpleCell::counter = 0;

DECLARE_MULTI_CONTAINER_CELL(MultiCellBase,                       \
                             ((DummyParticle)(30)(particles))     \
                             ((SimpleCell)(50)(cells)) )

class MultiCellWithParticles : public MultiCellBase
{
public:
    class API :
        public APITraits::HasCustomRegularGrid,
        public APITraits::HasPointMesh,
        public APITraits::HasUnstructuredGrid
    {
    public:
        static FloatCoord<2> getRegularGridSpacing()
        {
            return FloatCoord<2>(20, 10);
        }

        static FloatCoord<2> getRegularGridOrigin()
        {
            return FloatCoord<2>(0, 0);
        }
    };

};

class ParticleFilterBase : public Selector<DummyParticle>::Filter<FloatCoord<2>, double>
{
public:
    void copyStreakInImpl(const double *first, const double *last, FloatCoord<2> *target)
    {
        // left blank as it's not needed in our tests
    }

    void copyStreakOutImpl(const FloatCoord<2> *first, const FloatCoord<2> *last, double *target)
    {
        // left blank as it's not needed in our tests
    }

    void copyMemberInImpl(
        const double *source, DummyParticle *target, int num, FloatCoord<2> DummyParticle:: *memberPointer)
    {
        // left blank as it's not needed in our tests
    }
};

class ParticleFilterX : public ParticleFilterBase
{
    void copyMemberOutImpl(
        const DummyParticle *source, double *target, int num, FloatCoord<2> DummyParticle:: *memberPointer)
    {
        for (int i = 0; i < num; ++i) {
            target[i] = (source[i].*memberPointer)[0];
        }
    }
};

class ParticleFilterY : public ParticleFilterBase
{
    void copyMemberOutImpl(
        const DummyParticle *source, double *target, int num, FloatCoord<2> DummyParticle:: *memberPointer)
    {
        for (int i = 0; i < num; ++i) {
            target[i] = (source[i].*memberPointer)[1];
        }
    }
};

namespace LibGeoDecomp {

class SiloWriterTest : public CxxTest::TestSuite
{
public:
    void setUp()
    {
#ifdef LIBGEODECOMP_WITH_QT
        int argc = 0;
        char **argv = 0;
        app.reset(new QApplication(argc, argv));
#endif

        prefix = TempFile::serial("silowriter_test") + "foo";
        siloFile1 = prefix + ".00123.silo";
        siloFile2 = prefix + ".00666.silo";

        removeFile(prefix + "A.png");
        removeFile(prefix + "B.png");
        removeFile(prefix + "C.png");
        removeFile(prefix + "D.png");
        removeFile(prefix + "E.png");

        removeFile(prefix + "A0000.png");
        removeFile(prefix + "B0000.png");
        removeFile(prefix + "C0000.png");
        removeFile(prefix + "D0000.png");
        removeFile(prefix + "E0000.png");
    }

    void tearDown()
    {
        removeFile(prefix + "A.png");
        removeFile(prefix + "B.png");
        removeFile(prefix + "C.png");
        removeFile(prefix + "D.png");
        removeFile(prefix + "E.png");

        removeFile(prefix + "A0000.png");
        removeFile(prefix + "B0000.png");
        removeFile(prefix + "C0000.png");
        removeFile(prefix + "D0000.png");
        removeFile(prefix + "E0000.png");

        removeFile(siloFile1);
        removeFile(siloFile2);

#ifdef LIBGEODECOMP_WITH_QT
        app.reset();
#endif
    }

#ifdef LIBGEODECOMP_WITH_SILO
#ifdef LIBGEODECOMP_WITH_VISIT
#ifdef LIBGEODECOMP_WITH_QT

    typedef std::map<QRgb, int> Histogram;

    void render(std::string visitScript)
    {
        // init grid
        Coord<2> dim(10, 5);
        CoordBox<2> box(Coord<2>(), dim);
        FloatCoord<2> quadrantDim;
        FloatCoord<2> origin;
        APITraits::SelectRegularGrid<CellWithPointMesh>::value(&quadrantDim, &origin);

        Grid<CellWithPointMesh> grid(dim);
        int counter = 0;

        for (CoordBox<2>::Iterator i = box.begin(); i != box.end(); ++i) {
            grid[*i] = CellWithPointMesh(counter++);
            FloatCoord<2> center =
                FloatCoord<2>(*i).scale(quadrantDim) +
                quadrantDim * 0.5;
            grid[*i].particles << DummyParticle(center);
        }

        // dump silo file
        SiloWriter<CellWithPointMesh> writer(prefix, 1);
        writer.addSelector(Selector<CellWithPointMesh>(&CellWithPointMesh::dummyValue, "dummyValue"));

        boost::shared_ptr<Selector<DummyParticle>::FilterBase> filterX(new ParticleFilterX());
        boost::shared_ptr<Selector<DummyParticle>::FilterBase> filterY(new ParticleFilterY());

        writer.addSelectorForPointMesh(
            Selector<DummyParticle>(&DummyParticle::pos, "posX", filterX));
        writer.addSelectorForUnstructuredGrid(
            Selector<DummyParticle>(&DummyParticle::pos, "posY", filterY));
        writer.stepFinished(grid, 123, WRITER_INITIALIZED);

        // plot
        Py_Initialize();
        PyRun_SimpleString(visitScript.c_str());
        Py_Finalize();

        remove(siloFile1.c_str());
    }

    Histogram loadImage(const std::string suffix1, const std::string suffix2)
    {
        Histogram ret;

        std::string imageFile1 = prefix + suffix1 + ".png";
        std::string imageFile2 = prefix + suffix1 + suffix2 + ".png";

        QImage image;
        bool loadOK = image.load(QString(imageFile1.c_str()));

        if (!loadOK) {
            TS_ASSERT(image.load(QString(imageFile2.c_str())));
        }

        QSize expectedSize(1024, 1024);
        TS_ASSERT_EQUALS(expectedSize, image.size());

        if (image.size() == expectedSize) {
            for (int y = 0; y < 1024; ++y) {
                for (int x = 0; x < 1024; ++x) {
                    ret[image.pixel(x, y)] += 1;
                }
            }
        }

        remove(imageFile1.c_str());
        remove(imageFile2.c_str());

        return ret;
    }

#endif
#endif
#endif

    void testBasic()
    {
#ifdef LIBGEODECOMP_WITH_SILO
#ifdef LIBGEODECOMP_WITH_VISIT
#ifdef LIBGEODECOMP_WITH_QT

        QColor white(255, 255, 255);
        QColor black(0, 0, 0);
        QColor red(  255, 0, 0);
        QColor green(0, 255, 0);
        QColor blue( 0, 0, 255);

        std::stringstream buf;
        buf << "import re\n"
            << "import os\n"
            << "import visit\n"
            << "\n"
            << "simfile = \"" << siloFile1 << "\"\n"
            << "\n"
            << "visit.LaunchNowin ()\n"
            << "visit.OpenDatabase(simfile)\n"
            << "attributes = visit.SaveWindowAttributes()\n"
            << "attributes.format = attributes.PNG\n"
            << "attributes.width = 1024\n"
            << "attributes.height = 1024\n"
            << "attributes.outputToCurrentDirectory = 1\n";
        // first image
        buf << "attributes.fileName = \"" << prefix << "A\"\n"
            << "visit.SetSaveWindowAttributes(attributes)\n"
            << "visit.AddPlot(\"Mesh\", \"regular_grid\")\n"
            << "visit.DrawPlots()\n"
            << "visit.SaveWindow()\n"
            << "visit.DeleteAllPlots()\n";
        // second image
        buf << "attributes.fileName = \"" << prefix << "B\"\n"
            << "visit.SetSaveWindowAttributes(attributes)\n"
            << "visit.AddPlot(\"Mesh\", \"point_mesh\")\n"
            << "visit.AddPlot(\"Mesh\", \"regular_grid\")\n"
            << "visit.DrawPlots()\n"
            << "visit.SaveWindow()\n"
            << "visit.DeleteAllPlots()\n";
        // third image
        buf << "attributes.fileName = \"" << prefix << "C\"\n"
            << "visit.SetSaveWindowAttributes(attributes)\n"
            << "visit.AddPlot(\"Mesh\", \"regular_grid\")\n"
            << "visit.AddPlot(\"Mesh\", \"point_mesh\")\n"
            << "visit.AddPlot(\"Pseudocolor\", \"dummyValue\")\n"
            << "visit.DrawPlots()\n"
            << "visit.SaveWindow()\n"
            << "visit.DeleteAllPlots()\n";
        // fourth image
        buf << "attributes.fileName = \"" << prefix << "D\"\n"
            << "visit.SetSaveWindowAttributes(attributes)\n"
            << "visit.AddPlot(\"Mesh\", \"regular_grid\")\n"
            << "visit.AddPlot(\"Pseudocolor\", \"posX\")\n"
            << "visit.DrawPlots()\n"
            << "visit.SaveWindow()\n"
            << "visit.DeleteAllPlots()\n";
        // fifth image
        buf << "attributes.fileName = \"" << prefix << "E\"\n"
            << "visit.SetSaveWindowAttributes(attributes)\n"
            << "visit.AddPlot(\"Mesh\", \"regular_grid\")\n"
            << "visit.AddPlot(\"Mesh\", \"point_mesh\")\n"
            << "visit.AddPlot(\"Pseudocolor\", \"posY\")\n"
            << "visit.DrawPlots()\n"
            << "visit.SaveWindow()\n"
            << "visit.DeleteAllPlots()\n";
        render(buf.str());

        Histogram histogram1 = loadImage("A", "0000");

        TS_ASSERT(histogram1[white.rgb()] > 900000);

        Histogram histogram2 = loadImage("B", "0000");

        TS_ASSERT(histogram1[white.rgb()] > histogram2[white.rgb()]);
        // point mesh should add 50 dots a 2x2 pixels plus a label
        TS_ASSERT((histogram1[white.rgb()] - histogram2[white.rgb()]) > 200);

        Histogram histogram3 = loadImage("C", "0000");

        TS_ASSERT(histogram3[white.rgb()] > 800000);
        TS_ASSERT(histogram3[red.rgb()  ] >= 40);


        Histogram histogram4 = loadImage("D", "0000");

        TS_ASSERT(histogram1[white.rgb()] > histogram4[white.rgb()]);
        TS_ASSERT(histogram4[red.rgb()  ] > 20);

        Histogram histogram5 = loadImage("E", "0000");

        TS_ASSERT(histogram4[red.rgb()  ] > 20);
        TS_ASSERT(histogram4[white.rgb()] < histogram1[white.rgb()]);

#endif
#endif
#endif
    }

    void testMemberExtraction()
    {
#ifdef LIBGEODECOMP_WITH_SILO
#ifdef LIBGEODECOMP_WITH_VISIT
#ifdef LIBGEODECOMP_WITH_QT

        // init grid
        Coord<2> dim(10, 5);
        CoordBox<2> box(Coord<2>(), dim);
        FloatCoord<2> quadrantDim;
        FloatCoord<2> origin;
        APITraits::SelectRegularGrid<CellWithPointMesh>::value(&quadrantDim, &origin);

        Grid<CellWithPointMesh> gridA(dim);
        Grid<MultiCellWithParticles> gridB(dim);
        int counter = 0;

        for (CoordBox<2>::Iterator i = box.begin(); i != box.end(); ++i) {
            gridA[*i] = CellWithPointMesh(counter++);
            FloatCoord<2> center =
                FloatCoord<2>(*i).scale(quadrantDim) +
                quadrantDim * 0.5;
            gridA[*i].particles << DummyParticle(center);

            // copy over freshly created particles, so we can be sure
            // each coordinate yields the same particles in both
            // grids:
            ParticleVec particles = gridA[*i].particles;
            for (size_t c = 0; c < particles.size(); ++c) {
                gridB[*i].particles.insert(c, particles[c]);
            }
        }

        // dump silo file
        SiloWriter<CellWithPointMesh> writerA(prefix, 1);
        SiloWriter<MultiCellWithParticles,
                   CollectionInterface::Delegate<MultiCellWithParticles, ContainerCell<DummyParticle, 30, int> > > writerB(&MultiCellWithParticles::particles, prefix, 1);

        boost::shared_ptr<Selector<DummyParticle>::FilterBase> filterX(new ParticleFilterX());
        boost::shared_ptr<Selector<DummyParticle>::FilterBase> filterY(new ParticleFilterY());

        writerA.addSelectorForPointMesh(
            Selector<DummyParticle>(&DummyParticle::pos, "posX", filterX));
        writerA.addSelectorForUnstructuredGrid(
            Selector<DummyParticle>(&DummyParticle::pos, "posY", filterY));

        writerB.addSelectorForPointMesh(
            Selector<DummyParticle>(&DummyParticle::pos, "posX", filterX));
        writerB.addSelectorForUnstructuredGrid(
            Selector<DummyParticle>(&DummyParticle::pos, "posY", filterY));

        writerA.stepFinished(gridA, 123, WRITER_INITIALIZED);

        std::ifstream infile(siloFile1.c_str());
        if (!infile) {
            throw std::logic_error("could not open silo archive");
        }

        std::size_t bufferSize = 1024 * 1024;
        std::vector<char> bufferA(bufferSize);
        std::vector<char> bufferB(bufferSize);
        infile.read(&bufferA[0], bufferSize);
        TS_ASSERT(!infile.good());
        std::size_t sizeA = infile.gcount();

        infile.close();
        writerB.stepFinished(gridB, 666, WRITER_INITIALIZED);
        infile.open(siloFile2.c_str());
        infile.read(&bufferB[0], bufferSize);
        TS_ASSERT(!infile.good());
        std::size_t sizeB = infile.gcount();

        TS_ASSERT_EQUALS(sizeA, sizeB);
        for (std::size_t i = 0; i < sizeA; ++i) {
            int delta = 0;
            if (i == 187) {
                // Byte 187 in the header is part of a version counter
                delta = 1;
            }

            TS_ASSERT_EQUALS(bufferA[i] + delta, bufferB[i]);
        }
#endif
#endif
#endif
    }

private:
#ifdef LIBGEODECOMP_WITH_QT
    boost::shared_ptr<QApplication> app;
#endif
    std::string prefix;
    std::string siloFile1;
    std::string siloFile2;

    void removeFile(std::string name)
    {
        remove(name.c_str());
    }
};

}
