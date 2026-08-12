#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include <thread>

class MiningSource;
class Algorithm;

class GpuMiner
{
public:
    GpuMiner(MiningSource* source, Algorithm* algorithm, int intensity, int workerOffset, int totalWorkers);
    ~GpuMiner();

    void launchWorkers();
    // Arret propre : pose stop_ puis JOINT tous les workers. Chaque worker
    // sort ENTRE deux batches (pas de kernel Argon2id en vol), detruit son
    // GpuHasher -> cudaFree propre, pas de contexte CUDA orphelin. A
    // appeler AVANT de detruire le MiningSource (les workers l'utilisent).
    // Idempotent.
    void stop();
    int getDeviceCount() const;
    uint64_t getHashes() const { return hashes.load(); }
    uint64_t getDeviceHashes(int deviceIndex) const;

private:
    MiningSource* source_;
    Algorithm* algorithm_;
    int intensity_;
    int workerOffset_;
    int totalWorkers_;
    std::atomic<uint64_t> hashes{0};
    std::unique_ptr<std::atomic<uint64_t>[]> perDeviceHashes_;
    int deviceCount_ = 0;
    std::atomic<bool> stop_{false};
    std::vector<std::thread> workers_;

    void worker(int deviceIndex, int globalId);
    void supervisedWorker(int deviceIndex, int globalId);
    static double intensityFraction(int intensity);
};
