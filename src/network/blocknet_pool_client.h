#pragma once
#include <string>
#include <atomic>
#include <mutex>
#include <cstdint>

struct BlocknetPoolJob
{
    std::string job_id;
    uint64_t height = 0;
    std::string header_base_hex;
    std::string target_hex;
    double difficulty = 0.0;
    uint64_t nonce_start = 0;
    uint64_t nonce_end = 0;
    bool valid = false;
};

// Client pour le protocole pool de Blocknet (Stratum-like, JSON ligne
// par ligne sur TCP brut) - meme famille de transport que PoolClient
// (BTC09, pool tiers), mais avec un format de messages different :
// - "status" au niveau racine du message (pas imbrique dans "result")
// - le champ "accepted" de la soumission est un booleen dans "result",
//   avec repli sur "status" racine si absent
// - le job utilise "header_base" (92 octets) au lieu de "header"
// Confirme en analysant le code source reel du mineur officiel
// Blocknet (seine, licence BSD 3-Clause), notamment ses fixtures de
// test qui montrent le format exact des messages reels.
class BlocknetPoolClient
{
public:
    BlocknetPoolClient(
        const std::string& host,
        int port,
        const std::string& wallet,
        const std::string& worker
    );
    ~BlocknetPoolClient();

    bool connect();
    void run();
    void stop();
    BlocknetPoolJob getJob();
    void submit(
        const std::string& job_id,
        uint64_t nonce,
        const std::string& claimedHashHex
    );

    uint64_t getAcceptedCount() const { return acceptedCount_.load(); }
    uint64_t getRejectedCount() const { return rejectedCount_.load(); }
    bool hadSuccessfulSession() const { return sessionSucceeded_.load(); }

private:
    std::atomic<uint64_t> acceptedCount_{0};
    std::atomic<uint64_t> rejectedCount_{0};
    std::atomic<bool> sessionSucceeded_{false};

    std::string host_;
    int port_;
    std::string wallet_;
    std::string worker_;
    std::string sourceLabel() const;

    int sock_ = -1;
    std::atomic<bool> running_{false};
    std::mutex jobMutex_;
    BlocknetPoolJob currentJob_;
    int requestId_ = 2; // id=1 reserve au login

    void sendJson(const std::string& payload);
    void handleLine(const std::string& line);
};
