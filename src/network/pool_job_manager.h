#pragma once
#include "mining_source.h"
#include "pool_client.h"
#include "login_fatal_policy.h"
#include <thread>
#include <atomic>
#include <string>
#include <memory>
#include <mutex>

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
        uint64_t height,
        bool isDevFeeJob
    ) override;

    uint64_t getSubmittedCount() const override;
    uint64_t getAcceptedCount() const override;
    uint64_t getRejectedCount() const override;
    uint64_t getSendFailedCount() const override;

    // Etat fatal expose a l'orchestration (main.cpp) : login user rejete de
    // maniere repetee (voir LoginFatalPolicy, partagee avec Blocknet). Sur
    // true, main arrete tout le mineur proprement.
    bool hasFatalError() const override { return loginPolicy_.isFatal(); }
    std::string fatalError() const override { return loginPolicy_.reason(); }

private:
    std::string host_;
    int port_;
    std::string wallet_;
    std::string worker_;

    // Politique fatale generique (memes constantes/logique que Blocknet).
    // Mutee UNIQUEMENT par le thread watchdog ; lue par main.
    LoginFatalPolicy loginPolicy_;

    // client_ est remplace par le watchdog en cas de reconnexion, pendant
    // que des threads workers le lisent via getJob()/submitNonce(). shared_ptr
    // + clientMutex_ + snapshotClient() garantissent qu'un worker garde
    // l'objet vivant le temps de son appel meme si le watchdog swappe
    // (voir issue #1 : sans ca, use-after-free).
    std::shared_ptr<PoolClient> client_;
    mutable std::mutex clientMutex_;
    std::thread netThread_;
    std::thread watchdogThread_;
    std::atomic<bool> running_{false};
    int consecutiveFailures_ = 0;

    // Compteurs de shares survivant aux reconnexions (les compteurs bruts
    // vivent dans le client, detruit/recree a chaque reconnexion).
    // submitted_/sendFailed_ : comptes dans submitNonce() (threads workers)
    // -> atomiques. acceptedBase_/rejectedBase_ : comptes des clients deja
    // remplaces, replies au swap ; accedes UNIQUEMENT sous clientMutex_
    // (meme lock que le pliage) pour que le swap soit tout-ou-rien vis-a-vis
    // des lecteurs (pas de double comptage).
    std::atomic<uint64_t> submitted_{0};
    std::atomic<uint64_t> sendFailed_{0};
    uint64_t acceptedBase_ = 0;
    uint64_t rejectedBase_ = 0;

    // Copie atomique de client_ sous clientMutex_ : maintient l'objet en vie
    // le temps de l'appel de l'appelant, independamment d'un swap concurrent.
    std::shared_ptr<PoolClient> snapshotClient() const;
    void watchdogLoop();
    int reconnectDelaySeconds() const;
};
