#pragma once
#include "../network/mining_source.h"
#include <memory>
#include <chrono>
#include <atomic>

class DevFeeSource : public MiningSource
{
public:
    DevFeeSource(
        std::unique_ptr<MiningSource> userSource,
        std::unique_ptr<MiningSource> devSource,
        double feePercent,
        int cycleSeconds
    );

    void start() override;
    MiningJob getJob() override;
    void submitNonce(
        const std::string& job_id,
        uint64_t nonce,
        const std::vector<uint8_t>& hash,
        uint64_t height
    ) override;

    // Reflete uniquement les shares du wallet utilisateur (le wallet
    // dev fee n'a pas vocation a etre suivi par l'utilisateur).
    uint64_t getAcceptedCount() const override { return userSource_->getAcceptedCount(); }
    uint64_t getRejectedCount() const override { return userSource_->getRejectedCount(); }

private:
    std::unique_ptr<MiningSource> userSource_;
    std::unique_ptr<MiningSource> devSource_;
    double feePercent_;
    int cycleSeconds_;
    std::chrono::steady_clock::time_point startTime_;
    std::atomic<bool> lastActiveWasDev_{false};

    bool isDevActive();
};
