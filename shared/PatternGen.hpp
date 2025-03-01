#pragma once

#include "Exceptions.hpp"
#include "Env.hpp"
#include "RejectionInversionZipf.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <csignal>
#include <cstdint>
#include <locale>
#include <random>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <numeric>
#include <sstream>
#include <cmath>
#include <fstream>
#include <vector>
#include <cstdio>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <filesystem>
#include <numeric>
#include <cstdlib> 


namespace iob {

using std::cout;
using std::endl;
using std::string;
namespace fs = std::filesystem;

class PatternGen {
public:
    enum class Pattern {
        Sequential,
        Uniform,
        Beta,
        Zones,
        Zipf,
        ZNS,
        SeqZones,
        Undefined
    };
    struct Options {
        string patternString;
        string zonesString;
        uint64_t logicalPages;
        double alpha;
        double beta;
        double skewFactor;
        uint64_t totalWrites;
        uint64_t pageSize;
        uint64_t sectorSize;
        uint64_t znsActiveZones;
        uint64_t znsPagesPerZone;
    };
    Options options;
    const Pattern pattern;
    bool shuffle;
    // Shuffle
    std::vector<uint64_t> updatePattern;

    // Sequential
    std::atomic<uint64_t> seq = 0;

    // Uniform
    std::uniform_int_distribution<uint64_t> rndPage;

    // Zones (and SeqZones)
    struct AccessZone {
        uint64_t offset = 0;
        uint64_t count = 0;
        double rel_size = 1;
        double freq = 100;
        Pattern pattern = Pattern::Uniform;
        double skewFactor = 0;
        bool shuffle = false;
        unique_ptr<PatternGen> subGen; // recursive sub-pattern generator
       
        AccessZone(double rel_size, double rel_freq, Pattern pattern, double skewFactor, bool shuffle)
            : rel_size(rel_size), freq(rel_freq), pattern(pattern), skewFactor(skewFactor), shuffle(shuffle) {
        }

        void print() {
            std::cout << "{hint: o: " << offset << " c: " << count << " f: " << freq << " rels: " << rel_size;
            std::cout << " p: " << to_string((uint64_t)pattern) << " shuffle: " << shuffle << "}";
        }
    };

    // Zipf
    RejectionInversionZipfSampler zipfSampler;

    // Traces
    std::vector<uint64_t> inputTraces;
    size_t traceIndex = 0;
    std::string traceFilePath;
    const size_t chunkSize = 100000; // trace file chunk size to load on the memory

    // ZNS
    uint64_t znsZones;
    std::mutex znsMutex;
    std::vector<int64_t> znsActiveZoneIds;
    std::vector<int64_t> znsActiveZoneOffset;

    // Trace accesses
    bool traceAccessedPages = false;
    std::vector<uint64_t> accessedPages;
public:
    PatternGen(Options options) : options(options), pattern(stringToPattern(options.patternString)), zipfSampler(options.logicalPages, options.skewFactor) {
        shuffle = !options.patternString.contains("-noshuffle"); // default is shuffle
        if (pattern == Pattern::Sequential
            || pattern == Pattern::SeqZones
            || pattern == Pattern::ZNS) { // except for
            shuffle = options.patternString.contains("-shuffle");
        }
        rndPage = std::uniform_int_distribution<uint64_t>(0, options.logicalPages - 1);
        std::cout << "PatternGen: shuffle: " << shuffle << " totalWrites: " << options.totalWrites << std::endl;
        init();
    }
    PatternGen(Pattern pattern, uint64_t logicalPages, double skewFactor = 1.0, bool shuffle = true)
        : pattern(pattern), zipfSampler(logicalPages, skewFactor), shuffle(shuffle)  {
            options.logicalPages = logicalPages;
            options.skewFactor = skewFactor;
        rndPage = std::uniform_int_distribution<uint64_t>(0, options.logicalPages - 1);
        init();
    }

