#pragma once
#include "mining_source.h"
#include "work_client.h"
#include <mutex>
#include <atomic>
#include <thread>

class SoloJobManager : public MiningSource
{
public:
    // pollIntervalSeconds : frequence de sondage du coordinateur en
    // usage normal (hors 429). L'instance dev fee peut utiliser une
    // valeur plus longue, puisqu'elle n'est reellement utilisee que
    // 1% du temps et n'a pas besoin d'un job aussi frais que celui de
    // l'utilisateur - reduit le volume de requetes cumule.
    SoloJobManager(
        const std::string& poolUrl,
        const std::string& address,
        const std::string& worker,
        int pollIntervalSeconds = 10
    );
    ~SoloJobManager() override;

    void start() override;
    MiningJob getJob() override;
    void submitNonce(
        const std::string& job_id,
        uint64_t nonce,
        const std::vector<uint8_t>& hash,
        uint64_t height,
        bool isDevFeeJob
    ) override;

    uint64_t getAcceptedCount() const override { return acceptedCount_.load(); }
    uint64_t getRejectedCount() const override { return rejectedCount_.load(); }

private:
    std::atomic<uint64_t> acceptedCount_{0};
    std::atomic<uint64_t> rejectedCount_{0};
    std::string worker_;
    int pollIntervalSeconds_;
    WorkClient client_;
    std::mutex mutex_;
    std::string sourceLabel() const;
    MiningJob current_;
    std::atomic<bool> running_{false};
    std::thread pollThread_;

    void pollLoop();
    // Retourne true si un code HTTP 429 (Too Many Requests) a ete
    // recu, pour que pollLoop() applique un delai bien plus long
    // avant de reessayer plutot que le cycle normal de 10s.
    bool refreshWork();
};
