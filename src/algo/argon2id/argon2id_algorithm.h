#pragma once
#include "../algorithm.h"

// Implementation Argon2id generique de l'interface Algorithm. Les
// parametres specifiques a un coin (salt, m_cost/t_cost, position du
// nonce dans l'input) sont fournis au constructeur plutot que codes en
// dur ici, pour permettre a plusieurs coins (BTC09, et plus tard
// Blocknet) de reutiliser cette meme classe avec des parametres
// differents.
class Argon2idAlgorithm : public Algorithm
{
public:
    Argon2idAlgorithm(
        std::string algoName,
        std::vector<uint8_t> salt,
        size_t inputSizeBytes,
        size_t nonceOffsetBytes,
        uint32_t tCost,
        uint32_t mCostKib
    );

    std::string name() const override;
    std::vector<uint8_t> hashCpu(const std::vector<uint8_t>& input, uint64_t nonce, uint32_t tCost = 0, uint32_t mCostKib = 0) const override;
    size_t inputSize() const override;
    size_t gpuMemoryPerHashBytes() const override;

    // Accesseurs specifiques Argon2id, utilises par GpuMiner (le
    // pipeline GPU actuel reste base sur argon2-gpu, ces parametres lui
    // sont necessaires directement plutot que via l'interface generique).
    const std::vector<uint8_t>& salt() const { return salt_; }
    size_t nonceOffset() const { return nonceOffset_; }
    uint32_t tCost() const { return tCost_; }
    uint32_t mCostKib() const { return mCostKib_; }

private:
    std::string name_;
    std::vector<uint8_t> salt_;
    size_t inputSizeBytes_;
    size_t nonceOffset_;
    uint32_t tCost_;
    uint32_t mCostKib_;
};
