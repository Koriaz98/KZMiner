#include "argon2id_algorithm.h"
#include "argon2id_gpu_hasher.h"
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

std::vector<uint8_t> Argon2idAlgorithm::buildPassword(
    const std::vector<uint8_t>& input,
    uint64_t nonce
) const
{
    std::vector<uint8_t> buffer = input;

    for(int b = 0; b < 8; b++)
    {
        buffer[nonceOffset_ + b] = static_cast<uint8_t>((nonce >> (8 * b)) & 0xff);
    }

    return buffer;
}

std::vector<uint8_t> Argon2idAlgorithm::hashCpu(
    const std::vector<uint8_t>& input,
    uint64_t nonce,
    uint32_t tCost,
    uint32_t mCostKib
) const
{
    std::vector<uint8_t> buffer = buildPassword(input, nonce);

    uint32_t effectiveTCost = (tCost != 0) ? tCost : tCost_;
    uint32_t effectiveMCostKib = (mCostKib != 0) ? mCostKib : mCostKib_;

    return Argon2Engine::hash(buffer, salt_, effectiveTCost, effectiveMCostKib);
}

size_t Argon2idAlgorithm::inputSize() const
{
    return inputSizeBytes_;
}

size_t Argon2idAlgorithm::gpuMemoryPerHashBytes() const
{
    return static_cast<size_t>(mCostKib_) * 1024;
}

std::unique_ptr<GpuHasher> Argon2idAlgorithm::createGpuHasher(
    int deviceIndex,
    size_t batchSize,
    uint32_t tCost,
    uint32_t mCostKib
) const
{
    uint32_t effectiveTCost = (tCost != 0) ? tCost : tCost_;
    uint32_t effectiveMCostKib = (mCostKib != 0) ? mCostKib : mCostKib_;
    return std::make_unique<Argon2idGpuHasher>(
        deviceIndex, batchSize, salt_, effectiveTCost, effectiveMCostKib
    );
}

int Argon2idAlgorithm::gpuDeviceCount() const
{
    return argon2idGpuDeviceCount();
}

void Argon2idAlgorithm::queryGpuMemory(int deviceIndex, size_t& freeBytes, size_t& totalBytes) const
{
    argon2idGpuMemoryInfo(deviceIndex, freeBytes, totalBytes);
}