    static Options loadOptionsFromEnv(uint64_t logicalPages, uint64_t pageSize) {
        iob::PatternGen::Options pgOptions;
        pgOptions.logicalPages = logicalPages;
        pgOptions.pageSize = pageSize;
        pgOptions.patternString = getEnv("PATTERN", "uniform");
        pgOptions.zonesString = getEnv("ZONES", "s0.9 f0.1 uniform s0.1 f0.9 uniform");
        pgOptions.alpha = stof(getEnv("ALPHA", "1"));
        pgOptions.beta = stof(getEnv("BETA", "1"));
        uint64_t writePercentage = std::stoull(getEnv("WRITES", "10"));
        pgOptions.totalWrites = static_cast<uint64_t>(writePercentage * logicalPages);
        pgOptions.sectorSize = std::stoul(getEnv("SECTOR_SIZE", "512")); // Sector size for trace parsing
        pgOptions.znsActiveZones = std::stoul(getEnv("ZNS_ACTIVE_ZONES", "4"));
        pgOptions.znsPagesPerZone = getBytesFromString(getEnv("ZNS_ZONE_SIZE", "1G")) / pageSize;
        pgOptions.skewFactor = 1.0;
        if (pgOptions.patternString.contains("zipf")) {
            pgOptions.skewFactor = stof(getEnv("ZIPF", "1.0"));
        }
        if (!pgOptions.patternString.contains("zones")) {
            pgOptions.zonesString = "";
        }
        return pgOptions;
    }

    static Pattern stringToPattern(std::string pattern) {
        Pattern ret;
        if (pattern.contains("sequential")) {
            ret = Pattern::Sequential;
        } else if (pattern.contains("uniform")) {
            ret = Pattern::Uniform;
        } else if (pattern.contains("beta")) {
            ret = Pattern::Beta;
        } else if (pattern.contains("seqzones")){ // check before zones
            ret = Pattern::SeqZones;
        } else if (pattern.contains("zones")) {
            ret = Pattern::Zones;
        } else if (pattern.contains("zipf")) {
            ret = Pattern::Zipf;
        } else if (pattern.contains("trace")) {
            ensurem(false, "Currently no supported: " + pattern);
        } else{
            ensurem(false, "Pattern not supported: " + pattern);
        }
        return ret;
    }

    std::string patternDetails() {
        std::string patternDetails;
        if (pattern == Pattern::Zones || pattern == Pattern::SeqZones) {
            patternDetails = options.zonesString; 
        } else if (pattern == Pattern::Beta) {
           patternDetails = "a:" + to_string(options.alpha) + " b:" + to_string(options.beta);
        } else if (pattern == Pattern::Zipf) {
           patternDetails = to_string(options.skewFactor);
        } else if (pattern == Pattern::ZNS) {
            patternDetails = "activeZones: " + to_string(options.znsActiveZones) + " pagesPerZone: " + to_string(options.znsPagesPerZone); 
        }
        return patternDetails;
    }

    void init() {
        // shuffle LBA space if true
        if (this->pattern == Pattern::Zones) {
            parseAndInitZoneAccessPattern();
        } else if (this->pattern == Pattern::SeqZones) {
            parseAndInitSeqZoneAccessPattern();
        } else if (this->pattern == Pattern::ZNS) {
            parseAndInitZNSAccessPattern();
        }
        if (shuffle) {
            updatePattern.resize(options.logicalPages);
            for (uint64_t i = 0; i < updatePattern.size(); i++) {
                updatePattern[i] = i;
            }
            std::random_device rd;
            std::mt19937_64 g(rd());
            std::shuffle(updatePattern.begin(), updatePattern.end(), g);
        }
    }

