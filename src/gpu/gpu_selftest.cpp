#include "../argon2/argon2_engine.h"
#include "argon2-gpu-common/argon2params.h"
#include "argon2-cuda/globalcontext.h"
#include "argon2-cuda/programcontext.h"
#include "argon2-cuda/processingunit.h"

#include <cuda_runtime.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <iomanip>

// Auto-test : verifie que le hash calcule par le backend CUDA correspond
// exactement, octet par octet, a celui calcule par notre moteur CPU deja
// valide en conditions reelles (shares acceptees par le pool).
int runGpuSelfTest(int deviceIndex)
{
    // Header de test : 88 octets, motif reconnaissable (pas besoin d'un
    // vrai job reseau, seule la coherence CPU/GPU nous interesse ici).
    std::vector<uint8_t> header(88);
    for(size_t i = 0; i < header.size(); i++)
    {
        header[i] = static_cast<uint8_t>(i);
    }

    const uint32_t argonTime = 1;
    const uint32_t argonMemKib = 65536;

    // Salt officiel BTC09 : "BTC09/pow/v1", pas de secret ni de donnees additionnelles
    static const uint8_t salt[] = { 'B','T','C','0','9','/','p','o','w','/','v','1' };
    std::vector<uint8_t> saltVec(salt, salt + sizeof(salt));

    // --- Reference CPU (deja validee par de vrais shares acceptes) ---
    std::vector<uint8_t> cpuHash = Argon2Engine::hash(header, saltVec, argonTime, argonMemKib);

    // --- Calcul GPU ---
    cudaSetDevice(deviceIndex);  // avant toute construction CUDA (fix connu)

    argon2::cuda::GlobalContext global;
    auto &devices = global.getAllDevices();

    if(deviceIndex >= static_cast<int>(devices.size()))
    {
        std::cerr << "GPU selftest: device index out of range\n";
        return 1;
    }

    const auto &device = devices[deviceIndex];

    argon2::cuda::ProgramContext pc(
        &global, { device },
        argon2::ARGON2_ID, argon2::ARGON2_VERSION_13
    );

    argon2::Argon2Params params(
        32,               // outLen
        salt, sizeof(salt),
        nullptr, 0,       // secret
        nullptr, 0,       // ad
        argonTime, argonMemKib, 1  // t_cost, m_cost, lanes
    );

    argon2::cuda::ProcessingUnit unit(
        &pc, &params, &device,
        1,      // batchSize = 1 pour ce test
        true,   // bySegment
        false   // precomputeRefs
    );

    unit.setPassword(0, header.data(), header.size());
    unit.beginProcessing();
    unit.endProcessing();

    std::vector<uint8_t> gpuHash(32);
    unit.getHash(0, gpuHash.data());

    // --- Comparaison ---
    bool match = (cpuHash == gpuHash);

    auto printHex = [](const std::vector<uint8_t> &v)
    {
        for(uint8_t b : v)
        {
            printf("%02x", b);
        }
        printf("\n");
    };

    std::cout << "CPU hash: "; printHex(cpuHash);
    std::cout << "GPU hash: "; printHex(gpuHash);
    std::cout << (match ? "MATCH - le calcul GPU est correct.\n"
                         : "MISMATCH - NE PAS MINER EN GPU, bug a corriger.\n");

    return match ? 0 : 1;
}
