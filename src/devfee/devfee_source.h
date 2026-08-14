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
        uint64_t height,
        bool isDevFeeJob
    ) override;

    // Reflete uniquement les shares du wallet utilisateur (le wallet
    // dev fee n'a pas vocation a etre suivi par l'utilisateur).
    uint64_t getSubmittedCount() const override { return userSource_->getSubmittedCount(); }
    uint64_t getAcceptedCount() const override { return userSource_->getAcceptedCount(); }
    uint64_t getRejectedCount() const override { return userSource_->getRejectedCount(); }
    uint64_t getSendFailedCount() const override { return userSource_->getSendFailedCount(); }

    // Portee du fatal : UNIQUEMENT le wallet utilisateur. Un rejet de login
    // cote source dev (adresse dev codee en dur, valide) ne doit jamais
    // arreter le minage de l'utilisateur - au pire une panne dev signifie
    // 100 % user, sans danger. On ne propage donc que le fatal de userSource_.
    bool hasFatalError() const override { return userSource_->hasFatalError(); }
    std::string fatalError() const override { return userSource_->fatalError(); }

private:
    std::unique_ptr<MiningSource> userSource_;
    std::unique_ptr<MiningSource> devSource_;
    double feePercent_;
    int cycleSeconds_;
    std::chrono::steady_clock::time_point startTime_;
    std::atomic<bool> lastActiveWasDev_{false};

    bool isDevActive();
};