    int64_t accessPatternGenerator(std::mt19937_64& gen) {
        uint64_t page = 0;
        if (pattern == Pattern::Sequential) {
            page = seq++ % options.logicalPages;
        } else if (pattern == Pattern::Uniform) {
            page = rndPage(gen);
        } else if (pattern == Pattern::SeqZones) {
            page = accessZonesGenerator(gen);
        } else if (pattern == Pattern::Zones) {
            page = accessZonesGenerator(gen);
        } else if (pattern == Pattern::Beta) {
            double beta_val;
            beta_val = beta_distribution(gen, options.alpha, options.beta);
            page = beta_val * (options.logicalPages - 1);
        } else if (pattern == Pattern::Zipf) {
            page = zipfSampler.sample(gen) - 1; // produces values in [1, n]
        } else if(pattern == Pattern::ZNS){
            page = accessZNS(gen);
        } else {
           throw std::runtime_error("Error: pattern not implemented.");
        }
        if (shuffle) {
           if (!(page >= 0 && page < updatePattern.size())) {
              raise(SIGINT);
           }
           page = updatePattern.at(page);
        }
        if (page < 0 || page > options.logicalPages) {
            cout << "invalid pids: " << page << endl;
            raise(SIGINT);        
        }
        // used for histogram generation
        if (traceAccessedPages) {
            accessedPages.push_back(page);
        }
        //std::cout << "page: " << page << std::endl;
        ensure(page >= 0 && page < options.logicalPages);
        return page;
    }

    double sumFreq = 0; 
    std::vector<AccessZone> accessZones;

    void parseZoneSizes(const std::string& str) {
        std::stringstream ss(str);
        std::string value;
        double size = -1;
        double freq = -1;
        Pattern pattern = Pattern::Uniform;
        double skewFactor = -1;
        bool shuffle = false; // default is don't shuffle, if zones are globally shuffled than another shuffle inside the zone is redundant 
        auto addZone = [&]() {
            if(size > 0) {
                accessZones.emplace_back(size, freq, pattern, skewFactor, shuffle);
                size = -1; freq = -1; 
                pattern = Pattern::Uniform;
                skewFactor = -1;
                shuffle = false;
            }
        };
        while (ss >> value) {
            if (value[0] == 's' && value != "shuffle" && value != "sequential") {
                addZone();
                size = std::stod(value.substr(1));
            }
            else if (value[0] == 'f') {
                freq = std::stod(value.substr(1));
                std::cout << "freq: " << freq << std::endl;
            }
            else if (value == "uniform") {
                pattern = Pattern::Uniform;
            }
            else if (value.substr(0,4) == "zipf") {
                pattern = Pattern::Zipf;
                skewFactor = std::stod(value.substr(4));
            }
            else if (value == "sequential") {
                pattern = Pattern::Sequential;
            }
            else if (value == "shuffle") {
                shuffle = true;
            }
            else if (value == "noshuffle") {
                shuffle = false;
            }
            else {
                ensurem(false, "Zone keyword not recognized: " + value);
            }
        }
        addZone();
    }

    void initZoneAccessPattern() {
        const long pageCount = options.logicalPages;
        double sumRelSizes = 0;
        for (auto &hz: accessZones) {
            sumRelSizes += hz.rel_size;
        }
        //cout << "accessZones: " << accessZones.size() << endl;
        long assigned = 0;
        for (auto &h: accessZones) {
            h.count = (double) h.rel_size / sumRelSizes * pageCount;
            assigned += h.count;
            sumFreq += h.freq;
        }
        long remaining = pageCount - assigned;
        ensurem(remaining >= 0, "Remaining pages negative " + to_string(remaining));
        int i = 0;
        while (remaining > 0) {
           accessZones[i++ % accessZones.size()].count += 1;
           remaining--;
        }
        long lastEnd = 0;
        long assigned2 = 0;
        for (auto &h: accessZones) {
            h.offset = lastEnd;
            assigned2 += h.count;
            lastEnd += h.count;
        }
        ensurem(pageCount == assigned2, "Pages not correclyt assinged")
        for (auto& h: accessZones) {
            h.subGen = std::make_unique<PatternGen>(h.pattern, h.count, h.skewFactor, h.shuffle);
            h.print(); cout << endl;
        }
        cout << endl;
        cout << "sumFreq: " << sumFreq << endl;
    }

