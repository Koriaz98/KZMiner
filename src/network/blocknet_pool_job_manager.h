#pragma once
#include "mining_source.h"
#include "blocknet_pool_client.h"
#include <thread>
#include <atomic>
#include <string>
#include <memory>

// Implementation de MiningSource pour le protocole pool officiel de
// Blocknet - meme structure que PoolJobManager (BTC09, pool tiers),
// juste branchee sur BlocknetPoolClient plutot que PoolClient.
class BlocknetPoolJobManager : public MiningSource
{
public:
    BlocknetPoolJobManager(
        const std::string& host,
        int port,
        const std::string& wallet,
        const std::string& worker
    );
    ~BlocknetPoolJobManager() override;

    void start() override;
    MiningJob getJob() override;
    void submitNonce(
        const std::string& job_id,
        uint64_t nonce,
        const std::vector<uint8_t>& hash,
        uint64_t height,
        bool isDevFeeJob
    ) override;

    uint64_t getAcceptedCount() const override;
    uint64_t getRejectedCount() const override;

private:
    std::string host_;
    int port_;
    std::string wallet_;
    std::string worker_;

    std::unique_ptr<BlocknetPoolClient> client_;
    std::thread netThread_;
    std::thread watchdogThread_;
    std::atomic<bool> running_{false};
    int consecutiveFailures_ = 0;

    void watchdogLoop();
    int reconnectDelaySeconds() const;
};
