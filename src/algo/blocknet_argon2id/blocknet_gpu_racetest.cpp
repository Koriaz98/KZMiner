#include "blocknet_argon2id_algorithm.h"
#include "blocknet_gpu_hasher.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>

// Test diagnostique dedie, a PETITE echelle (64 Kio au lieu de 2 GiB)
// - permet a compute-sanitizer --tool racecheck (bien plus lent que
// memcheck, doit suivre chaque acces memoire partagee) de terminer en
// quelques secondes plutot qu'en plusieurs minutes. Une vraie course
// entre threads dans la logique cooperative se manifesterait deja a
// cette echelle reduite - pas besoin des 2 GiB complets pour la
// detecter. Ne remplace PAS l'auto-test de correction a pleine
// echelle (blocknet-gpu-selftest), sert uniquement au diagnostic.
int main()
{
    BlocknetArgon2idAlgorithm algo;

    std::vector<uint8_t> headerBase(92);
    for(size_t i = 0; i < headerBase.size(); i++)
    {
        headerBase[i] = static_cast<uint8_t>(i);
    }
    const uint64_t testNonce = 0x0123456789ABCDEFULL;
    const uint32_t smallMCostKib = 64; // 64 blocs de 1 KiB, 16 par tranche

    std::vector<uint8_t> cpuHash = algo.hashCpu(headerBase, testNonce, 1, smallMCostKib);

    auto hasher = algo.createGpuHasher(0, 1, 1, smallMCostKib);
    hasher->setSalt(headerBase.data(), headerBase.size());
    std::vector<uint8_t> password = algo.buildPassword(headerBase, testNonce);
    hasher->setPassword(0, password.data(), password.size());
    hasher->computeBatch();

    // DIAGNOSTIC TEMPORAIRE : extrait toute la memoire du hachage 0
    // pour comparaison bloc par bloc avec une reference independante.
    BlocknetGpuHasher* concreteHasher = static_cast<BlocknetGpuHasher*>(hasher.get());
    std::vector<uint8_t> fullMemory = concreteHasher->debugDumpMemory(0, smallMCostKib);
    std::ofstream memFile("/tmp/gpu_memory_dump.bin", std::ios::binary);
    memFile.write(reinterpret_cast<char*>(fullMemory.data()), fullMemory.size());
    memFile.close();
    std::cout << "Memoire complete du hachage 0 exportee vers /tmp/gpu_memory_dump.bin ("
              << fullMemory.size() << " octets)\n";

    std::vector<uint8_t> gpuHash(32);
    hasher->getHash(0, gpuHash.data());

    bool match = (cpuHash == gpuHash);
    auto printHex = [](const std::vector<uint8_t>& v)
    {
        for(uint8_t b : v) printf("%02x", b);
        printf("\n");
    };
    std::cout << "CPU hash (petite echelle): "; printHex(cpuHash);
    std::cout << "GPU hash (petite echelle): "; printHex(gpuHash);
    std::cout << (match ? "MATCH\n" : "MISMATCH\n");

    return match ? 0 : 1;
}
