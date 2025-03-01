//
// Created by Gabriel Haas on 02.07.24.
//
#pragma once

#include "../shared/Exceptions.hpp"

#include <cstdint>
#include <list>
#include <unordered_map>
#include <map>
#include <vector>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <functional>
#include <limits>
#include <cmath>
//#include <format>

class SSD {
public:
   constexpr static uint64_t unused = ~0ull;
   const double ssdFill; // 1-alpha [0-1]
   const uint64_t capacity; // bytes
   const uint64_t zoneSize; // bytes
   const uint64_t pageSize; // bytes
   const uint64_t zones;
   const uint64_t pagesPerZone;
   const uint64_t logicalPages;
   const uint64_t physicalPages;

   /* stats */
   std::vector<uint64_t>writtenPages;

   // Write buffer properties (with LFU replacement policy)
   const uint64_t writeBufferSize; // Size of the write buffer in terms of the number of pages
   std::list<uint64_t> writeBuffer; // LRU cache implemented with a list
   // LFU
   //std::unordered_map<uint64_t, std::pair<uint64_t, std::list<uint64_t>::iterator>> writeBufferMap; // Maps logPage to {frequency, iterator in freqList}
   std::map<uint64_t, std::list<uint64_t>> freqList; // Maps frequency to a list of pages
   uint64_t minFreq = 1;
   static constexpr double writeBufferSizePct = 0; //0.0002;
   // LRU
   std::unordered_map<uint64_t, std::list<uint64_t>::iterator> writeBufferMap; // Map to quickly find pages in the buffer
   
   class Block {
      uint64_t _writePos = 0;
      uint64_t _validCnt = 0;
      uint64_t _eraseCount = 0;
      inline static uint64_t _eraseAgeCounter = 0;
      std::vector<uint64_t> _ptl; // phy to log
   public:
      const std::vector<uint64_t>& ptl() const { return _ptl; }
      const uint64_t validCnt() const { return _validCnt; }
      const uint64_t writePos() const { return _writePos; }
      const uint64_t pagesPerZone;
      const uint64_t blockId;
      std::vector<uint64_t> invalidationTimes;
      int64_t gcAge = -1;
      int64_t gcGeneration = 0;
      int64_t group = -1;
      bool writtenByGc = false;
      Block(uint64_t pagesPerBlock, uint64_t blockId) : _ptl(pagesPerBlock), blockId(blockId), pagesPerZone(pagesPerBlock) {
         fill(_ptl.begin(), _ptl.end(), unused);
      }
      uint64_t write(uint64_t logPageId) {
         ensure(canWrite());
         ensure(_ptl[_writePos]==unused);
         _ptl[_writePos] = logPageId;
         _validCnt++;
         return _writePos++;
      }
      void setUnused(uint64_t pos) {
         ensure(_ptl[pos] != unused);
         _ptl[pos] = unused;
         _validCnt--;
      }
      bool fullyWritten() const {
         return _writePos == pagesPerZone;
      }
      bool canWrite() const {
         return _writePos < pagesPerZone;
      }
      bool allValid() const {
         return _validCnt == pagesPerZone;
      }
      bool allInvalid() const {
         return _validCnt == 0;
      }
      bool isErased() const {
         return _writePos == 0;
      }
      void compactNoMappingUpdate() {
         _writePos = 0;
         // unlike a real gc that actually moves valid pages to a clean zone
         // before erasing, we just move pages to the beginning of gced zone
         for (uint64_t p=0; p<pagesPerZone; p++) {
            const uint64_t logpagemove = _ptl[p];
            if (logpagemove != unused) {
               // move page to beginning of zone (might overwrite itself)
               _ptl[_writePos] = logpagemove;
               _writePos++;
            }
         }
         _validCnt = _writePos;
         fill(_ptl.begin()+_writePos, _ptl.end(), unused);
          // this counts as erase
         _eraseCount++;
         gcAge = _eraseAgeCounter++;
         writtenByGc = true;
      }
      void erase() {
         // careful, compact is also an erase
         fill(_ptl.begin(), _ptl.end(), unused);
         _writePos = 0;
         _eraseCount++;
         gcAge = _eraseAgeCounter++;
         writtenByGc = false; // reset
         _validCnt = 0;
         group = -1;
      }
      void print() {
         std::cout << "age: " << gcAge << " gcGen: " << gcGeneration << " wbgc: "<< writtenByGc << " vc: " << _validCnt ;
      }
   };
private:
   std::vector<Block> _blocks; // phys -> logPageId // FIXME rename blocks
   std::vector<uint64_t> _ltpMapping; // logPageId -> physAddr
   std::vector<uint64_t> _mappingUpdatedCnt; // stats
   std::vector<uint64_t> _mappingUpdatedGC; // stats
   uint64_t _physWrites = 0;
   // stats
   uint64_t gcedNormalBlock = 0;
   uint64_t gcedColdBlock = 0;
public:
   //const Block& blocks(uint64_t idx) const { return _blocks.at(idx); } // only give read only access
   const decltype(_blocks)& blocks() const { return _blocks; } // only give read only access
   const decltype(_ltpMapping)& ltpMapping() const { return _ltpMapping; }
   const decltype(_mappingUpdatedCnt)& mappingUpdatedCnt() const { return _mappingUpdatedCnt; }
   const decltype(_mappingUpdatedGC)& mappingUpdatedGC() const { return _mappingUpdatedGC; }
   const uint64_t physWrites() const { return _physWrites; }
   void hackForOptimalWASetPhysWrites(uint64_t phyWrites) {  _physWrites = phyWrites; }
   SSD(uint64_t capacity, uint64_t zoneSize, uint64_t pageSize, double ssdFill)
         : capacity(capacity), zoneSize(zoneSize), pageSize(pageSize), ssdFill(ssdFill),
           zones(capacity/zoneSize), pagesPerZone(zoneSize/pageSize), logicalPages((capacity/pageSize)*ssdFill), physicalPages(zones*pagesPerZone),
           writeBufferSize(static_cast<uint64_t>(logicalPages * writeBufferSizePct))
   {
      // init by sequentially filling blocks
      _ltpMapping.resize(logicalPages);
      _mappingUpdatedCnt.resize(logicalPages);
      _mappingUpdatedGC.resize(logicalPages);

      fill(_ltpMapping.begin(), _ltpMapping.end(), unused);
      for (unsigned z=0; z<zones; z++) {
         _blocks.emplace_back(Block(pagesPerZone, z));
      }
   }