    void parseAndInitZoneAccessPattern() {
        cout << "wf" << endl;
        parseZoneSizes(options.zonesString);
        initZoneAccessPattern();
    }

    void parseAndInitSeqZoneAccessPattern() {
        cout << "wf" << endl;
        const int zones = std::stoi(options.zonesString);
        float f = std::pow(10, 1.0/(zones-1));
        for (int i = 0; i < zones; i++) {
           accessZones.emplace_back(1, std::pow(f, i), Pattern::Sequential, -1, false);
        }
        initZoneAccessPattern();
    }

    uint64_t accessZonesGenerator(std::mt19937_64& gen) {
        std::uniform_real_distribution<double> realDist(0, sumFreq);
        double randFreq = realDist(gen);
        int randZoneId = 0;
        double freqCnt = accessZones.at(0).freq;
        while (freqCnt < randFreq) {
            randZoneId++;
            freqCnt += accessZones.at(randZoneId).freq;
        }
        assert(randZoneId >= 0 && randZoneId < accessZones.size());
        auto& az = accessZones.at(randZoneId);
        //std::cout << "randZoneId: " << randZoneId << std::endl;
        //std::uniform_int_distribution<long> rndPageInZone(az.offset, az.offset + az.count - 1);
        //uint64_t idx = rndPageInZone(gen);
        uint64_t idx = az.offset + az.subGen->accessPatternGenerator(gen);
        //std::cout << "randZoneId: " << randZoneId << " idx: " << idx << " (offset: " << az.offset << ")" << std::endl;
        return idx;
    }

    uint64_t accessDBGenerator(std::mt19937_64& gen) {
        std::uniform_real_distribution<double> realDist(0, sumFreq);
        double randFreq = realDist(gen);
        int randZoneId = 0;
        double freqCnt = accessZones.at(0).freq;
        while (freqCnt < randFreq) {
            randZoneId++;
            freqCnt += accessZones.at(randZoneId).freq;
        }
        assert(randZoneId >= 0 && randZoneId < accessZones.size());
        auto& az = accessZones.at(randZoneId);
        std::uniform_int_distribution<long> rndPageInZone(az.offset, az.offset + az.count - 1);
        uint64_t idx = rndPageInZone(gen);
        if (!(idx >= 0 && idx < updatePattern.size())) {
            raise(SIGINT);
        }
        return updatePattern.at(idx);
    }

    void parseAndInitZNSAccessPattern() {
        cout << "zns" << endl;
        ensure(options.logicalPages > options.znsPagesPerZone);
        znsZones = options.logicalPages / options.znsPagesPerZone;
        ensure(options.znsActiveZones < znsZones);
        uint64_t unusedRest = options.logicalPages % options.znsPagesPerZone;
        std::cout << "Remaining SSD space: " << unusedRest*options.pageSize/MEGA << " MB (" << unusedRest/(options.logicalPages*options.pageSize)*100 << "%)" << std::endl;
        uint64_t offset = 0;
        float f = std::pow(10, 1.0/(options.znsActiveZones-1));
        for (int i = 0; i < options.znsActiveZones; i++) {
           //accessZones.emplace_back(1, std::pow(f, i), Pattern::Undefined, -1, false);
           accessZones.emplace_back(1, 1, Pattern::Undefined, -1, false);
        }
        // we use the zone init function to setup frequencies
        initZoneAccessPattern();
        znsActiveZoneIds.resize(options.znsActiveZones);
        std::fill(znsActiveZoneIds.begin(), znsActiveZoneIds.end(), -1);
        znsActiveZoneOffset.resize(options.znsActiveZones);
        std::fill(znsActiveZoneOffset.begin(), znsActiveZoneOffset.end(), options.znsPagesPerZone);
    }

