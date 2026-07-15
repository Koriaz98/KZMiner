#include "argon2id_algorithm.h"
#include "../../argon2/argon2_engine.h"
#include <cstring>

Argon2idAlgorithm::Argon2idAlgorithm(
    std::string algoName,
    std::vector<uint8_t> salt,
    size_t inputSizeBytes,
    size_t nonceOffsetBytes,
    uint32_t tCost,
    uint32_t mCostKib
)
: name_(std::move(algoName))
, salt_(std::move(salt))
, inputSizeBytes_(inputSizeBytes)
, nonceOffset_(nonceOffsetBytes)
, tCost_(tCost)
, mCostKib_(mCostKib)
{
}

std::string Argon2idAlgorithm::name() const
{
    return name_;
}

std::vector<uint8_t> Argon2idAlgorithm::hashCpu(
    const std::vector<uint8_t>& input,
    uint64_t nonce
) const
{
    std::vector<uint8_t> buffer = input;

    for(int b = 0; b < 8; b++)
    {
        buffer[nonceOffset_ + b] = static_cast<uint8_t>((nonce >> (8 * b)) & 0xff);
    }

    return Argon2Engine::hash(buffer, tCost_, mCostKib_);
}

size_t Argon2idAlgorithm::inputSize() const
{
    return inputSizeBytes_;
}

size_t Argon2idAlgorithm::gpuMemoryPerHashBytes() const
{
    return static_cast<size_t>(mCostKib_) * 1024;
}
