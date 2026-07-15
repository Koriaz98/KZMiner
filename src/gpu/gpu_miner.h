#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class MiningSource;

class GpuMiner
{
public:
    GpuMiner(MiningSource* source, int intensity, int workerOffset, int totalWorkers);

    void launchWorkers();
    int getDeviceCount() const;
    uint64_t getHashes() const { return hashes.load(); }
    uint64_t getShares() const { return sharesFound.load(); }
    uint64_t getDeviceHashes(int deviceIndex) const;

private:
    MiningSource* source_;
    int intensity_;
    int workerOffset_;
    int totalWorkers_;
    std::atomic<uint64_t> hashes{0};
    std::atomic<uint64_t> sharesFound{0};
    std::unique_ptr<std::atomic<uint64_t>[]> perDeviceHashes_;
    int deviceCount_ = 0;

    void worker(int deviceIndex, int globalId);
    void supervisedWorker(int deviceIndex, int globalId);
    static double intensityFraction(int intensity);
};
