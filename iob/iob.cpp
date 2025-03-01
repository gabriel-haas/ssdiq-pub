// -------------------------------------------------------------------------------------
#include "PageState.hpp"
#include "PatternGen.hpp"
#include "RequestGenerator.hpp"
#include "../shared/Env.hpp"
#include "Time.hpp"
#include "ThreadBase.hpp"
#include "RequestGenerator.hpp"
#include "io/IoInterface.hpp"

#include <cstdint>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <stdlib.h>
#include <iomanip>
#include <mutex>

static void initializeSSDIfNecessary(mean::FileState& fileState, long maxPage, long bufSize, std::string init, int iodepth) {
   using namespace mean;
   // check if necessary
   IoChannel& ioChannel = IoInterface::instance().getIoChannel(0);

   int initBufSize = 512*1024; // FIXME: this might not work with SPDK on some SSDs, check for max transfer size and use that.
   char* buf = (char*)IoInterface::instance().allocIoMemoryChecked(initBufSize + 512, 512);
   memset(buf, 0, initBufSize);

   long iniOps = maxPage*bufSize / initBufSize +1; // +1 because you need to round up 
   bool forceInit = init == "yes";
   bool autoInit = init == "auto";
   bool disableCheck = init == "disable" || init == "no";

   int iniDoneCheck = true;
   if (!forceInit && !disableCheck) {
      // just a heurisitc: check first and last page
      // TODO: refactor: move this to FileState?
      auto offset = 0 * bufSize;
      ioChannel.pushBlocking(IoRequestType::Read, buf, offset, bufSize);
      const bool check1 = fileState.checkBufferNoThrow(buf, offset, bufSize);
      std::cout << "check 0: " << check1 << std::endl;
      iniDoneCheck &= check1;
      fileState.resetBufferChecks(buf, bufSize);
      auto offsetLast = maxPage * bufSize;
      ioChannel.pushBlocking(IoRequestType::Read, buf, offsetLast, bufSize);
      const bool check2 = fileState.checkBufferNoThrow(buf, offsetLast, bufSize);
      std::cout << "check " << offsetLast << ": " << check2 << std::endl;
      iniDoneCheck &= check2;
      std::cout << "init check: " << (iniDoneCheck ? "probably already done" : "not done yet") << std::endl;
      if (!autoInit && !iniDoneCheck && !disableCheck) {
         throw std::logic_error("Init not done yet on " + IoInterface::instance().getDeviceInfo().names() + ". If you know what you are doing use INIT=disable or INIT=auto / INIT=yes to force SSD initialization.");
      }
   }
   if (!disableCheck) {
      if (forceInit || !iniDoneCheck ) {
         auto start = getSeconds();
         std::cout << ">> init: " << std::endl << std::flush;
         std::cout << "init "<< iniOps*initBufSize/1024/1024/1024 << " GB iniOps: " << iniOps<< std::endl << std::flush;
         JobOptions initOptions;
         initOptions.name = "init";
         initOptions.bs = initBufSize;
         initOptions.filesize = iniOps*initBufSize; // make sure that maxPage * 4kB are at least written
         initOptions.io_size = iniOps*initBufSize;
         initOptions.writePercent = 1;
         initOptions.iodepth = iodepth;
         initOptions.printEverySecond = false;
         atomic<long> time = 0;
         iob::PatternGen::Options pgOptions;
         pgOptions.logicalPages = initOptions.filesize / initOptions.bs;
         pgOptions.pageSize = initOptions.bs;
         pgOptions.patternString ="sequential";
         iob::PatternGen patternGen(pgOptions);
         RequestGenerator init("", initOptions, IoInterface::instance().getIoChannel(0), 0, time, patternGen, fileState);
         init.runIo();
         std::cout << std::endl;
         auto duration = getSeconds() - start;
         std::cout << "init done: " << iniOps*initBufSize/duration/MEBI << "MiB/s ops: " << iniOps << " time: " << duration << std::endl << std::flush;
      }
      // check again
      fileState.resetBufferChecks(buf, bufSize);
      ioChannel.pushBlocking(IoRequestType::Read, buf, 0 * bufSize, bufSize);
      fileState.checkBuffer(buf, 0, bufSize);
      fileState.resetBufferChecks(buf, bufSize);
      ioChannel.pushBlocking(IoRequestType::Read, buf, maxPage*bufSize, bufSize);
      fileState.checkBuffer(buf, maxPage*bufSize, bufSize);
      // check all 
   }
   IoInterface::instance().freeIoMemory(buf);
}

