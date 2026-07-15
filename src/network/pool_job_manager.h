#pragma once
#include "mining_source.h"
#include "pool_client.h"
#include <thread>
#include <atomic>
#include <string>
#include <memory>

class PoolJobManager : public MiningSource
{
public:
    PoolJobManager(
        const std::string& host,
        int port,
        const std::string& wallet,
        const std::string& worker
    );
    ~PoolJobManager() override;

    void start() override;
    MiningJob getJob() override;
    void submitNonce(
        const std::string& job_id,
        uint64_t nonce,
        const std::vector<uint8_t>& hash,
        uint64_t height
    ) override;

    uint64_t getAcceptedCount() const override;
    uint64_t getRejectedCount() const override;

private:
    std::string host_;
    int port_;
    std::string wallet_;
    std::string worker_;

    std::unique_ptr<PoolClient> client_;
    std::thread netThread_;
    std::thread watchdogThread_;
    std::atomic<bool> running_{false};

    void watchdogLoop();
};
