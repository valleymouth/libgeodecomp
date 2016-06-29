#ifndef LIBGEODECOMP_PARALLELIZATION_HPXDATAFLOWSIMULATOR_H
#define LIBGEODECOMP_PARALLELIZATION_HPXDATAFLOWSIMULATOR_H

#include <libgeodecomp/config.h>
#ifdef LIBGEODECOMP_WITH_HPX

#include <libgeodecomp/geometry/partitions/unstructuredstripingpartition.h>
#include <libgeodecomp/geometry/partitionmanager.h>
#include <libgeodecomp/communication/hpxreceiver.h>
#include <libgeodecomp/parallelization/hierarchicalsimulator.h>
#include <stdexcept>

namespace LibGeoDecomp {

namespace HPXDataFlowSimulatorHelpers {

template<typename MESSAGE>
class Neighborhood
{
public:
    // fixme: move semantics
    inline Neighborhood(
        int targetGlobalNanoStep,
        std::vector<int> messageNeighborIDs,
        std::vector<hpx::shared_future<MESSAGE> > messagesFromNeighbors,
        const std::map<int, hpx::id_type>& remoteIDs) :
        targetGlobalNanoStep(targetGlobalNanoStep),
        messageNeighborIDs(messageNeighborIDs),
        messagesFromNeighbors(hpx::util::unwrapped(messagesFromNeighbors)),
        remoteIDs(remoteIDs)
    {}

    const MESSAGE& operator[](int index) const
    {
        std::vector<int>::const_iterator i = std::find(messageNeighborIDs.begin(), messageNeighborIDs.end(), index);
        if (i == messageNeighborIDs.end()) {
            throw std::logic_error("ID not found for incoming messages");
        }

        return messagesFromNeighbors[i - messageNeighborIDs.begin()];
    }

    // fixme: move semantics
    void send(int remoteCellID, const MESSAGE& message) const
    {
        std::map<int, hpx::id_type>::const_iterator iter = remoteIDs.find(remoteCellID);
        if (iter == remoteIDs.end()) {
            throw std::logic_error("ID not found for outgoing messages");
        }

        hpx::apply(
            typename HPXReceiver<MESSAGE>::receiveAction(),
            iter->second,
            targetGlobalNanoStep,
            message);
    }

private:
    int targetGlobalNanoStep;
    std::vector<int> messageNeighborIDs;
    std::vector<MESSAGE> messagesFromNeighbors;
    std::map<int, hpx::id_type> remoteIDs;
};

// fixme: componentize
template<typename CELL, typename MESSAGE>
class CellComponent
{
public:
    static const unsigned NANO_STEPS = APITraits::SelectNanoSteps<CELL>::VALUE;
    typedef typename APITraits::SelectMessageType<CELL>::Value MessageType;

    // fixme: move semantics
    // fixme: own cell, not just pointer, to facilitate migration of components
    explicit CellComponent(
        const std::string& basename = "",
        CELL *cell = 0,
        int id = -1,
        const std::vector<int> neighbors = std::vector<int>()) :
        basename(basename),
        cell(cell),
        id(id),
    // fixme: move semantics
        neighbors(neighbors)
    {
        for (auto&& neighbor: neighbors) {
            std::string linkName = endpointName(basename, neighbor, id);
            receivers[neighbor] = HPXReceiver<MESSAGE>::make(linkName).get();
        }
    }

    void setupDataflow()
    {
        std::vector<hpx::future<void> > remoteIDFutures;
        remoteIDFutures.reserve(neighbors.size());

        for (auto i = neighbors.begin(); i != neighbors.end(); ++i) {
            std::string linkName = HPXDataFlowSimulatorHelpers::CellComponent<MessageType, MessageType>::endpointName(
                basename, id, *i);

            int neighbor = *i;
            remoteIDFutures << HPXReceiver<MessageType>::find(linkName).then(
                [neighbor, this](hpx::shared_future<hpx::id_type> remoteIDFuture)
                {
                    remoteIDs[neighbor] = remoteIDFuture.get();
                });
        }

        // res.get();

        hpx::when_all(remoteIDFutures).get();
    }

    // fixme: use move semantics here
    void update(
        std::vector<int> neighbors,
        std::vector<hpx::shared_future<MESSAGE> > inputFutures,
        const hpx::shared_future<void>& /* unused, just here to ensure
                                           correct ordering of updates
                                           per cell */,
        int nanoStep,
        int step)
    {
        int targetGlobalNanoStep = step * NANO_STEPS + nanoStep + 1;
        Neighborhood<MESSAGE> hood(targetGlobalNanoStep, neighbors, inputFutures, remoteIDs);
        cell->update(hood, nanoStep, step);
    }

    static std::string endpointName(const std::string& basename, int sender, int receiver)
    {
        return "HPXDataflowSimulatorEndPoint_" +
            basename +
            "_" +
            StringOps::itoa(sender) +
            "_to_" +
            StringOps::itoa(receiver);

    }

    // fixme: make private
// private:
    std::string basename;
    std::vector<int> neighbors;
    CELL *cell;
    int id;
    std::map<int, std::shared_ptr<HPXReceiver<MESSAGE> > > receivers;
    std::map<int, hpx::id_type> remoteIDs;
};

}

/**
 * Experimental Simulator based on (surprise surprise) HPX' dataflow
 * operator. Primary use case (for now) is DGSWEM.
 */
template<typename CELL, typename PARTITION = UnstructuredStripingPartition>
class HPXDataflowSimulator : public HierarchicalSimulator<CELL>
{
public:
    typedef typename APITraits::SelectMessageType<CELL>::Value MessageType;
    typedef HierarchicalSimulator<CELL> ParentType;
    typedef typename DistributedSimulator<CELL>::Topology Topology;
    typedef PartitionManager<Topology> PartitionManagerType;
    using DistributedSimulator<CELL>::NANO_STEPS;
    using DistributedSimulator<CELL>::initializer;
    using HierarchicalSimulator<CELL>::initialWeights;