   uint64_t getZone(uint64_t physAddr) const { return physAddr / pagesPerZone; }
   uint64_t getPage(uint64_t physAddr) const { return physAddr % pagesPerZone; }
   uint64_t getAddr(uint64_t zone, uint64_t pos) const { return (zone*pagesPerZone) + pos; }

   void writePage(uint64_t logPage, uint64_t block, int64_t group = -1) {
      writePage(logPage, _blocks[block], group);
   }

   // LFU
   // void writePage(uint64_t logPage, Block& block) {
   //    if (writeBufferMap.find(logPage) != writeBufferMap.end()) {
   //          // buffer hit
   //          auto& [freq, it] = writeBufferMap[logPage];
   //          freqList[freq].erase(it);
   //          if (freqList[freq].empty() && freq == minFreq) {
   //              freqList.erase(freq);
   //              minFreq++;
   //          }
   //          freq++;
   //          freqList[freq].push_front(logPage);
   //          writeBufferMap[logPage] = {freq, freqList[freq].begin()};
   //    } else {
   //          // buffer miss, evict a page (lfuPage) from the wbuffer
   //          uint64_t lfuPage;
   //          if (writeBufferMap.size() >= writeBufferSize) {
   //              // Buffer is full, evict the least frequently used page
   //              lfuPage = freqList[minFreq].back();
   //              freqList[minFreq].pop_back();
   //              if (freqList[minFreq].empty()) {
   //                  freqList.erase(minFreq);
   //              }
   //              writeBufferMap.erase(lfuPage);
   //          }
   //          minFreq = 1;
   //          freqList[minFreq].push_front(logPage);
   //          writeBufferMap[logPage] = {minFreq, freqList[minFreq].begin()};
        

