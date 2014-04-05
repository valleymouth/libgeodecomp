#include <mpi.h>
#include <libgeodecomp/geometry/partitions/scotchpartition.h>
#include <libgeodecomp/geometry/partitions/ptscotchpartition.h>
#include <libgeodecomp/geometry/partitions/checkerboardingpartition.h>
#include <libgeodecomp/geometry/partitions/zcurvepartition.h>
#include <libgeodecomp/geometry/partitions/stripingpartition.h>
#include <libgeodecomp/geometry/partitions/recursivebisectionpartition.h>
#include <libgeodecomp/geometry/partitions/hilbertpartition.h>
#include <iostream>
#include <fstream>
#include <limits.h>
#include <string>
#include <random>
#include <chrono>

using namespace LibGeoDecomp;

void print(Region<3> reg){
    std::cout << reg.size() <<
        " " << reg.expand(1).size() - reg.size() << " Ratio: "
              << ((float)reg.expand(1).size() - reg.size()) / reg.size() <<std::endl;
}

void print(Region<2> reg){
    std::cout << reg.size() <<
        " " << reg.expand(1).size() - reg.size() << " Ratio: "
              << ((float)reg.expand(1).size() - reg.size()) / reg.size() <<std::endl;
}

void printW(std::vector<std::size_t> vec){
    int sum = 0;
    for(auto it = vec.begin();it != vec.end(); it++){
        std::cout << *it << "  ";
        sum += *it;
    }
    std::cout << " sum: " << sum;
    std::cout << std::endl;
}

template<int DIM>
void outputGzsShift(Partition<DIM> * part, std::ofstream &output,unsigned int number){
    Region<DIM> reg;
    reg = part->getRegion(0);
    double num  = reg.expand(1).size() - reg.size();
    //reg = part->getRegion(1);
    //num += reg.expand(1).size() - reg.size();
    output << number << "\t" << num << "\n";
}

template<int DIM>
void outputGzs(Partition<DIM> * part, std::ofstream &output,unsigned int number){
    Region<DIM> reg;

    unsigned int min,max,avg,tmp,minV,maxV,avgV,tmpV;
    min  = std::numeric_limits<int>::max();
    minV = std::numeric_limits<int>::max();
    max = 0;
    maxV = 0;
    avg = 0;
    avgV = 0;
    for(unsigned int j = 0;j < number; ++j){
        reg = part->getRegion(j);
        tmp = reg.expand(1).size() - reg.size();
        tmpV = reg.size();
        if(tmp < min){
            min = tmp;
        }
        if(tmp > max){
            max = tmp;
        }
        avg += tmp;

        if(tmpV < minV){
            minV = tmpV;
        }
        if(tmpV > maxV){
            maxV = tmpV;
        }
        avgV += tmpV;
        std::cout << reg.size() << " ";
    }

    std::cout << std::endl;
    avg /= number;
    avgV /= number;
    output << number << "\t" << min << "\t" << avg << "\t" << max << "\t"
           << minV << "\t" << avgV << "\t" << maxV << "\n";
}

template<int DIM>
void outputGzsVol(Partition<DIM> * part, std::ofstream &output,unsigned int number){
    Region<DIM> reg;

    double gzs = 0,vol = 0;
    for(unsigned int j = 0;j < number; ++j){
        reg = part->getRegion(j);
        gzs += (reg.expand(1).size() - reg.size());
        vol += reg.size();
    }
    output << number << "\t" << (gzs/vol) << "\n";
}

