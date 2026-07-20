#include "argon2id_gpu_hasher.h"

#include "argon2-gpu-common/argon2params.h"
#include "argon2-cuda/globalcontext.h"
#include "argon2-cuda/programcontext.h"
#include "argon2-cuda/processingunit.h"

#include <cuda_runtime.h>
#include <stdexcept>

// Seul fichier de tout le projet (avec gpu_selftest.cpp, qui a son
// propre besoin de validation croisee independant) a inclure les
// en-tetes CUDA/argon2-gpu - toute la logique specifique au backend
// GPU d'Argon2id est concentree ici, derriere l'interface generique
// GpuHasher/Algorithm::createGpuHasher.

struct Argon2idGpuHasher::Impl
{
    argon2::cuda::GlobalContext global;
    argon2::cuda::ProgramContext pc;
    argon2::Argon2Params params;
    argon2::cuda::ProcessingUnit unit;
    size_t batchSizeValue;

    Impl(
        int deviceIndex,
        size_t batchSizeArg,
        const std::vector<uint8_t>& salt,
        uint32_t tCost,
        uint32_t mCostKib
    )
    : global()
    , pc(
        &global,
        { global.getAllDevices().at(static_cast<size_t>(deviceIndex)) },
        argon2::ARGON2_ID, argon2::ARGON2_VERSION_13
      )
    , params(
        32,
        salt.data(), salt.size(),
        nullptr, 0,
        nullptr, 0,
        tCost, mCostKib, 1
      )
    , unit(
        &pc, &params,
        &global.getAllDevices().at(static_cast<size_t>(deviceIndex)),
        batchSizeArg, true, false
      )
    , batchSizeValue(batchSizeArg)
    {
    }
};

Argon2idGpuHasher::Argon2idGpuHasher(
    int deviceIndex,
    size_t batchSizeArg,
    const std::vector<uint8_t>& salt,
    uint32_t tCost,
    uint32_t mCostKib
)
{
    // cudaSetDevice() doit imperativement precede toute construction
    // d'objet CUDA/argon2-gpu (fix connu, deja applique dans
    // gpu_selftest.cpp et l'ancien code de gpu_miner.cpp) - sans cela,
    // le contexte CUDA courant peut ne pas correspondre au device
    // vise, avec des resultats incoherents ou des plantages.
    cudaError_t setDevErr = cudaSetDevice(deviceIndex);
    if(setDevErr != cudaSuccess)
    {
        throw std::runtime_error(
            std::string("cudaSetDevice failed: ") + cudaGetErrorString(setDevErr)
        );
    }

    // Validation explicite de l'index avant de construire Impl (qui
    // appelle .at() plusieurs fois - .at() leverait deja une exception
    // en cas de depassement, mais un message d'erreur clair et unique
    // ici est plus lisible pour l'appelant).
    argon2::cuda::GlobalContext probe;
    if(deviceIndex < 0 || static_cast<size_t>(deviceIndex) >= probe.getAllDevices().size())
    {
        throw std::runtime_error("Argon2idGpuHasher: device index out of range");
    }

    impl_ = std::make_unique<Impl>(deviceIndex, batchSizeArg, salt, tCost, mCostKib);
}

Argon2idGpuHasher::~Argon2idGpuHasher() = default;

void Argon2idGpuHasher::setPassword(size_t index, const uint8_t* data, size_t len)
{
    impl_->unit.setPassword(index, data, len);
}

void Argon2idGpuHasher::computeBatch()
{
    impl_->unit.beginProcessing();
    impl_->unit.endProcessing();
}

void Argon2idGpuHasher::getHash(size_t index, uint8_t* out) const
{
    impl_->unit.getHash(index, out);
}

size_t Argon2idGpuHasher::batchSize() const
{
    return impl_->batchSizeValue;
}

int argon2idGpuDeviceCount()
{
    argon2::cuda::GlobalContext global;
    return static_cast<int>(global.getAllDevices().size());
}

void argon2idGpuMemoryInfo(int deviceIndex, size_t& freeBytes, size_t& totalBytes)
{
    cudaSetDevice(deviceIndex);
    cudaMemGetInfo(&freeBytes, &totalBytes);
}