   //    // perform lfu page write
   //    uint64_t addr = _ltpMapping[lfuPage];
   //    if (addr != unused) {
   //       uint64_t z = getZone(addr);
   //       uint64_t p = getPage(addr);
   //       ensure(z < _blocks.size());
   //       Block& b = _blocks[z];
   //       _blocks[z].setUnused(p);
   //    }
   //    uint64_t writePos = block.write(lfuPage);
   //    _ltpMapping[lfuPage] = getAddr(block.blockId, writePos);
   //    _mappingUpdatedCnt[lfuPage]++;
   //    _physWrites++;
   //    writtenPages.push_back(lfuPage);
   //    }
   // }
    void writePage(uint64_t logPage, Block& block, int64_t group = -1) {
      if (writeBufferSize == 0) {
         writePageWithoutCaching(logPage, block, group);
      } else {
         if (writeBufferMap.find(logPage) != writeBufferMap.end()) {
            // Buffer hit, move it to the front (most recently used)
            writeBuffer.erase(writeBufferMap[logPage]);
            writeBuffer.push_front(logPage);
            writeBufferMap[logPage] = writeBuffer.begin();
         } else {
            // Buffer miss
            // push the logPage infront of the LRU list
            writeBuffer.push_front(logPage);
            writeBufferMap[logPage] = writeBuffer.begin();
            if (writeBuffer.size() >= writeBufferSize) {
               // Buffer is full, evict the least recently used page
               uint64_t lruPage = writeBuffer.back();
               writeBufferMap.erase(lruPage);
               writeBuffer.pop_back();
               writePageWithoutCaching(lruPage, block, group);
            }
         }
      }
    }

   // only use from GCup
   void writePageWithoutCaching(uint64_t logPage, Block& block, int64_t group = -1) {
      if (block.group == -1) {
         //std::cout << "set group: " << group << std::endl;
         block.group = group;
      }
      uint64_t addr = _ltpMapping.at(logPage);
      if (addr != unused) { // page is updated, not new
         uint64_t z = getZone(addr);
         uint64_t p = getPage(addr);
         ensure(z < _blocks.size());
         Block& b = _blocks.at(z);
         _blocks.at(z).setUnused(p);
      }
      uint64_t writePos = block.write(logPage);
      _ltpMapping[logPage] = getAddr(block.blockId, writePos);
      _mappingUpdatedCnt[logPage]++;
      _physWrites++;
      //writtenPages.push_back(logPage);
    }

   void eraseBlock(Block& block) {
      block.erase();
   }

   void eraseBlock(uint64_t blockId) {
      ensure(blockId < _blocks.size());
      _blocks[blockId].erase();
      ensure(_blocks[blockId].isErased());
   }

   void compactBlock(uint64_t & block) {
      compactBlock(_blocks[block]);
   }
   void compactBlock(Block& block) { // compacts a block by moving active data to the front ~ erase
      block.compactNoMappingUpdate();
      block.gcGeneration++;
      if (block.writtenByGc) {
         gcedColdBlock++;
      } else {
         gcedNormalBlock++;
      }
      block.writtenByGc = true;
      // update mapping for all pages in block
      for (uint64_t p=0; p<block.writePos(); p++) {
         uint64_t logPage = block.ptl()[p];
         ensure(block.ptl()[p] != unused);
         _ltpMapping[logPage] = getAddr(block.blockId, p);
         _mappingUpdatedGC[logPage]++;
         _physWrites++;
      }
   }

   // tries to move valid pages to destination block, set moved pages from source to invalid
   // does not erase source
   bool moveValidPagesTo(uint64_t sourceId, uint64_t destinationId) {
      Block& source = _blocks[sourceId];
      Block& destination = _blocks[destinationId];
      //ensure(!source.allValid());
      if (source.writtenByGc) {
         gcedColdBlock++;
      } else {
         gcedNormalBlock++;
      }
      destination.writtenByGc = true;
      uint64_t p = 0;
      while (p < pagesPerZone && destination.canWrite()) {
         if (source.ptl()[p] != unused) {
            writePageWithoutCaching(source.ptl()[p], destination);
         }
         p++;
      }
      return !source.allInvalid();
   }

   int64_t moveValidPagesTo(uint64_t sourceId, std::function<std::tuple<int64_t,int64_t>(uint64_t)> destinationFun) {
      Block& source = _blocks[sourceId];
      ensure(!source.allValid());
      if (source.writtenByGc) {
         gcedColdBlock++;
      } else {
         gcedNormalBlock++;
      }
      uint64_t p = 0;
      int64_t firstFullDestinationId = -1;
      while (p < pagesPerZone) {
         uint64_t lba = source.ptl()[p];
         if (lba != unused) {
            auto [destinationId, groupId] = destinationFun(lba);
            Block& destination = _blocks[destinationId];
            //std::cout << "moveValidPageTo: dest: " << destinationId << std::endl;
            if (destination.canWrite()) { // skip full destinations
               writePageWithoutCaching(source.ptl()[p], destination, groupId);
            } else if (firstFullDestinationId == -1) {
               //std::cout << "moveValidPageTo: first dest full: " << destinationId << std::endl;
               firstFullDestinationId = destinationId;
            }
         }
         p++;
      }
      if (source.allInvalid()) {
         return -1;
      } else {
         Block& dest = _blocks[firstFullDestinationId];
         ensure(!dest.canWrite());
         return firstFullDestinationId;
      }
   }

