//
// Created by Gabriel Haas on 02.07.24.
//
#pragma once

#include "SSD.hpp"

#include <fstream>
#include <csignal>
#include <list>
#include <random>
#include <algorithm>

class GreedyGC {
   SSD& ssd;
   uint64_t currentBlock;
   int64_t currentGCBlock = -1;
   // k - greedy
   int k;
   bool simpleTwoR;
   std::random_device rd;
   std::mt19937_64 gen{rd()};
   std::uniform_int_distribution<uint64_t> rndBlockDist;
   std::list<uint64_t> freeBlocks;
public:
   GreedyGC(SSD& ssd, int k = 0, bool twoR = false) : ssd(ssd), k(k), rndBlockDist(0, ssd.zones-1), simpleTwoR(twoR) {
      for (uint64_t z=0; z < ssd.zones; z++) {
         freeBlocks.push_back(z);
      }
      currentBlock = freeBlocks.front();
      freeBlocks.pop_front();
   }
   string name() {
      if (simpleTwoR) {
         return "greedy-s2r";
      }
      if (k==0) {
         return "greedy";
      }
      return "greedy-k" + std::to_string(k);
   }
   void writePage(uint64_t pageId) {
      if (!ssd.blocks()[currentBlock].canWrite()) {
         if (freeBlocks.empty()) {
            performGC();
         }
         currentBlock = freeBlocks.front();
         freeBlocks.pop_front();
         ensure(ssd.blocks()[currentBlock].canWrite());
      }
      ssd.writePage(pageId, currentBlock);
   }
   uint64_t singleGreedy() {
      uint64_t minIdx;
      uint64_t minCnt = std::numeric_limits<uint64_t>::max();
      for (uint64_t i = 0; i < ssd.zones; i++) {
         const SSD::Block& block = ssd.blocks()[i];
         if (block.validCnt() < minCnt && block.fullyWritten()) { // only use full blocks for gc
            minIdx = i;
            minCnt = block.validCnt();
         }
      }
      return minIdx;
   }
   void performGC() {
      if (!simpleTwoR) {
         uint64_t victimBlockIdx;
         if (k == 0) {
            victimBlockIdx = singleGreedy();
            //writezone = random() % zones;
         } else {
            // perform k-greedy
            uint64_t minIdx;
            uint64_t minCnt = std::numeric_limits<uint64_t>::max();
            int i = 0;
            do {
               uint64_t idx = rndBlockDist(gen);
               if (ssd.blocks().at(idx).validCnt() < minCnt) {
                  minIdx = idx;
                  minCnt = ssd.blocks()[idx].validCnt();
               }
               i++;
            } while (i < k);
            victimBlockIdx = minIdx;
         }
         ssd.compactBlock(victimBlockIdx);
         freeBlocks.push_back(victimBlockIdx);
      } else {
         // compact until free block using singleGreedy
         auto [freeBlock, gcBlock] = ssd.compactUntilFreeBlock(currentGCBlock, [&]() { return singleGreedy(); });
         assert(ssd.blocks()[freeBlock].isErased());
         currentGCBlock = gcBlock;
         freeBlocks.push_back(freeBlock);
      }
   }
   void stats() {
      std::cout << "Greedy stats" << std::endl;
   }
   void resetStats() {}
};
