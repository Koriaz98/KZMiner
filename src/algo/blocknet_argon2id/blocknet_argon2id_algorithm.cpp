#include "blocknet_argon2id_algorithm.h"
#include "../../argon2/argon2_engine.h"
#include <stdexcept>

namespace
{
    constexpr uint32_t kTCost = 1;
    constexpr uint32_t kMCostKib = 2u * 1024u * 1024u; // 2 GiB
    constexpr size_t kHeaderBaseLen = 92;
}

BlocknetArgon2idAlgorithm::BlocknetArgon2idAlgorithm()
: tCost_(kTCost), mCostKib_(kMCostKib)
{
}

std::string BlocknetArgon2idAlgorithm::name() const
{
    return "Argon2id-2GiB-Blocknet";
}

std::vector<uint8_t> BlocknetArgon2idAlgorithm::buildPassword(
    const std::vector<uint8_t>& /*input*/,
    uint64_t nonce
) const
{
    // Le nonce EST le mot de passe entier (8 octets, little-endian) -
    // le header_base ("input") ne joue aucun role dans le mot de
    // passe, uniquement dans le sel (voir hashCpu ci-dessous).
    std::vector<uint8_t> password(8);
    for(int b = 0; b < 8; b++)
    {
        password[b] = static_cast<uint8_t>((nonce >> (8 * b)) & 0xff);
    }
    return password;
}

std::vector<uint8_t> BlocknetArgon2idAlgorithm::hashCpu(
    const std::vector<uint8_t>& input,
    uint64_t nonce,
    uint32_t tCost,
    uint32_t mCostKib
) const
{
    std::vector<uint8_t> password = buildPassword(input, nonce);

    uint32_t effectiveTCost = (tCost != 0) ? tCost : tCost_;
    uint32_t effectiveMCostKib = (mCostKib != 0) ? mCostKib : mCostKib_;

    // Le header_base ("input") EST le sel ici - pas une constante fixe
    // comme pour BTC09, il varie a chaque job recu du pool.
    return Argon2Engine::hash(password, input, effectiveTCost, effectiveMCostKib);
}

size_t BlocknetArgon2idAlgorithm::inputSize() const
{
    return kHeaderBaseLen;
}

size_t BlocknetArgon2idAlgorithm::gpuMemoryPerHashBytes() const
{
    return static_cast<size_t>(mCostKib_) * 1024;
}

std::unique_ptr<GpuHasher> BlocknetArgon2idAlgorithm::createGpuHasher(
    int /*deviceIndex*/,
    size_t /*batchSize*/,
    uint32_t /*tCost*/,
    uint32_t /*mCostKib*/
) const
{
    throw std::runtime_error("Blocknet: GPU mining not yet implemented in KZMiner, use --cpu only");
}

int BlocknetArgon2idAlgorithm::gpuDeviceCount() const
{
    // GPU pas encore implemente pour Blocknet (voir feuille de route) -
    // annoncer 0 device fait que --gpu n'a simplement aucun effet,
    // sans code special necessaire ailleurs dans le projet.
    return 0;
}

void BlocknetArgon2idAlgorithm::queryGpuMemory(
    int /*deviceIndex*/,
    size_t& freeBytes,
    size_t& totalBytes
) const
{
    freeBytes = 0;
    totalBytes = 0;
}