class RequestGeneratorThread : public mean::ThreadBase {
public:
   std::atomic<bool> done = false;
   mean::RequestGenerator gen;
   RequestGeneratorThread(mean::JobOptions jobOptions, int thr, atomic<long>& time, iob::PatternGen& patternGen, mean::FileState& fileState)
      : ThreadBase("gent", thr), 
      gen(std::to_string(thr), jobOptions, mean::IoInterface::instance().getIoChannel(thr), thr, time, patternGen, fileState) {
         //setCpuAffinityBeforeStart(thr);
   }
   int process() override {
      gen.runIo();
      done = true;
      return 0;
   }
   bool isDone() {
      return done;
   }
   void stop() {
      gen.stopIo();
   }
};

int main(int , char** ) {
   using namespace mean;
   std::cout << "start" << std::endl;
   std::string filename = getEnvRequired("FILENAME");
   std::string ioEngine = getEnv("IOENGINE", "io_uring");
   const int threads = getEnv("THREADS",1);
   const int runtimeLimit = getEnv("RUNTIME", 0);
   long bufSize = getBytesFromString(getEnv("BS", "4K"));
   if (bufSize % 512 != 0) { throw std::logic_error("BS is not a multiple of 512"); }
   if (bufSize % 4096 != 0) { std::cout << "BS is not a multiple of 4096. Are you sure that is what you want?" << std::endl; }
   double writePercent = getEnv("RW", 0.0);
   const bool latencyThread = getEnv("LATENCYTHREAD", 0); // one of the threads run 10k IOPS of the workload to measures latency.

   IoOptions ioOptions(ioEngine, filename);
   ioOptions.iodepth = getEnv("IO_DEPTH", 128); 
   ioOptions.channelCount = threads;
   ioOptions.ioUringPollMode = getEnv("IOUPOLL", 0); // keep default off, as queues must be set in kernel parameters
   ioOptions.ioUringNVMePassthrough = getEnv("IOUPT", 0);

   // filesize
   IoInterface::initInstance(ioOptions);
   string filesizeStr = getEnv("FILESIZE", "");
   const long filesize = (filesizeStr.size() == 0) ? IoInterface::instance().storageSize() : getBytesFromString(filesizeStr);

   // iosize
   double ioSize = -1;
   if (getEnv("IO_SIZE", "").size() > 0) {
      ioSize = getBytesFromString(getEnv("IO_SIZE", ""));
   } else {
      double ioSizePercentFile = getEnv("IO_SIZE_PERCENT_FILE", 1.0);
      std::cout << "asdfafds" << ioSizePercentFile << std::endl;
      ioSize = filesize * ioSizePercentFile;
   }
   std::cout << "iosize: " << ioSize << std::endl;
   long opsPerThread = ioSize / bufSize / threads;

   //fill
   const double fill = getEnv("FILL", 1);
   long maxUsedFilesSize = getEnv("FILL", "").size() > 0 ? filesize*fill : filesize;
   ensure(maxUsedFilesSize <= filesize);
   if ((maxUsedFilesSize % (512*1024)) > 0) {
      maxUsedFilesSize = maxUsedFilesSize - (maxUsedFilesSize % (512*1024)); // make sure that maxUsedFilesSize is dividable by 512K 
   }
   // TODO there is a bug with init and blocksize = 512K
   std::string init = getEnv("INIT", "no");
   bool crc = getEnv("CRC", true);
   bool deepCheck = getEnv("DEEP_CHECK", false);
   bool randomData = getEnv("RANDOM_DATA", true);

   // TODO: refactor remove -1 ; rename pageCount , fix +1/-1
   long maxPage = maxUsedFilesSize/bufSize - 1;

   if (runtimeLimit > 0) {
      ioSize = -1;
   }

   std::cout << "FILENAME: " << filename << std::endl;
   std::cout << "BS: " << bufSize << std::endl;
   std::cout << "FILESIZE: " << filesize << " use: " << maxUsedFilesSize << " (FILL: " <<(getEnv("FILL", "1")) << ") in pages: " << maxPage << std::endl;
   std::cout << "IO_SIZE: " << opsPerThread*bufSize/(float)GIBI << "GiB pages: " << opsPerThread << " per thread" << std::endl;
   std::cout << "INIT: " << init << std::endl;
   std::cout << "IO_DEPTH: " << ioOptions.iodepth << std::endl;
   std::cout << "IOENGING: " << ioEngine << std::endl;

   mean::FileState fileState{(maxPage+1)*bufSize, crc, deepCheck, randomData};
   initializeSSDIfNecessary(fileState, maxPage, bufSize, init, ioOptions.iodepth);

   if (ioSize == 0) {
      return 0;
   }

   //unanlignedBench(filesize, bufSize, ops, 0);
   JobOptions jobOptions;

   string prefix = getEnv("PREFIX", "p");
   jobOptions.statsPrefix = prefix;

   jobOptions.breakEvery = getEnv("BREAK_EVERY", 0); // seconds
   jobOptions.breakFor = getEnv("BREAK_FOR", 0);

   jobOptions.filesize = maxUsedFilesSize;
   std::cout << "actual used filesize = " << jobOptions.filesize << std::endl;
   jobOptions.bs = bufSize;

   auto pgOptions = iob::PatternGen::loadOptionsFromEnv(jobOptions.filesize / jobOptions.bs, jobOptions.bs);
   iob::PatternGen::printPatternHistorgram(pgOptions);
   iob::PatternGen patternGen(pgOptions);

   jobOptions.disableChecks = init == "disable";
   jobOptions.filename = filename;
   jobOptions.iodepth = ioOptions.iodepth;
   jobOptions.io_alignment = bufSize;
   jobOptions.io_size = ioSize / threads;
   jobOptions.writePercent = writePercent;
   jobOptions.threads = threads;
   jobOptions.printEverySecond = false;
   jobOptions.fdatasync = 0;
   jobOptions.totalRate = getEnv("RATE", 0);
   jobOptions.rateLimit = jobOptions.totalRate / threads;
   jobOptions.exponentialRate = getEnv("EXPRATE", true);

   jobOptions.logHash = getTimeStampStr();
   std::ofstream dump;
   dump.open("iob-dump-"+prefix+".csv", std::ios_base::app);
   IoTrace::IoStat::dumpIoStatHeader(dump, "iodepth, bs, io_alignment,");
   dump << std::endl;

   std::cout << jobOptions.print();

   string iobLogFilename = "iob-log-"+prefix+".csv";
   bool iobLogExists = false;
   {
      std::ifstream iobLogExistsFS(iobLogFilename);
      iobLogExists = iobLogExistsFS.good();
   }
   std::ofstream iobLog;
   iobLog.open(iobLogFilename, std::ios_base::app);

   // start threads
   atomic<long> time = 0;
   std::vector<std::unique_ptr<RequestGeneratorThread>> threadVec;
   if (latencyThread) {
      auto propOptions = jobOptions;
      propOptions.name = "gen 0";
      propOptions.rateLimit = 10000;
      propOptions.exponentialRate = 0;
      propOptions.writePercent = 0.5;
      threadVec.emplace_back(std::move(std::make_unique<RequestGeneratorThread>(propOptions, 0, time, patternGen, fileState)));
      for (int thr = 1; thr < threads; thr++) {
         jobOptions.name = "gen " + std::to_string(thr);
         jobOptions.rateLimit = (jobOptions.totalRate - ((double)propOptions.rateLimit*propOptions.writePercent)) / (threads-1);
         threadVec.emplace_back(std::move(std::make_unique<RequestGeneratorThread>(jobOptions, thr, time, patternGen, fileState)));
      }
   } else {
      for (int thr = 0; thr < threads; thr++) {
         jobOptions.name = "gen " + std::to_string(thr);
         jobOptions.rateLimit = jobOptions.totalRate / threads;
         threadVec.emplace_back(std::move(std::make_unique<RequestGeneratorThread>(jobOptions, thr, time, patternGen, fileState)));
      }
   }
   std::cout << "run" << endl;
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
   for (auto& t: threadVec) {
      t->start();
   }

   long maxRead = 0;
   //if (runtimeLimit > 0) {
   cout << "runtime: " << runtimeLimit << " s" << endl;
   auto start = getSeconds();
   //std::this_thread::sleep_for(std::chrono::seconds(1));
   long lastOCPUpdateTime = -1; 
   long prevPhyWrites = -1;
   long prevPhyReads = -1;
   long prevHostWrites = -1;
   while (true) {
      auto now = getSeconds();
      NvmeLog nvmeLog;
      nvmeLog.loadOCPSmartLog();
      
      long sumReads = 0;
      long sumWrites = 0;
      long sumReadsPS = 0;
      long sumWritesPS = 0;
      /*
      for (int i = 0; i < threads; i++) {
         //IoInterface::instance().getIoChannel(i).printCounters(std::cout);
         //std::cout << std::endl << std::flush;
      }
      */
      for (int i = 0; i < threads; i++) {
         auto& thr = threadVec[i];
         int s = thr->gen.stats.seconds;
         if (s > 0) {
            sumReads += thr->gen.stats.reads;
            sumWrites += thr->gen.stats.writes;
            sumReadsPS += thr->gen.stats.readsPerSecond.at(s-1);
            sumWritesPS += thr->gen.stats.writesPerSecond.at(s-1);
            //std::cout << s << " r" << i << ": " << thr->gen.stats.readsPerSecond[s-1]/1000 << "k ";
            //std::cout << s << " w" << i << ": " << thr->gen.stats.writesPerSecond[s-1]/1000 << "k ";
         }
      }
      //std::cout << " total: r: "<< sumRead/1000 << "k " << " w: " << sumWrites << "k " << std::endl << std::flush;
      //maxRead = std::max(maxRead, sumRead);

      if (time == 0) {
         std::stringstream header;
         header << "stat,type,prefix,";
         header << "hash,";
         header << "time,";
         header << "exacttime,";
         header << "device,filesizeGib,fill,usedfilesize,pattern,patternDetails,rate,expRate,writepercent,";
         header << "bs,";
         header << "writesTotal,readsTotal,";
         header << "writeIOPS,readIOPS,";
         header << "writesMBs,readsMBs,";
         header << "physicalMediaUnitsWrittenBytes, physicalMediaUnitsReadBytes,";
         header << "phyWriteMBs, phyReadMBs,";
         header << "percentFreeBlocks,";
         header << "softECCError,unalignedIO,maxUserDataEraseCount,minUserDataEraseCount,currentThrottlingStatus,";
         header << "wa";
         if (!iobLogExists) {
            iobLog << header.str() << endl;
            string s = "echo \"prefix,time,hash,type,data\" >> iob-smart-"+prefix+".csv";
            int sys = system(s.c_str());
            assert(sys == 0);
         }
         cout << header.str() << endl;
      }
      std::stringstream ss;
      ss << "keep,ssd," << prefix;
      ss << "," << jobOptions.logHash;
      ss << "," << time;
      ss << "," << (now - start);
      ss << "," << filename << "," << filesize/GIBI << "," << fill << "," << jobOptions.filesize*1.0/GIBI << "," << patternGen.options.patternString << ",\"" << patternGen.patternDetails() << "\"";
      ss << "," << jobOptions.totalRate; 
      ss << "," << jobOptions.exponentialRate; 
      ss << "," << jobOptions.writePercent; 
      ss << "," << jobOptions.bs;
      ss << "," << sumWrites << "," << sumReads;
      ss << "," << sumWritesPS << "," << sumReadsPS;
      ss << "," << sumWritesPS*jobOptions.bs/MEBI << "," << sumReadsPS*jobOptions.bs/MEBI;
      long currentTotPhyWrites = nvmeLog.physicalMediaUnitsWrittenBytes();
      long currentTotPhyReads = nvmeLog.physicalMediaUnitsReadBytes();
      long thisSecondPhyWrites = 0;
      long thisSecondPhyReads = 0;
      double thisSecondWA = 0;
      if (currentTotPhyWrites != prevPhyWrites) { // ocp was updated, => immediately on kioxia, samsung only every couple of minutes
         if (prevPhyWrites >= 0) {
            thisSecondPhyWrites = (currentTotPhyWrites - prevPhyWrites) / (time - lastOCPUpdateTime);
            thisSecondPhyReads = (currentTotPhyReads - prevPhyReads) / (time - lastOCPUpdateTime);
            thisSecondWA =  (currentTotPhyWrites - prevPhyWrites)*1.0 / ((sumWrites - prevHostWrites)*jobOptions.bs);
         }
         // update prev values
         lastOCPUpdateTime = time;
         prevPhyWrites = currentTotPhyWrites;
         prevPhyReads = currentTotPhyReads;
         prevHostWrites = sumWrites;
      }
      ss << "," << currentTotPhyWrites;
      ss << "," << currentTotPhyReads;
      ss << "," << thisSecondPhyWrites/MEBI;
      ss << "," << thisSecondPhyReads/MEBI;
      ss << "," << (int)nvmeLog.percentFreeBlocks();
      ss << "," << nvmeLog.softECCError() << "," << nvmeLog.unalignedIO() << "," << nvmeLog.maxUserDataEraseCount();
      ss << "," << nvmeLog.minUserDataEraseCount() << "," << (int)nvmeLog.currentThrottlingStatus();
      ss << "," << thisSecondWA; 
      iobLog << ss.str() << endl;
      cout << ss.str() << endl;

      string smartLine = prefix+","+to_string(time)+","+jobOptions.logHash+"";
      string sysSmart = 
             "echo -n \""+smartLine+",smart,\"                                >> iob-smart-"+prefix+".csv" +
             " ; sudo nvme smart-log         "+filename+" --output-format=json | tr -d '\\n'      >> iob-smart-"+prefix+".csv" +
             " ; echo ""                                                                          >> iob-smart-"+prefix+".csv";
      string sysOCP = 
             "echo -n \""+smartLine+",ocp,\"                                 >> iob-smart-"+prefix+".csv" +
             " ; sudo nvme ocp smart-add-log "+filename+" --output-format=json  2> /dev/null | tr -d '\\n'      >> iob-smart-"+prefix+".csv" +
             " ; echo ""                                                                          >> iob-smart-"+prefix+".csv";
      int sys = system(sysSmart.c_str());
      assert(sys == 0);
      sys = system(sysOCP.c_str());
      assert(sys == 0);
      //cout << sys << endl;

      now = getSeconds();
      bool oneDone = false;
      for (auto& t: threadVec) {
         oneDone |= t->isDone();
      }
      if (oneDone) { 
         std::cout << "one done, break;" << std::endl;
         break; // when the first one ist done, stop all
      }
      if (runtimeLimit > 0 && now - start > runtimeLimit) {
         break;
      }
      std::this_thread::sleep_for(std::chrono::microseconds((long)((time + 1 - (now - start))*1e6)));
      time++;
   }
   for (auto& t: threadVec) {
      t->stop();
   }

   u64 reads = 0;
   u64 writes = 0;
   u64 r50p = 0;
   u64 r99p = 0;
   u64 r99p9 = 0;
   u64 w50p = 0;
   u64 w99p = 0;
   u64 w99p9 = 0;
   u64 rTotalTime = 0;
   u64 wTotalTime = 0;
   float totalTime = 0;
   for (auto& t: threadVec) {
      t->join();
   }
   std::ofstream patDump;
   patDump.open("iob-patdump-"+prefix+".csv", std::ios_base::app);
   RequestGenerator::dumpPatternAccessHeader(patDump, "");
   std::array<long, 100> accessHist;
   accessHist.fill(0);
   const long sampleCount = 200;
   std::vector<long> sampleLocs(sampleCount);
   std::vector<long> accesses(sampleCount);
   std::random_device randDev;
   std::mt19937_64 mersene{randDev()};
   std::uniform_int_distribution<uint64_t> rndLoc{0, jobOptions.totalMinusOffsetBlocks()};
   for (int i = 0; i < sampleCount; i++) {
      sampleLocs[i] = rndLoc(mersene);
   }
   std::sort(sampleLocs.begin(), sampleLocs.end());
   for (size_t i = 0; i < threadVec.size(); i++) {
      cout << "thread " << i << endl;
      auto& t = threadVec[i];
      reads += t->gen.stats.reads;
      writes += t->gen.stats.writes;
      totalTime += t->gen.stats.time;
      r50p += t->gen.stats.readHist.getPercentile(50);
      r99p += t->gen.stats.readHist.getPercentile(99);
      r99p9 += t->gen.stats.readHist.getPercentile(99.9);
      w50p += t->gen.stats.writeHist.getPercentile(50);
      w99p += t->gen.stats.writeHist.getPercentile(99);
      w99p9 += t->gen.stats.writeHist.getPercentile(99.9);
      rTotalTime += t->gen.stats.readTotalTime;
      wTotalTime += t->gen.stats.writeTotalTime;
      t->gen.ioTrace.dumpIoTrace(dump, std::to_string(jobOptions.iodepth) + "," + std::to_string(jobOptions.bs) + "," + std::to_string(jobOptions.io_alignment) + ",");
      t->gen.aggregatePatternAccess(accessHist);
      cout << endl;
      t->gen.samplePatternAccess(sampleLocs, accesses);
      //t->gen.dumpPatternAccess("patterDump:: ", cout);
   }
   cout << "sample:" << endl;
   for (auto s: sampleLocs) {
      cout << s << ",";
   }
   cout << endl;
   for (auto a: accesses) {
      cout << a << ",";
   }
   cout << endl;
   cout << "hist:" << endl;
   for (auto a: accessHist) {
      cout << a << ",";
   }
   cout << endl;

   totalTime /= threads;
   r50p /= threads; r99p /= threads; r99p9 /= threads;
   w50p /= threads; w99p /= threads; w99p9 /= threads;
   dump << "filesize,fill,usedFileSize,io_size,filename,bs,rw,threads,iodepth,reads,writes,rmb,wmb,ravg,wavg,r50p,r99p,r99p9,w50p,w99p,w99p9"  << std::endl;
   dump << filesize << "," << fill << "," << maxUsedFilesSize << "," << ioSize << ","; 
   dump << "\""<< filename << "\"," << bufSize << "," << writePercent << "," << threads << "," << jobOptions.iodepth << ",";
   dump << reads/totalTime << "," << writes/totalTime << "," << reads/totalTime*bufSize/MEBI << "," << writes/totalTime *bufSize/MEBI<< ",";
   dump <<  std::setprecision(6) << (float)reads/rTotalTime*1e6 << "," << (float)writes/wTotalTime*1e6 << "," << r50p << "," << r99p << ","<< r99p9 << ","<< w50p << ","<< w99p << ","<< w99p9 << "," << std::endl;
   dump.close();

   std::cout << "summary ";
   std::cout << std::setprecision(4);
   std::cout <<  "total: " << (reads+writes)/totalTime/MEGA << " MIOPS"; 
   std::cout << " (reads: " << reads/totalTime/MEGA << " write: " << writes/totalTime/MEGA << ")";
   std::cout << " total: " << (reads+writes)/totalTime*bufSize/GIBI << " GiB/s";
   std::cout << " (reads: " << reads/totalTime*bufSize/GIBI << " write: " << writes/totalTime *bufSize/GIBI << ")";
   if (maxRead > 0) {std::cout << " max read: " << maxRead/1e6 << "M"; };
   std::cout << std::endl;

   std::this_thread::sleep_for(std::chrono::seconds(1));
   std::cout << "fin" << std::endl;

   return 0;
}