template<int DIM>
void varyWeigth(int dimx, int dimy,int dimz, int maxnodes){

    std::string scotch = "Scotch";
    std::string zCurve = "ZCurve";
    std::string recBi = "RecBi";
    std::string checker = "Checker";
    std::string ptscotch = "PTScotch";
    std::string striping = "Striping";
    std::string hilbert = "Hilbert";
    std::string x = "x";
    std::string dimString = std::to_string(dimx) + x + std::to_string(dimy);
    Coord<DIM> origin;
    Coord<DIM> dimensions;
    if(DIM == 2){
        origin[0] = 0;
        origin[1] = 0;
        dimensions[0] = dimx;
        dimensions[1] = dimy;
    } else {
        origin[0] = 0;
        origin[1] = 0;
        origin[2] = 0;
        dimensions[0] = dimx;
        dimensions[1] = dimy;
        dimensions[2] = dimz;
        dimString += x + std::to_string(dimz);
    }
    int vol = dimx * dimy * dimz;

    std::vector<std::size_t> weights;
    std::ofstream outputScotch,outputZCurve,outputRecBi,outputCheck,
        outputPTScotch, outputStriping, outputHilbert, gnuplot;

    outputScotch.open((dimString + scotch).c_str());
    outputZCurve.open((dimString + zCurve).c_str());
    outputRecBi.open((dimString + recBi).c_str());
    outputCheck.open((dimString + checker).c_str());
    outputPTScotch.open((dimString + ptscotch).c_str());
    outputStriping.open((dimString + striping).c_str());
    outputHilbert.open((dimString + hilbert).c_str());

    gnuplot.open(("plot" + dimString).c_str());
    gnuplot << "set title \"" + dimString << "\"\n"
            << "set xlabel \"weight of Node 0\" \n "
            << "set ylabel \"Ghostzone of Node 0\" \n"
            << "plot \"" << dimString << scotch << "\" using 1:2 title \"Scotch\" with lines, "
            << "\"" << dimString << ptscotch << "\" using 1:2 title \"PTScotch\" with lines,"
            << "\"" << dimString << recBi << "\" using 1:2 title \"Recursive Bisection\" with lines,"

            << "\"" << dimString << striping << "\" using 1:2 title \"Striping\" with lines,"
            << "\"" << dimString << zCurve << "\" using 1:2 title \"ZCurve\" with lines ";

    for(int i = 1; i < vol/2; ++i){
        std::cout << "Round: " << i << std::endl;
        weights.push_back(i);
        weights.push_back(vol-i);

        std::cout << "scotch" << std::endl;
        Partition<DIM> *scotch = new ScotchPartition<DIM>(origin, dimensions, 0, weights);
        outputGzsShift(scotch,outputScotch,i);
        delete scotch;
        std::cout << "ZCurve" << std::endl;
        Partition<DIM> *zCurve = new ZCurvePartition<DIM>(origin, dimensions, 0, weights);
        outputGzsShift(zCurve,outputZCurve,i);
        delete zCurve;
        std::cout << "recBi" << std::endl;
        Partition<DIM> *recBi = new RecursiveBisectionPartition<DIM>(origin, dimensions, 0, weights);
        outputGzsShift(recBi,outputRecBi,i);
        delete recBi;
        std::cout << "ptscotch" << std::endl;
        Partition<DIM> *ptscotch = new PTScotchPartition<DIM>(origin, dimensions, 0, weights);
        outputGzsShift(ptscotch,outputPTScotch,i);
        delete ptscotch;
        std::cout << "striping" << std::endl;
        Partition<DIM> *striping = new StripingPartition<DIM>(origin, dimensions, 0, weights);
        outputGzsShift(striping,outputStriping,i);
        delete striping;

        weights.clear();
    }
}