    /**
     * basename will be added to IDs for use in AGAS lookup, so for
     * each simulation all localities need to use the same basename,
     * but if you intent to run multiple different simulations in a
     * single program, either in parallel or sequentially, you'll need
     * to use a different basename.
     */
    inline HPXDataflowSimulator(
        Initializer<CELL> *initializer,
        const std::string& basename,
        int loadBalancingPeriod = 10000,
        bool enableFineGrainedParallelism = true) :
        ParentType(
            initializer,
            loadBalancingPeriod * NANO_STEPS,
            enableFineGrainedParallelism),
        basename(basename)
    {}

    void step()
    {
        throw std::logic_error("HPXDataflowSimulator::step() not implemented");
    }

    long currentNanoStep() const
    {
        throw std::logic_error("HPXDataflowSimulator::currentNanoStep() not implemented");
        return 0;
    }

    void balanceLoad()
    {
        throw std::logic_error("HPXDataflowSimulator::balanceLoad() not implemented");
    }
    void run()
    {
        // fixme: reduce this
        UnstructuredGrid<CELL> grid(initializer->gridBox());
        // fixme: init in parallel by calling grid for each cell (forward pointer to cellcomponent?)
        initializer->grid(&grid);

        using hpx::dataflow;
        using hpx::util::unwrapped;

        Region<1> localRegion;
        CoordBox<1> box = initializer->gridBox();
        std::size_t rank = hpx::get_locality_id();
        std::size_t numLocalities = hpx::get_num_localities().get();

        std::vector<double> rankSpeeds(numLocalities, 1.0);
        std::vector<std::size_t> weights = initialWeights(
            box.dimensions.prod(),
            rankSpeeds);

        Region<1> globalRegion;
        globalRegion << box;

        boost::shared_ptr<PARTITION> partition(
            new PARTITION(
                box.origin,
                box.dimensions,
                0,
                weights,
                initializer->getAdjacency(globalRegion)));

        PartitionManager<Topology> partitionManager;
        partitionManager.resetRegions(
            initializer,
            box,
            partition,
            rank,
            1);

        localRegion = partitionManager.ownRegion();
        boost::shared_ptr<Adjacency> adjacency = initializer->getAdjacency(localRegion);


        // fixme: instantiate components in agas and only hold ids of those
        std::map<int, HPXDataFlowSimulatorHelpers::CellComponent<CELL, MessageType> > components;
        std::vector<int> neighbors;

        for (Region<1>::Iterator i = localRegion.begin(); i != localRegion.end(); ++i) {
            neighbors.clear();
            adjacency->getNeighbors(i->x(), &neighbors);
            HPXDataFlowSimulatorHelpers::CellComponent<CELL, MessageType> component(
                basename,
                &grid[*i],
                i->x(),
                neighbors);
            components[i->x()] = component;
        }

        for (Region<1>::Iterator i = localRegion.begin(); i != localRegion.end(); ++i) {
            components[i->x()].setupDataflow();
        }

        typedef hpx::shared_future<void> UpdateResultFuture;
        typedef std::vector<UpdateResultFuture> TimeStepFutures;
        TimeStepFutures lastTimeStepFutures;
        TimeStepFutures thisTimeStepFutures;

        // fixme: also create dataflow in cellcomponent
        for (Region<1>::Iterator i = localRegion.begin(); i != localRegion.end(); ++i) {
            lastTimeStepFutures << hpx::make_ready_future(UpdateResultFuture());
        }
        thisTimeStepFutures.resize(localRegion.size());

        // fixme: add steerer/writer interaction
        int maxTimeSteps = initializer->maxSteps();
        for (int step = 0; step < maxTimeSteps; ++step) {
            for (int nanoStep = 0; nanoStep < NANO_STEPS; ++nanoStep) {
                int index = 0;
                int globalNanoStep = step * NANO_STEPS + nanoStep;

                for (Region<1>::Iterator i = localRegion.begin(); i != localRegion.end(); ++i) {

                    std::vector<hpx::shared_future<MessageType> > receiveMessagesFutures;
                    neighbors.clear();
                    adjacency->getNeighbors(i->x(), &neighbors);

                    for (auto j = neighbors.begin(); j != neighbors.end(); ++j) {
                        if ((globalNanoStep) > 0) {
                            receiveMessagesFutures << components[i->x()].receivers[*j]->get(globalNanoStep);
                        } else {
                            receiveMessagesFutures <<  hpx::make_ready_future(MessageType());
                        }
                    }

                    auto Operation = boost::bind(&HPXDataFlowSimulatorHelpers::CellComponent<CELL, MessageType>::update,
                                                 components[i->x()], _1, _2, _3, _4, _5);

                    thisTimeStepFutures[index] = dataflow(
                        hpx::launch::async,
                        Operation,
                        neighbors,
                        receiveMessagesFutures,
                        lastTimeStepFutures[index],
                        nanoStep,
                        step);

                    ++index;
                }

                using std::swap;
                swap(thisTimeStepFutures, lastTimeStepFutures);
            }
        }

        hpx::when_all(lastTimeStepFutures).get();
    }

    std::vector<Chronometer> gatherStatistics()
    {
        // fixme
        return std::vector<Chronometer>();
    }

private:
    std::string basename;

};

}

#endif

#endif
