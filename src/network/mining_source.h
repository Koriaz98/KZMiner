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

    // Fige, au moment ou ce job est recupere, si c'est un job du
    // wallet dev fee ou de l'utilisateur - reutilise tel quel a la
    // soumission (voir MiningSource::submitNonce) plutot que de
    // reevaluer la bascule dev fee a cet instant different, ce qui
    // pourrait ne plus correspondre au job reellement calcule si la
    // fenetre a bascule entre-temps.
    bool isDevFeeJob = false;
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
        uint64_t height,
        bool isDevFeeJob
    ) = 0;

    // Compteurs de shares, sur toute la duree du run et survivant aux
    // reconnexions, pour le wallet utilisateur uniquement (le dev fee
    // n'est pas suivi cote affichage). Semantique commune a tous les
    // modes (solo/pool) :
    //   submitted = shares reellement ENVOYEES avec succes au pool/coordinateur
    //   accepted / rejected = verdicts du serveur
    //   sendFailed = soumissions dont l'envoi a echoue (jamais arrivees)
    //   pending (non expose) = submitted - accepted - rejected (en vol, transitoire)
    // Par defaut a 0 : chaque source concrete redefinit ce qu'elle suit.
    virtual uint64_t getSubmittedCount() const { return 0; }
    virtual uint64_t getAcceptedCount() const { return 0; }
    virtual uint64_t getRejectedCount() const { return 0; }
    virtual uint64_t getSendFailedCount() const { return 0; }
};
