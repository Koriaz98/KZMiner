#include "solo_job_manager.h"
#include "../console_lock.h"
#include <iostream>
#include <chrono>
#include <cstdio>

namespace
{
    // MaxTargetBits mainnet officiel (core/params.go: 0x1f00ffff),
    // target de reference pour difficulty = 1, en 32 octets big-endian.
    const std::vector<uint8_t> kMaxTarget = {
        0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    double bytesToDouble(const std::vector<uint8_t>& bytes)
    {
        double result = 0.0;
        for(uint8_t b : bytes)
        {
            result = result * 256.0 + static_cast<double>(b);
        }
        return result;
    }

    double computeDifficulty(const std::vector<uint8_t>& target)
    {
        if(target.size() != 32) return 0.0;
        double t = bytesToDouble(target);
        if(t <= 0.0) return 0.0;
        return bytesToDouble(kMaxTarget) / t;
    }
}

SoloJobManager::SoloJobManager(
    const std::string& poolUrl,
    const std::string& address,
    const std::string& worker
)
: client_(poolUrl, address, worker)
{
}

SoloJobManager::~SoloJobManager()
{
    running_ = false;
    if(pollThread_.joinable())
    {
        pollThread_.join();
    }
}

void SoloJobManager::refreshWork()
{
    auto work = client_.requestWork();
    if(!work.has_value())
    {
        std::lock_guard<std::mutex> lock(consoleMutex());
        std::cerr << "SoloJobManager: failed to fetch work, will retry\n";
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    bool isNewJob = (work->job_id != current_.job_id);
    current_.valid         = true;
    current_.job_id         = work->job_id;
    current_.height         = work->height;
    current_.header         = work->header;
    current_.target         = work->target;
    current_.argon_mem_kib = work->argon_mem_kib;
    current_.argon_time     = work->argon_time;
    current_.difficulty     = computeDifficulty(work->target);
    current_.nonce_start    = 0;
    current_.nonce_end      = 0; // signal "mode solo, pas de plage imposee"

    if(isNewJob)
    {
        std::string headerPrefix;
        for(int i = 0; i < 8 && i < (int)current_.header.size(); i++)
        {
            char buf[3];
            snprintf(buf, sizeof(buf), "%02x", current_.header[i]);
            headerPrefix += buf;
        }
        std::lock_guard<std::mutex> consoleLock(consoleMutex());
        std::cout
            << "[solo] new work job_id=" << current_.job_id
            << " height=" << current_.height
            << " difficulty=" << current_.difficulty
            << " header_prefix=" << headerPrefix
            << "\n";
    }
}

void SoloJobManager::pollLoop()
{
    while(running_)
    {
        refreshWork();
        for(int i = 0; i < 100 && running_; i++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void SoloJobManager::start()
{
    running_ = true;
    for(int attempt = 0; attempt < 5; attempt++)
    {
        refreshWork();
        if(current_.valid) break;
        {
            std::lock_guard<std::mutex> lock(consoleMutex());
            std::cerr << "SoloJobManager: retrying initial work fetch...\n";
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    pollThread_ = std::thread(&SoloJobManager::pollLoop, this);
}

MiningJob SoloJobManager::getJob()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return current_;
}

void SoloJobManager::submitNonce(
    const std::string& job_id,
    uint64_t nonce,
    const std::vector<uint8_t>& /*hash*/,
    uint64_t height
)
{
    auto result = client_.submitNonce(job_id, nonce);

    std::lock_guard<std::mutex> lock(consoleMutex());
    if(result.ok)
    {
        std::cout
            << "\n*** BLOCK FOUND (solo) *** height=" << height
            << " block_id=" << result.block_id << "\n\n";
    }
    else
    {
        std::cout << "[solo] submit rejected: " << result.error_code << "\n";
    }
}
