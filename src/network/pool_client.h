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

    void submit(
        const std::string& job_id,
        uint64_t nonce,
        const std::string& hashHex
    );

    uint64_t getAcceptedCount() const { return acceptedCount_.load(); }
    uint64_t getRejectedCount() const { return rejectedCount_.load(); }

private:
    std::atomic<uint64_t> acceptedCount_{0};
    std::atomic<uint64_t> rejectedCount_{0};
    std::string host_;
    int port_;
    std::string wallet_;
    std::string worker_;

    int sock_ = -1;
    std::atomic<bool> running_{false};

    std::mutex jobMutex_;
    PoolJob currentJob_;

    int requestId_ = 2; // id=1 reserve au login

    void sendJson(const std::string& payload);
    void handleLine(const std::string& line);
};
