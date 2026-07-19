#include "gpu_miner.h"
#include "../network/mining_source.h"
#include "../console_output.h"

#include "argon2-gpu-common/argon2params.h"
#include "argon2-cuda/globalcontext.h"
#include "argon2-cuda/programcontext.h"
#include "argon2-cuda/processingunit.h"
#include "argon2-cuda/cudaexception.h"

#include <cuda_runtime.h>
#include <sstream>
#include <thread>
#include <vector>
#include <chrono>
#include <cstring>
#include <exception>

namespace
{
    const uint8_t kSalt[] = { 'B','T','C','0','9','/','p','o','w','/','v','1' };
    constexpr size_t kVramReserveBytes = 512ULL * 1024 * 1024;
}

double GpuMiner::intensityFraction(int intensity)
{
    switch(intensity)
    {
        case 1: return 0.15;
        case 2: return 0.30;
        case 3: return 0.50;
        case 4: return 0.70;
        case 5: return 0.97;
        default: return 0.50;
    }
}

GpuMiner::GpuMiner(MiningSource* source, int intensity, int workerOffset, int totalWorkers)
: source_(source), intensity_(intensity), workerOffset_(workerOffset), totalWorkers_(totalWorkers)
{
}

int GpuMiner::getDeviceCount() const
{
    argon2::cuda::GlobalContext global;
    return static_cast<int>(global.getAllDevices().size());
}

uint64_t GpuMiner::getDeviceHashes(int deviceIndex) const
{
    if(!perDeviceHashes_ || deviceIndex < 0 || deviceIndex >= deviceCount_)
    {
        return 0;
    }
    return perDeviceHashes_[deviceIndex].load();
}