    uint64_t accessZNS(std::mt19937_64& gen) {
        std::uniform_real_distribution<double> realDist(0, sumFreq);
        double randFreq = realDist(gen);
        int randZoneId = 0;
        double freqCnt = accessZones.at(0).freq;
        while (freqCnt < randFreq) {
            randZoneId++;
            freqCnt += accessZones.at(randZoneId).freq;
        }
        assert(randZoneId >= 0 && randZoneId < accessZones.size());
        // have to mutex the following
        std::lock_guard<std::mutex> guard(znsMutex);
        uint64_t idInZone = znsActiveZoneOffset[randZoneId]++;
        uint64_t idx;
        if (idInZone >= options.znsPagesPerZone) {
            // pick new zone, check if not already in use, reset offset to zero
            std::uniform_int_distribution<long> randomZoneDist(0, znsZones-1);
            uint64_t randomZone;
            do {
                randomZone = randomZoneDist(gen);
            } while (std::find(znsActiveZoneIds.begin(), znsActiveZoneIds.end(), randomZone) != znsActiveZoneIds.end());
            znsActiveZoneIds[randZoneId] = randomZone;
            znsActiveZoneOffset[randZoneId] = 0;
            idx = znsActiveZoneIds[randZoneId]*options.znsPagesPerZone + znsActiveZoneOffset[randZoneId]++;
        } else {
            idx = znsActiveZoneIds[randZoneId]*options.znsPagesPerZone + idInZone;
        }
        //std::cout << "idx: "  << randZoneId << " az: " << znsActiveZoneIds[randZoneId] << " idx: " << idx << " offset: " << znsActiveZoneOffset[randZoneId]-1 << std::endl; 
        return idx;
    }    

    static double beta_distribution(std::mt19937_64 &gen, double alpha, double beta) {
        std::gamma_distribution<> X(alpha, 1.0);
        std::gamma_distribution<> Y(beta, 1.0);
        double x = X(gen);
        double y = Y(gen);
        double beta_sample = x / (x + y);
        return beta_sample;
    }

    static void printPatternHistorgram(Options options) {
        cout << "Distribution Histogram" << endl;
        options.logicalPages = 100;
        options.znsPagesPerZone = 10;
        options.znsActiveZones = 4;
        long totalWrites = 10e6;
        std::random_device rd;
        std::mt19937_64 rng{rd()};
        PatternGen pg(options);
        std::vector<int> vec(options.logicalPages);
        for (uint64_t i = 0; i < totalWrites; i++) {
            uint64_t page = pg.accessPatternGenerator(rng);
            vec.at(page)++;
        }
        printHistogram(vec);
        cout << "Pattern: " << options.patternString << endl;
    }
	
    static void printHistogram(const std::vector<int> &histogram, bool useLog = false) {
        std::vector vec(histogram);
        const int max = *max_element(vec.begin(), vec.end());
        const int min = *min_element(vec.begin(), vec.end());
        if (useLog) {
            for (auto &v: vec) {
                v = round(1.0 * log(v) / log(max) * 10);
            }
        } else {
            for (auto &v: vec) {
                v = round(1.0 * v / max * 10);
            }
        }
        cout << "(min: " << min << ", max: " << max << ")" << endl;
        size_t maxCount = *max_element(vec.begin(), vec.end());
        for (int row = maxCount; row > 0; row--) {
            for (size_t col = 0; col < vec.size(); col++) {
                if (vec[col] >= row) {
                    cout << "#";
                } else {
                    cout << " ";
                }
            }
            cout << "\n";
        }
        for (size_t col = 0; col < vec.size(); col++) {
            if ((col) % 10 == 0) {
                cout << "|";
            } else {
                cout << "-";
            }
        }
        cout << endl;
        for (size_t col = 0; col <= vec.size(); col += 10) {
            cout << col << std::setw(10);
        }
        cout << endl;
    }
};
}