   // compacts blocks until a block is completely free
   // returns the free block and the last (not-full) gc block
   std::tuple<uint64_t, int64_t> compactUntilFreeBlock(int64_t gcBlockId, std::function<uint64_t()> nextBlock) {
      if (gcBlockId == -1 || _blocks[gcBlockId].allValid()) {
         gcBlockId = nextBlock();
         Block& gcBlock = _blocks[gcBlockId];
         compactBlock(gcBlock);
         ensure(!gcBlock.allValid());
      }
      ensure(!_blocks[gcBlockId].allValid());
      uint64_t victimId = nextBlock();
      while (moveValidPagesTo(victimId, gcBlockId)) {
         // not enough space in gcBlock, compact victimBlock and make it new gcBlock
         Block& victim = _blocks[victimId];
         Block& gcBlock = _blocks[gcBlockId];
         compactBlock(victim);
         gcBlockId = victimId;
         victimId = nextBlock();
      }
      Block& nowFree = _blocks[victimId];
      nowFree.erase();
      nowFree.gcGeneration = 0;
      return std::make_tuple(victimId, gcBlockId);
   }

   std::tuple<uint64_t, int64_t> compactUntilFreeBlock(int64_t groupId, 
         std::function<uint64_t(int64_t)> nextBlock, 
         std::function<std::tuple<int64_t,int64_t>(int64_t)> gcDestinationFun,
         std::function<void(int64_t, int64_t)> updateGroupFun) {
      uint64_t victimId = nextBlock(groupId);
      int64_t fullDest;
      do {
         // std::cout << "gc group: " << groupId << " victim: " << victimId << std::endl;
         fullDest = moveValidPagesTo(victimId, gcDestinationFun);
         if (fullDest == -1) {
            break; // victim is empty
         }
         // not enough space in destination, victim not empty, make victim new destination
         Block& dest = _blocks[fullDest];
         ensure(!dest.canWrite());
         Block& victim = _blocks[victimId];
         ensure(fullDest != victimId);
         compactBlock(victim);
         //std::cout << "compactUntil: full dest: " << fullDest << " wp: " << dest.writePos() << " dest.group: " << dest.group;
         //std::cout << " victim: " << victimId << " victim.wp: " << victim.writePos()<< std::endl;
         victim.group = dest.group;
         ensure(dest.group != -1);
         ensure(!dest.canWrite());
         updateGroupFun(dest.group, victimId);
         victimId = nextBlock(groupId);
      } while (true);
      Block& nowFree = _blocks[victimId];
      nowFree.erase();
      nowFree.gcGeneration = 0;
      return std::make_tuple(victimId, -1);
   }

   // compacts blocks until a block is completely free
   // returns the final GC block ID that has been erased
   uint64_t compactUntilFreeBlock(std::vector<uint64_t> victimBlockList) {
      // Ensure that the victimBlockList is not empty
      if (victimBlockList.empty()) {
         throw std::invalid_argument("victimBlockList must contain at least one block ID.");
      }

      // If there's only one block, check if it contains any valid pages
      if (victimBlockList.size() == 1) {
         uint64_t singleBlockId = victimBlockList[0];
         ensure(_blocks[singleBlockId].allInvalid());
            eraseBlock(singleBlockId);
            ensure(_blocks[singleBlockId].isErased());
         return singleBlockId;
      }

      uint64_t invalid_cnt = 0;

      // Compact and move valid pages from each block to the previous one
      for (size_t i = victimBlockList.size() - 1; i > 0; i--) {        
         uint64_t destBlockId = victimBlockList[i];
         compactBlock(_blocks[destBlockId]);
         uint64_t sourceBlockId = victimBlockList[i-1];
         moveValidPagesTo(sourceBlockId, destBlockId);
      }

      // Final compact operation on the first block in the list    
      uint64_t finalBlockId = victimBlockList[0];
      ensure(_blocks[finalBlockId].allInvalid());
      eraseBlock(finalBlockId);
      ensure(_blocks[finalBlockId].isErased());

      return finalBlockId;
   }
	