void GpuMiner::worker(int deviceIndex, int globalId)
{
    try
    {
        pushLogLine("GPU " + std::to_string(deviceIndex) + ": initializing...");

        cudaError_t setDevErr = cudaSetDevice(deviceIndex);
        if(setDevErr != cudaSuccess)
        {
            pushLogLine("GPU " + std::to_string(deviceIndex) + ": cudaSetDevice failed ("
                + std::string(cudaGetErrorString(setDevErr)) + ")");
            return;
        }

        MiningJob job;
        while(true)
        {
            job = source_->getJob();
            if(job.valid && job.header.size() == 88 && job.target.size() == 32) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        size_t freeBytes = 0, totalBytes = 0;
        cudaMemGetInfo(&freeBytes, &totalBytes);

        // Si la VRAM libre semble anormalement basse (moins de la moitie
        // du total), un ancien process peut etre en train de liberer sa
        // memoire cote driver sans que ce soit encore visible. On
        // reessaie quelques fois avant de figer un batch-size trop
        // petit pour toute la duree de vie du thread.
        for(int retry = 0; retry < 5 && totalBytes > 0 && freeBytes < totalBytes / 2; retry++)
        {
            std::ostringstream oss;
            oss << "GPU " << deviceIndex << ": only "
                << (freeBytes / (1024*1024)) << " MiB free out of "
                << (totalBytes / (1024*1024)) << " MiB total, retrying VRAM check in 3s ("
                << (retry + 1) << "/5)...";
            pushLogLine(oss.str());
            std::this_thread::sleep_for(std::chrono::seconds(3));
            cudaMemGetInfo(&freeBytes, &totalBytes);
        }

        size_t usableBytes = (freeBytes > kVramReserveBytes)
            ? (freeBytes - kVramReserveBytes) : 0;

        size_t perHashBytes = static_cast<size_t>(job.argon_mem_kib) * 1024;
        size_t batchSize = static_cast<size_t>(
            (usableBytes * intensityFraction(intensity_)) / perHashBytes
        );
        if(batchSize < 1) batchSize = 1;

        {
            std::ostringstream oss;
            oss << "GPU " << deviceIndex
                << ": " << (totalBytes / (1024*1024)) << " MiB total, "
                << (freeBytes / (1024*1024)) << " MiB free, "
                << "batch-size=" << batchSize
                << " (intensity " << intensity_ << "), "
                << "global worker id " << globalId << " / " << totalWorkers_;
            pushLogLine(oss.str());
        }

        argon2::cuda::GlobalContext global;
        auto &devices = global.getAllDevices();
        if(deviceIndex >= static_cast<int>(devices.size()))
        {
            pushLogLine("GPU " + std::to_string(deviceIndex) + ": index out of range");
            return;
        }
        const auto &device = devices[deviceIndex];

        argon2::cuda::ProgramContext pc(
            &global, { device },
            argon2::ARGON2_ID, argon2::ARGON2_VERSION_13
        );

        argon2::Argon2Params params(
            32,
            kSalt, sizeof(kSalt),
            nullptr, 0,
            nullptr, 0,
            job.argon_time, job.argon_mem_kib, 1
        );

        argon2::cuda::ProcessingUnit unit(
            &pc, &params, &device,
            batchSize, true, false
        );

        std::vector<uint8_t> lastHeader;
        uint64_t rangeStart = 0, rangeEnd = 0, nonce = 0;
        std::vector<uint8_t> buffer(88);
        std::vector<uint8_t> hashBuf(32);

        while(true)
        {
            job = source_->getJob();

            if(!job.valid || job.header.size() != 88 || job.target.size() != 32)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            if(job.header != lastHeader)
            {
                lastHeader = job.header;

                if(job.nonce_end != 0)
                {
                    uint64_t span = job.nonce_end - job.nonce_start;
                    uint64_t perWorker = totalWorkers_ > 0 ? span / static_cast<uint64_t>(totalWorkers_) : span;
                    rangeStart = job.nonce_start + static_cast<uint64_t>(globalId) * perWorker;
                    rangeEnd = (globalId == totalWorkers_ - 1)
                        ? job.nonce_end : rangeStart + perWorker;
                }
                else
                {
                    rangeStart = (static_cast<uint64_t>(globalId) << 56);
                    rangeEnd   = (static_cast<uint64_t>(globalId) + 1) << 56;
                }
                nonce = rangeStart;
            }

            for(size_t i = 0; i < batchSize; i++)
            {
                buffer = job.header;
                uint64_t n = nonce + i;
                for(int b = 0; b < 8; b++)
                {
                    buffer[buffer.size() - 8 + b] =
                        static_cast<uint8_t>((n >> (8 * b)) & 0xff);
                }
                unit.setPassword(i, buffer.data(), buffer.size());
            }

            unit.beginProcessing();
            unit.endProcessing();

            for(size_t i = 0; i < batchSize; i++)
            {
                unit.getHash(i, hashBuf.data());

                if(std::memcmp(hashBuf.data(), job.target.data(), 32) <= 0)
                {
                    sharesFound++;
                    // Soumettre avec le job_id du job REELLEMENT hashe
                    // (job, capture au debut de ce cycle) - PAS un job
                    // fraichement recupere ici, qui pourrait deja avoir
                    // change entre-temps et ne plus correspondre au
                    // calcul effectue, entrainant un rejet cote
                    // coordinateur/pool malgre un resultat valide.
                    source_->submitNonce(job.job_id, nonce + i, hashBuf, job.height, job.isDevFeeJob);
                }
            }

            hashes += batchSize;
            if(perDeviceHashes_ && deviceIndex < deviceCount_)
            {
                perDeviceHashes_[deviceIndex] += batchSize;
            }

            nonce += batchSize;
            if(nonce >= rangeEnd)
            {
                nonce = rangeStart;
            }
        }
    }
    catch(const argon2::cuda::CudaException &ex)
    {
        pushLogLine("GPU " + std::to_string(deviceIndex) + ": fatal CUDA error: " + ex.what());
    }
    catch(const std::exception &ex)
    {
        pushLogLine("GPU " + std::to_string(deviceIndex) + ": non-CUDA exception: " + ex.what());
    }
    catch(...)
    {
        pushLogLine("GPU " + std::to_string(deviceIndex) + ": unknown exception, thread stopped");
    }
}

void GpuMiner::launchWorkers()
{
    int deviceCount = getDeviceCount();

    if(deviceCount == 0)
    {
        pushLogLine("No CUDA GPU detected.");
        return;
    }

    deviceCount_ = deviceCount;
    perDeviceHashes_ = std::make_unique<std::atomic<uint64_t>[]>(deviceCount);
    for(int i = 0; i < deviceCount; i++)
    {
        perDeviceHashes_[i] = 0;
    }

    pushLogLine("GPUs detected: " + std::to_string(deviceCount));

    for(int d = 0; d < deviceCount; d++)
    {
        int globalId = workerOffset_ + d;
        std::thread(&GpuMiner::supervisedWorker, this, d, globalId).detach();
    }
}

void GpuMiner::supervisedWorker(int deviceIndex, int globalId)
{
    // worker() se termine si une exception fatale a ete attrapee en son
    // sein (voir ses blocs catch). Sans supervision, ce GPU resterait
    // definitivement hors service pour le reste de l'execution du
    // process. On le relance automatiquement apres un court delai,
    // plutot que d'abandonner ce GPU silencieusement.
    while(true)
    {
        worker(deviceIndex, globalId);

        pushLogLine("GPU " + std::to_string(deviceIndex) + ": worker thread stopped unexpectedly, restarting in 5s...");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}
