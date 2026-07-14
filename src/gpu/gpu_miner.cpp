#include "gpu_miner.h"
#include "../network/mining_source.h"

#include "argon2-gpu-common/argon2params.h"
#include "argon2-cuda/globalcontext.h"
#include "argon2-cuda/programcontext.h"
#include "argon2-cuda/processingunit.h"
#include "argon2-cuda/cudaexception.h"

#include <cuda_runtime.h>
#include <iostream>
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
        case 5: return 0.90;
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
        std::cout << "GPU " << deviceIndex << ": initializing..." << std::endl;
        cudaError_t setDevErr = cudaSetDevice(deviceIndex);
        if(setDevErr != cudaSuccess)
        {
            std::cerr << "GPU " << deviceIndex << ": cudaSetDevice failed ("
                       << cudaGetErrorString(setDevErr) << ")\n";
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

        size_t usableBytes = (freeBytes > kVramReserveBytes)
            ? (freeBytes - kVramReserveBytes) : 0;

        size_t perHashBytes = static_cast<size_t>(job.argon_mem_kib) * 1024;
        size_t batchSize = static_cast<size_t>(
            (usableBytes * intensityFraction(intensity_)) / perHashBytes
        );
        if(batchSize < 1) batchSize = 1;

        std::cout
            << "GPU " << deviceIndex
            << ": " << (totalBytes / (1024*1024)) << " MiB total, "
            << (freeBytes / (1024*1024)) << " MiB free, "
            << "batch-size=" << batchSize
            << " (intensity " << intensity_ << "), "
            << "global worker id " << globalId << " / " << totalWorkers_ << "\n";

        argon2::cuda::GlobalContext global;
        auto &devices = global.getAllDevices();
        if(deviceIndex >= static_cast<int>(devices.size()))
        {
            std::cerr << "GPU " << deviceIndex << ": index out of range\n";
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
                    MiningJob freshJob = source_->getJob();
                    source_->submitNonce(freshJob.job_id, nonce + i, hashBuf, job.height);
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
        std::cerr << "GPU " << deviceIndex << ": fatal CUDA error: " << ex.what() << "\n";
    }
    catch(const std::exception &ex)
    {
        std::cerr << "GPU " << deviceIndex << ": non-CUDA exception: " << ex.what() << "\n";
    }
    catch(...)
    {
        std::cerr << "GPU " << deviceIndex << ": unknown exception, thread stopped\n";
    }
}

void GpuMiner::launchWorkers()
{
    int deviceCount = getDeviceCount();

    if(deviceCount == 0)
    {
        std::cerr << "No CUDA GPU detected.\n";
        return;
    }

    deviceCount_ = deviceCount;
    perDeviceHashes_ = std::make_unique<std::atomic<uint64_t>[]>(deviceCount);
    for(int i = 0; i < deviceCount; i++)
    {
        perDeviceHashes_[i] = 0;
    }

    std::cout << "GPUs detected: " << deviceCount << "\n";

    for(int d = 0; d < deviceCount; d++)
    {
        int globalId = workerOffset_ + d;
        std::thread(&GpuMiner::worker, this, d, globalId).detach();
    }
}
