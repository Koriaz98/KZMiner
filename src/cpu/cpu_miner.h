#pragma once
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

class MiningSource;
class Algorithm;

class CPUMiner
{
public:
    CPUMiner(MiningSource* source, Algorithm* algorithm, int threads, int workerOffset, int totalWorkers);
    ~CPUMiner();

    void launchWorkers();
    // Arret propre : pose stop_ puis JOINT tous les workers (attend leur
    // fin reelle). A appeler AVANT de detruire le MiningSource, sinon un
    // worker encore vivant pourrait appeler source->getJob() sur un objet
    // detruit (use-after-free). Idempotent.
    void stop();
    int getThreadCount() const { return threads; }
    uint64_t getHashes() const { return hashes.load(); }

private:
    MiningSource* source_;
    Algorithm* algorithm_;
    int threads;
    int workerOffset_;
    int totalWorkers_;
    std::atomic<uint64_t> hashes{0};
    std::atomic<bool> stop_{false};
    std::vector<std::thread> workers_;
    void worker(int cpuId);
};
