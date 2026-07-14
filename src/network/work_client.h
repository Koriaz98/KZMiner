#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

struct MiningWork
{
    std::string job_id;          // 32 hex chars (16 bytes)
    uint64_t height = 0;
    std::vector<uint8_t> header; // 88 bytes, nonce (8 derniers octets) a zero
    std::vector<uint8_t> target; // 32 bytes, cible a ne pas depasser
    std::string expires_at;
    uint32_t argon_mem_kib = 0;
    uint32_t argon_time = 0;
};

struct SubmitResult
{
    bool ok = false;
    std::string status;       // "block_accepted" si succes
    std::string error_code;   // rempli si echec
    std::string block_id;
};

class WorkClient
{
public:
    WorkClient(
        const std::string& poolUrl,
        const std::string& address,
        const std::string& worker
    );

    // Retourne nullopt en cas d'echec reseau ou de travail invalide
    std::optional<MiningWork> requestWork();

    SubmitResult submitNonce(
        const std::string& job_id,
        uint64_t nonce
    );

private:
    std::string poolUrl_;
    std::string address_;
    std::string worker_;

    std::string httpPost(
        const std::string& path,
        const std::string& jsonBody
    );
};
