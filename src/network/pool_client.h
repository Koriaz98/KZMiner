#pragma once
#include <string>
#include <atomic>
#include <mutex>
#include <cstdint>

struct PoolJob
{
    std::string job_id;
    uint64_t height = 0;
    std::string header_hex;
    std::string target_hex;
    double difficulty = 0.0;
    uint64_t nonce_start = 0;
    uint64_t nonce_end = 0;
    bool valid = false;
};

class PoolClient
{
public:
    PoolClient(
        const std::string& host,
        int port,
        const std::string& wallet,
        const std::string& worker
    );
    ~PoolClient();

    bool connect();
    void run();
    void stop();

    PoolJob getJob();

    // Renvoie true si la ligne a ete envoyee avec succes sur le socket
    // (false = echec d'envoi, la soumission n'a pas atteint le pool).
    bool submit(
        const std::string& job_id,
        uint64_t nonce,
        const std::string& hashHex
    );

    uint64_t getAcceptedCount() const { return acceptedCount_.load(); }
    uint64_t getRejectedCount() const { return rejectedCount_.load(); }

    // true si le pool a confirme le login OU envoye au moins un job
    // durant cette session precise (pas juste une poignee de main TCP
    // reussie, qui ne garantit pas que le pool accepte reellement la
    // connexion sur la duree).
    bool hadSuccessfulSession() const { return sessionSucceeded_.load(); }
    // true si le pool a EXPLICITEMENT rejete le login (reponse id=1 avec un
    // statut != ok) - distinct d'une simple coupure reseau. Consomme par la
    // politique fatale generique (LoginFatalPolicy), partagee avec Blocknet.
    bool loginWasRejected() const { return loginRejected_.load(); }

private:
    std::atomic<uint64_t> acceptedCount_{0};
    std::atomic<uint64_t> rejectedCount_{0};
    std::atomic<bool> sessionSucceeded_{false};
    std::atomic<bool> loginRejected_{false};
    std::string host_;
    int port_;
    std::string wallet_;
    std::string worker_;
    std::string sourceLabel() const;

    int sock_ = -1;
    std::atomic<bool> running_{false};

    std::mutex jobMutex_;
    PoolJob currentJob_;

    // submit() est appele concurremment par plusieurs workers (chaque
    // thread CPU, chaque worker GPU). requestId_ atomique (sinon data
    // race sur l'increment) ; sendMutex_ serialise tous les envois socket
    // (login + submits) pour empecher l'entrelacement des lignes JSON sur
    // le fil, qui corromprait le protocole (suspect des duplicate shares).
    std::atomic<int> requestId_{2}; // id=1 reserve au login
    std::mutex sendMutex_;

    bool sendJson(const std::string& payload);
    void handleLine(const std::string& line);
};
