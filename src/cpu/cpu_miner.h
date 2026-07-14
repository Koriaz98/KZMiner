#pragma once
#include <atomic>
#include <cstdint>

class MiningSource;

class CPUMiner
{
public:
    CPUMiner(MiningSource* source, int threads, int workerOffset, int totalWorkers);

    void launchWorkers();
    int getThreadCount() const { return threads; }
    uint64_t getHashes() const { return hashes.load(); }
    uint64_t getShares() const { return sharesFound.load(); }

private:
    MiningSource* source_;
    int threads;
    int workerOffset_;
    int totalWorkers_;
    std::atomic<uint64_t> hashes{0};
    std::atomic<uint64_t> sharesFound{0};
    void worker(int cpuId);
};
