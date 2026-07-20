#pragma once
#include "../algorithm.h"

// Algorithme Argon2id pour Blocknet (2 GiB, 1 lane, 1 iteration) -
// mapping mot de passe/sel INVERSE par rapport a BTC09/Argon2idAlgorithm :
// ici, le nonce (8 octets, little-endian) EST le mot de passe complet,
// et le header_base (92 octets, fourni par le pool a chaque job,
// jamais fixe) EST le sel. Confirme en analysant le code source reel
// du mineur officiel Blocknet (seine, licence BSD 3-Clause,
// compatible GPLv2+), sur les implementations CPU et GPU.
//
// A la difference d'Argon2idAlgorithm (BTC09), le GPU n'est pas
// encore implemente ici (voir feuille de route) - gpuDeviceCount()
// annonce volontairement 0 device, ce qui fait que --gpu n'a
// simplement aucun effet pour cet algorithme, sans code special
// necessaire ailleurs dans le projet.
class BlocknetArgon2idAlgorithm : public Algorithm
{
public:
    BlocknetArgon2idAlgorithm();

    std::string name() const override;
    std::vector<uint8_t> hashCpu(
        const std::vector<uint8_t>& input,
        uint64_t nonce,
        uint32_t tCost = 0,
        uint32_t mCostKib = 0
    ) const override;
    size_t inputSize() const override;
    size_t gpuMemoryPerHashBytes() const override;
    std::unique_ptr<GpuHasher> createGpuHasher(
        int deviceIndex,
        size_t batchSize,
        uint32_t tCost,
        uint32_t mCostKib
    ) const override;
    int gpuDeviceCount() const override;
    void queryGpuMemory(int deviceIndex, size_t& freeBytes, size_t& totalBytes) const override;
    std::vector<uint8_t> buildPassword(
        const std::vector<uint8_t>& input,
        uint64_t nonce
    ) const override;

private:
    uint32_t tCost_;
    uint32_t mCostKib_;
};
