#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct MiningJob
{
    bool valid = false;
    std::string job_id;
    uint64_t height = 0;
    std::vector<uint8_t> header;   // 88 octets, nonce a zero
    std::vector<uint8_t> target;   // 32 octets
    uint32_t argon_mem_kib = 0;
    uint32_t argon_time = 0;
    double difficulty = 0.0;

    // Plage de nonce imposee par le serveur (mode pool). nonce_end == 0
    // signifie "aucune plage imposee" (mode solo) : chaque thread explore
    // une sous-plage arbitraire de l'espace 64-bit complet.
    uint64_t nonce_start = 0;
    uint64_t nonce_end = 0;
};

class MiningSource
{
public:
    virtual ~MiningSource() = default;
    virtual void start() = 0;
    virtual MiningJob getJob() = 0;
    virtual void submitNonce(
        const std::string& job_id,
        uint64_t nonce,
        const std::vector<uint8_t>& hash,
        uint64_t height
    ) = 0;

    // Statistiques de soumission confirmees par le serveur (pool ou
    // coordinateur solo). Par defaut a 0 : seules les sources qui
    // suivent reellement ces compteurs (mode pool) les redefinissent.
    virtual uint64_t getAcceptedCount() const { return 0; }
    virtual uint64_t getRejectedCount() const { return 0; }
};
