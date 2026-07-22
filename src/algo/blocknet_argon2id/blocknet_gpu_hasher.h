#pragma once
#include "../../gpu/gpu_hasher.h"
#include <vector>
#include <cstdint>
#include <memory>

// Implementation concrete de GpuHasher pour Blocknet, basee sur notre
// propre noyau CUDA Argon2id (pas la bibliotheque argon2-gpu
// vendorisee, incompatible avec un sel variable par job - voir
// argon2id_gpu_hasher.h pour BTC09, dont le sel est fixe a la
// construction). Implementation "maison" du standard Argon2id
// (RFC 9106), verifiee ligne par ligne contre libargon2 (la
// bibliotheque de reference deja utilisee et validee pour le CPU),
// pas un portage direct du noyau optimise de seine.
//
// lanes=1, t_cost=1 pour Blocknet - chaque thread CUDA traite un hash
// complet de bout en bout, sequentiellement (le remplissage memoire
// est intrinsequement sequentiel avec une seule lane) - le
// parallelisme vient du nombre de hachages independants (nonces
// differents) traites simultanement, pas d'une parallelisation
// interne a un seul hash. Version initiale correcte mais pas
// maximalement optimisee - a affiner une fois la justesse confirmee.
class BlocknetGpuHasher : public GpuHasher
{
public:
    BlocknetGpuHasher(
        int deviceIndex,
        size_t batchSizeArg,
        uint32_t tCost,
        uint32_t mCostKib
    );
    ~BlocknetGpuHasher() override;

    void setPassword(size_t index, const uint8_t* data, size_t len) override;
    void computeBatch() override;
    void getHash(size_t index, uint8_t* out) const override;
    size_t batchSize() const override;

    // Le sel varie par job pour Blocknet (contrairement a BTC09) -
    // methode additionnelle, appelee avant chaque computeBatch() si
    // le job (donc le header_base) a change.
    void setSalt(const uint8_t* data, size_t len) override;

    // DIAGNOSTIC TEMPORAIRE : extrait l'integralite de la memoire d'un
    // hachage donne du lot, pour comparaison bloc par bloc avec une
    // reference independante. A retirer une fois le bug localise.
    std::vector<uint8_t> debugDumpMemory(size_t index, size_t mCostKib) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Nombre de GPU CUDA disponibles (meme mecanisme que pour BTC09,
// fonction libre partagee - voir argon2id_gpu_hasher.h).
int blocknetGpuDeviceCount();
void blocknetGpuMemoryInfo(int deviceIndex, size_t& freeBytes, size_t& totalBytes);