   void resetPhysicalCounters() {
      _physWrites = 0;
   }

   void printInfo() {
      cout << "capacity: " << capacity << " blocksize: " << zoneSize << " pageSize: " << pageSize << endl;
      cout << "blockCnt: " << zones << " pagesPerBlock: " << pagesPerZone << " logicalPages: " << logicalPages << " ssdfill: " << ssdFill << endl;
   }

   void stats() {
      auto writtenByGc = std::count_if(_blocks.begin(), _blocks.end(), [](Block& b) { return b.writtenByGc; });
      int64_t maxGCAge = 20;
      std::vector<uint64_t> gcGenerations(maxGCAge, 0);
      std::vector<uint64_t> gcGenerationValid(maxGCAge, 0);
      std::vector<uint64_t> gcGenerationValidMin(maxGCAge, std::numeric_limits<uint64_t>::max());
      // count gc generations
      for (auto& b : _blocks) {
         auto idx = std::min(b.gcGeneration, maxGCAge-1);
         gcGenerations[idx]++;
         gcGenerationValid[idx] += b.validCnt();
         if (b.fullyWritten()) {
            if (b.validCnt() > pagesPerZone) raise(SIGINT);
            gcGenerationValidMin[idx] = std::min(gcGenerationValidMin[idx], b.validCnt());
         }
      }
      cout << "writtenByGC: " <<  writtenByGc << " (" << std::round((float)writtenByGc/zones*100) << "%)" << " gcedNormal: " << gcedNormalBlock << " gcedCold: " << gcedColdBlock << endl;
      // print gc generations vector
      cout << "gc generations: [";
      for (int i = 0; i < maxGCAge; i++) {
         //cout << i << ": (c: " << std::format("{:.2f}", gcGenerations[i]*100.0/zones);
         cout << i << ":(c: " << gcGenerations[i]*100/zones;
         if (gcGenerations[i] > 0) {
            cout << ", f: " << std::round(gcGenerationValid[i]*100/gcGenerations[i]/pagesPerZone);
         } else {
            cout << ", f: 0";
         }
         if (gcGenerationValidMin[i] != std::numeric_limits<uint64_t>::max()) {
            if (gcGenerationValidMin[i] > pagesPerZone) raise(SIGINT);
            cout << ", m: " << gcGenerationValidMin[i]*100/pagesPerZone;
         } else {
            cout << ", m: x";
         }
         cout <<  "), ";
      }
      cout << "]" << endl;
      gcedNormalBlock = 0;
      gcedColdBlock = 0;
   }

   void printBlocksStats() {
      cout << "BlockStats: " << endl;
      std::vector<long> ages;
      for (auto& b: _blocks) {
         ages.emplace_back(b.gcAge);
      }
      sort(ages.begin(), ages.end());
      long min = *std::min_element(ages.begin(), ages.end());
      cout << "age: ";
      for (auto& a: ages) {
         cout << (a-min) << " ";
      }
      cout << endl;
      cout << "gcGen: ";
      for (auto& b: _blocks) {
         cout << b.gcGeneration << " ";
      }
      cout << endl;
      cout << "writtenByGC: ";
      for (auto& b: _blocks) {
         cout << b.writtenByGc << " ";
      }
      cout << endl;
      cout << "ValidCnt: ";
      for (auto& b: _blocks) {
         cout << pagesPerZone - b.validCnt() << " ";
      }
      cout << endl;
      cout << "Groups: ";
      for (auto& b: _blocks) {
         cout << b.group << " ";
      }
      cout << endl;
   }

   void writeStatsFile(std::string prefix) {
      /*
      auto writeToFile = [&](string filename, std::vector<uint64_t>& vec){
         cout << "write: " << filename << endl;
         std::ofstream myfile;
         myfile.open (filename);
         myfile << "id" << "," << "cnt"<< "\n";
         string s;
         for (uint64_t i = 0; i < vec.size(); i++) {
            if (vec[i] > 0) {
               s = std::to_string(i) + "," + std::to_string(vec[i]) + "\n";
               myfile.write(s.c_str(), s.size());
            }
         }
         myfile.close();
      };
      writeToFile(prefix + "zonedist.csv", _blocks, [&](uint64_t id) { return _blocks[id].validCnt(); });
      writeToFile(prefix + "updates.csv", _mappingUpdatedCnt);
      writeToFile(prefix + "updatesgc.csv", _mappingUpdatedGC);
       */
   }
};