template<int DIM>
void phiPlot(int dimx, int dimy,int dimz, int maxnodes){

    std::string scotch = "Scotch";
    std::string zCurve = "ZCurve";
    std::string recBi = "RecBi";
    std::string checker = "Checker";
    std::string ptscotch = "PTScotch";
    std::string striping = "Striping";
    std::string x = "x";
    std::string dimString = std::to_string(dimx) + x + std::to_string(dimy);
    Coord<DIM> origin;
    Coord<DIM> dimensions;
    if(DIM == 2){
        origin[0] = 0;
        origin[1] = 0;
        dimensions[0] = dimx;
        dimensions[1] = dimy;
    } else {
        origin[0] = 0;
        origin[1] = 0;
        origin[2] = 0;
        dimensions[0] = dimx;
        dimensions[1] = dimy;
        dimensions[2] = dimz;
        dimString += x + std::to_string(dimz);
    }

    std::vector<std::size_t> weights;
    std::ofstream outputScotch,outputZCurve,outputRecBi,outputCheck,
        outputPTScotch, outputStriping, outputHilbert, gnuplot;

    outputScotch.open((dimString + scotch).c_str());
    outputZCurve.open((dimString + zCurve).c_str());
    outputRecBi.open((dimString + recBi).c_str());
    outputCheck.open((dimString + checker).c_str());
    outputPTScotch.open((dimString + ptscotch).c_str());
    outputStriping.open((dimString + striping).c_str());

    gnuplot.open(("plot" + dimString).c_str());
    gnuplot << "set title \"" + dimString << "\"\n"
            << "set xlabel \"# of Nodes\" \n "
            << "set ylabel \"Size of biggest Ghostzone\" \n"
            << "plot \"" << dimString << scotch << "\" using 1:4 title \"Scotch\" with lines, "
            << "\"" << dimString << ptscotch << "\" using 1:4 title \"PTScotch\" with lines,"
            << "\"" << dimString << recBi << "\" using 1:4 title \"Recursive Bisection\" with lines,"
            << "\"" << dimString << striping << "\" using 1:4 title \"Striping\" with lines,"
            << "\"" << dimString << zCurve << "\" using 1:4 title \"ZCurve\" with lines ";

    for(int i = 2; i <= maxnodes; i+=2){
        std::cout << "Round: " << i << std::endl;
        int weight = dimx*dimy*dimz/i;
        int remain = dimx*dimy*dimz%i;
        weight += remain/i;
        remain -= remain/i;
        std::cout << weight << std::endl;
        int sum = 0;
        for(int j = 1; j <= i; j+=2){
            weights.push_back(weight*(1.0/10.0));
            weights.push_back(weight*(19.0/10.0));
            if(remain >= 2){
                weights[j-1] += 1;
                weights[j] += 1;
            }
            remain -= 2;
            sum += weights[j-1];
            sum += weights[j];
        }
        weights[i-1] += dimx*dimy*dimz-sum;
        printW(weights);

        std::cout << "scotch" << std::endl;
        Partition<DIM> *scotch = new ScotchPartition<DIM>(origin, dimensions, 0, weights);
        outputGzs(scotch,outputScotch,i);
        delete scotch;
        std::cout << "ZCurve" << std::endl;
        Partition<DIM> *zCurve = new ZCurvePartition<DIM>(origin, dimensions, 0, weights);
        outputGzs(zCurve,outputZCurve,i);
        delete zCurve;
        std::cout << "recBi" << std::endl;
        Partition<DIM> *recBi = new RecursiveBisectionPartition<DIM>(origin, dimensions, 0, weights);
        outputGzs(recBi,outputRecBi,i);
        delete recBi;
        std::cout << "ptscotch" << std::endl;
        Partition<DIM> *ptscotch = new PTScotchPartition<DIM>(origin, dimensions, 0, weights);
        outputGzs(ptscotch,outputPTScotch,i);
        delete ptscotch;
        std::cout << "striping" << std::endl;
        Partition<DIM> *striping = new StripingPartition<DIM>(origin, dimensions, 0, weights);
        outputGzs(striping,outputStriping,i);
        delete striping;

        weights.clear();
    }
}

