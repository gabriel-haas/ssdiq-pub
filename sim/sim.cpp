#include "Env.hpp"
#include "Time.hpp"
#include "PatternGen.hpp"
#include "SSD.hpp"
#include "Greedy.hpp"
#include "TwoR.hpp"

#include <cstdint>
#include <iostream>
#include <cassert>
#include <random>
#include <ostream>
#include <filesystem>

using std::cout;
using std::endl;
using std::string;
using namespace iob;
namespace fs = std::filesystem;

template <typename GCAlgo>
void runBench(GCAlgo &gc, SSD &ssd, PatternGen &pg, bool initLoad = false) {
    std::random_device randDevice;
    std::mt19937_64 rng{randDevice()};

    // seq init, guarantees ssd is full,
    if (initLoad) {
        for (uint64_t i = 0; i < ssd.logicalPages; i++) {
            gc.writePage(i);
        }

        // a batch of writes based on access pattern to fill OP 
        //uint64_t writeOP = ssd.physicalPages - ssd.logicalPages;
        uint64_t writeOP = ssd.physicalPages;
        for (uint64_t i = 0; i < writeOP; i++) {
            uint64_t logPage = pg.accessPatternGenerator(rng);
            gc.writePage(logPage);
        }

        cout << "Init WA: " << std::to_string(((float)ssd.physWrites()) / ssd.logicalPages) << endl;
        ssd.resetPhysicalCounters();
        gc.resetStats();
    }

    std::ofstream logFile("runBench.csv");
    if (!logFile.is_open()) {
        std::cerr << "Error opening runBench log file." << std::endl;
        return;
    }

    std::string header = "sim,hash,rep,time,capacity,erase,pagesize,pattern,skew,zones,alpha,beta,ssdFill,freePercentaftergc,gc,runningWAF,cumulativeWAF";
    cout << header << endl;
    logFile << header << endl;
    std::string logHash = mean::getTimeStampStr();

    // bench
    uint64_t writesPerRep = ssd.logicalPages / 4.0; // 1/10th of ssd size
    uint64_t numReps = pg.options.totalWrites / writesPerRep;
    uint64_t  cumulativePhysWrites = 0;  // Cumulative physical writes across all repetitions
    uint64_t  cumulativeLogWrites = 0;   // Cumulative logical writes across all repetitions
    auto start = mean::getSeconds();
    for (uint64_t rep = 0; rep < numReps; rep++) {
        for (uint64_t i = 0; i < writesPerRep; i++) {
            uint64_t logPage = pg.accessPatternGenerator(rng);
            gc.writePage(logPage);
            cumulativeLogWrites++;
        }

        cumulativePhysWrites += ssd.physWrites();
        float currentWAF = ((float)ssd.physWrites()) / writesPerRep;
        float cumulativeWAF = (float)((float)cumulativePhysWrites / (float)cumulativeLogWrites);
        auto now = mean::getSeconds();
        string s = "bench," + logHash + "," + std::to_string(rep) + "," + std::to_string(now - start) + "," + std::to_string(ssd.capacity) + "," + std::to_string(ssd.zoneSize) + "," + std::to_string(ssd.pageSize) + ",";
        s += pg.options.patternString + "," + std::to_string(pg.options.skewFactor) + ",'" + pg.patternDetails() + "'," + std::to_string(pg.options.alpha) + "," + std::to_string(pg.options.beta) + "," + std::to_string(ssd.ssdFill) + ",";
        s += std::to_string(writesPerRep / (float)ssd.physWrites());
        s += "," + gc.name() + "," + std::to_string(currentWAF) + "," + std::to_string(cumulativeWAF) + "\n";

        cout << s << std::flush;
        logFile << s << std::flush;
        ssd.resetPhysicalCounters();
        //ssd.printBlocksStats();
        gc.stats();
    }
    //ssd.printBlocksStats();

    logFile.close();

    //pg.generateAccessFrequencyHistogram(ssd.writtenPages, ssd.ssdFill);
    // Save the access pattern data to file and generate the plot
}

int main() {
    uint64_t pageSize = getBytesFromString(getEnv("PAGE", "4K"));
    uint64_t capacity = getBytesFromString(getEnv("CAPACITY", "16G"));
    uint64_t blockSize = getBytesFromString(getEnv("ERASE", "8M"));

    // SSD options
    string prefix = getEnv("PREFIX", "sim");
    float ssdFill = stof(getEnv("SSDFILL", "0.875"));
    if (ssdFill <= 0 || ssdFill >= 1) {
        throw std::runtime_error("ssdFill must be in (0, 1)");
    }
    string initialLoad = getEnv("LOAD", "true");
    bool initLoad = (initialLoad == "true");
    SSD ssd(capacity, blockSize, pageSize, ssdFill);
    ssd.printInfo();
    
    // GC options
    string gcAlgorithm = getEnv("GC", "greedy");

    auto pgOptions = iob::PatternGen::loadOptionsFromEnv(ssd.logicalPages, ssd.pageSize);
    iob::PatternGen::printPatternHistorgram(pgOptions);
    // Pattern generation options
    PatternGen pg(pgOptions);

    if (gcAlgorithm == "greedy") {
        GreedyGC greedy(ssd);
        runBench(greedy, ssd, pg,  initLoad);
    } else if (gcAlgorithm.contains("greedy-k")) {
        int k = std::stoi(gcAlgorithm.substr(8));
        GreedyGC greedy(ssd, k);
        runBench(greedy, ssd, pg, initLoad);
    } else if (gcAlgorithm.contains("greedy-s2r")) {
        GreedyGC greedy(ssd, 0, true);
        runBench(greedy, ssd, pg, initLoad);
    } else if (gcAlgorithm.contains("2r")) {
        TwoR twoR(ssd, gcAlgorithm);
        runBench(twoR, ssd, pg, initLoad);
    } else {
        throw std::runtime_error("unknown gc algorithm: " + gcAlgorithm);
    }
    return 0;
}
