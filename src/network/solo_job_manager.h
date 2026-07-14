#pragma once
#include "mining_source.h"
#include "work_client.h"
#include <mutex>
#include <atomic>
#include <thread>

class SoloJobManager : public MiningSource
{
public:
    SoloJobManager(
        const std::string& poolUrl,
        const std::string& address,
        const std::string& worker
    );
    ~SoloJobManager() override;

    void start() override;
    MiningJob getJob() override;
    void submitNonce(
        const std::string& job_id,
        uint64_t nonce,
        const std::vector<uint8_t>& hash,
        uint64_t height
    ) override;

private:
    WorkClient client_;
    std::mutex mutex_;
    MiningJob current_;
    std::atomic<bool> running_{false};
    std::thread pollThread_;

    void pollLoop();
    void refreshWork();
};