template<int DIM>
void sizeOverNodes(int dimx, int dimy,int dimz, int maxnodes){

    std::string scotch = "Scotch";
    std::string zCurve = "ZCurve";
    std::string recBi = "RecBi";
    std::string checker = "Checker";
    std::string ptscotch = "PTScotch";
    std::string striping = "Striping";
    std::string hilbert = "Hilbert";
    std::string x = "x";
    std::string dimString = std::to_string(dimx) + x + std::to_string(dimy);
    Coord<DIM> origin;
    Coord<DIM> dimensions;
    if(DIM == 2){
        origin[0] = 0;
        origin[1] = 0;
        dimensions[0] = dimx;
        dimensions[1] = dimy;
    } else {
        origin[0] = 0;
        origin[1] = 0;
        origin[2] = 0;
        dimensions[0] = dimx;
        dimensions[1] = dimy;
        dimensions[2] = dimz;
        dimString += x + std::to_string(dimz);
    }

    std::vector<std::size_t> weights;
    std::ofstream outputScotch,outputZCurve,outputRecBi,outputCheck,
        outputPTScotch, outputStriping, outputHilbert, gnuplot;

    outputScotch.open((dimString + scotch).c_str());
    outputZCurve.open((dimString + zCurve).c_str());
    outputRecBi.open((dimString + recBi).c_str());
    outputCheck.open((dimString + checker).c_str());
    outputPTScotch.open((dimString + ptscotch).c_str());
    outputStriping.open((dimString + striping).c_str());
    outputHilbert.open((dimString + hilbert).c_str());

    gnuplot.open(("plot" + dimString).c_str());
    gnuplot << "set title \"" + dimString << "\"\n"
            << "set xlabel \"# of Nodes\" \n "
            << "set ylabel \"Size of biggest Ghostzone\" \n"
            << "plot \"" << dimString << scotch << "\" using 1:4 title \"Scotch\" with lines, "
            << "\"" << dimString << ptscotch << "\" using 1:4 title \"PTScotch\" with lines,"
            << "\"" << dimString << recBi << "\" using 1:4 title \"Recursive Bisection\" with lines,"
            << "\"" << dimString << checker << "\" using 1:4 title \"Checkerboarding\" with lines,"
            << "\"" << dimString << striping << "\" using 1:4 title \"Striping\" with lines,"
            << "\"" << dimString << zCurve << "\" using 1:4 title \"ZCurve\" with lines ";

    for(int i = 2; i <= maxnodes; ++i){
        std::cout << "Round: " << i << std::endl;
        int remain = (dimx*dimy*dimz)%i;
        int weight = (dimx*dimy*dimz)/i;
        weight += remain/i;
        remain -= remain/i;

        for(int j = 1; j <= i; ++j){
            weights.push_back(weight);
            if(remain != 0){
                weights[j-1] += 1;
                remain--;
            }
        }
        //weights[i-1] += remain;

        printW(weights);

        std::cout << "scotch" << std::endl;
        Partition<DIM> *scotch = new ScotchPartition<DIM>(origin, dimensions, 0, weights);
        outputGzs(scotch,outputScotch,i);
        delete scotch;
        std::cout << "ZCurve" << std::endl;
        Partition<DIM> *zCurve = new ZCurvePartition<DIM>(origin, dimensions, 0, weights);
        outputGzs(zCurve,outputZCurve,i);
        delete zCurve;
        std::cout << "recBi" << std::endl;
        Partition<DIM> *recBi = new RecursiveBisectionPartition<DIM>(origin, dimensions, 0, weights);
        outputGzs(recBi,outputRecBi,i);
        delete recBi;
        std::cout << "checker" << std::endl;
        Partition<DIM> *checker = new CheckerboardingPartition<DIM>(origin, dimensions, 0, weights);
        outputGzs(checker,outputCheck,i);
        delete checker;
        std::cout << "ptscotch" << std::endl;
        Partition<DIM> *ptscotch = new PTScotchPartition<DIM>(origin, dimensions, 0, weights);
        outputGzs(ptscotch,outputPTScotch,i);
        delete ptscotch;
        std::cout << "striping" << std::endl;
        Partition<DIM> *striping = new StripingPartition<DIM>(origin, dimensions, 0, weights);
        outputGzs(striping,outputStriping,i);
        delete striping;

        weights.clear();
    }
}


int main(int argc, char **argv)
{

    MPI_Init(&argc, &argv);
    int dimx, dimy, dimz,dim,nodes;
    if(argc == 2){
        dimx = atoi(argv[1]);
        dimy = 1;
        dimz = 1;
    } else if(argc == 3) {
        dimx = atoi(argv[1]);
        dimy = atoi(argv[2]);
        dimz = 1;
    } else {
        dimx = atoi(argv[1]);
        dimy = atoi(argv[2]);
        dimz = atoi(argv[3]);
    }



    std::cout << "1: varyWeight; 2: SizeOverNodes; 3: PhiPlot" << std::endl;
    nodes = 70;

    int choice;
    std::cin >> choice;
    switch(choice){
    case 1:
        if(argc < 4){
            std::cout << "2D" << std::endl;
            varyWeigth<2>(dimx, dimy, dimz, nodes);
        } else {
            std::cout << "3D" << std::endl;
            varyWeigth<3>(dimx, dimy, dimz, nodes);
        }
        break;
    case 2:
        if(argc < 4){
            std::cout << "2D" << std::endl;
            sizeOverNodes<2>(dimx, dimy, dimz, nodes);
        } else {
            std::cout << "3D" << std::endl;
            sizeOverNodes<3>(dimx, dimy, dimz, nodes);
        }
        break;
    case 3:
        if(argc < 4){
            std::cout << "2D" << std::endl;
            phiPlot<2>(dimx, dimy, dimz, nodes);
        } else {
            std::cout << "3D" << std::endl;
            phiPlot<3>(dimx, dimy, dimz, nodes);
        }
        break;
    }

    MPI_Finalize();
}
