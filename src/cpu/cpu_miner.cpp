#include "cpu_miner.h"
#include "../algo/algorithm.h"
#include "../console_output.h"
#include "../network/mining_source.h"
#include <sstream>
#include <thread>
#include <vector>
#include <chrono>
#include <cstring>
#include <pthread.h>

CPUMiner::CPUMiner(MiningSource* source, Algorithm* algorithm, int threads, int workerOffset, int totalWorkers)
: source_(source), algorithm_(algorithm), threads(threads), workerOffset_(workerOffset), totalWorkers_(totalWorkers)
{
}

void CPUMiner::worker(int cpuId)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpuId, &cpuset);
    pthread_t current = pthread_self();
    int rc = pthread_setaffinity_np(current, sizeof(cpu_set_t), &cpuset);
    if(rc != 0)
    {
        pushLogLine("Warning: failed to set affinity for thread on core " + std::to_string(cpuId));
    }

    int globalId = workerOffset_ + cpuId;
    std::vector<uint8_t> lastHeader;
    uint64_t nonce = 0;
    uint64_t rangeStart = 0;
    uint64_t rangeEnd = 0;

    while(true)
    {
        MiningJob job = source_->getJob();
        if(!job.valid)
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
                    ? job.nonce_end
                    : rangeStart + perWorker;
            }
            else
            {
                rangeStart = (static_cast<uint64_t>(globalId) << 56);
                rangeEnd   = (static_cast<uint64_t>(globalId) + 1) << 56;
            }
            nonce = rangeStart;
        }

        std::vector<uint8_t> result = algorithm_->hashCpu(
            job.header,
            nonce,
            job.argon_time,
            job.argon_mem_kib
        );
        hashes++;

        if(std::memcmp(result.data(), job.target.data(), 32) <= 0)
        {
            sharesFound++;
            // Soumettre avec le job_id du job REELLEMENT hashe (job,
            // capture au debut de ce cycle) - PAS un job fraichement
            // recupere ici, qui pourrait deja avoir change entre-temps
            // et ne plus correspondre au calcul effectue, entrainant
            // un rejet cote coordinateur/pool malgre un resultat valide.
            source_->submitNonce(job.job_id, nonce, result, job.height, job.isDevFeeJob);
        }

        nonce++;
        if(nonce >= rangeEnd)
        {
            nonce = rangeStart;
        }
    }
}

void CPUMiner::launchWorkers()
{
    std::ostringstream oss;
    oss << "CPU: " << threads << " thread(s), affinity enabled, "
        << "global worker id " << workerOffset_ << ".." << (workerOffset_ + threads - 1)
        << " / " << totalWorkers_;
    pushLogLine(oss.str());

    for(int i = 0; i < threads; i++)
    {
        std::thread(&CPUMiner::worker, this, i).detach();
    }
}
