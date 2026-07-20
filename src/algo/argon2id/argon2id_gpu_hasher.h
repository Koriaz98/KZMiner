#pragma once
#include "../../gpu/gpu_hasher.h"
#include <vector>
#include <cstdint>
#include <memory>

// Implementation concrete de GpuHasher pour Argon2id, basee sur
// argon2-gpu/CUDA. Utilise le patron "Pimpl" (implementation privee
// opaque) : ce header ne depend d'aucun en-tete CUDA, seul le fichier
// .cpp correspondant en a besoin - ainsi, tout code qui inclut
// seulement ce header (comme argon2id_algorithm.h/.cpp) reste
// entierement independant de CUDA.
class Argon2idGpuHasher : public GpuHasher
{
public:
    Argon2idGpuHasher(
        int deviceIndex,
        size_t batchSizeArg,
        const std::vector<uint8_t>& salt,
        uint32_t tCost,
        uint32_t mCostKib
    );
    ~Argon2idGpuHasher() override;

    void setPassword(size_t index, const uint8_t* data, size_t len) override;
    void computeBatch() override;
    void getHash(size_t index, uint8_t* out) const override;
    size_t batchSize() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Nombre de GPU CUDA compatibles avec argon2-gpu detectes sur cette
// machine. Fonction libre plutot que methode statique de la classe :
// n'a pas besoin d'une instance de hasheur pour repondre.
int argon2idGpuDeviceCount();

// VRAM libre/totale (en octets) du device donne. Selectionne le device
// (cudaSetDevice) en interne avant l'appel a cudaMemGetInfo.
void argon2idGpuMemoryInfo(int deviceIndex, size_t& freeBytes, size_t& totalBytes);
