#include "blocknet_argon2id_algorithm.h"
#include "blocknet_gpu_hasher.h"
#include <iostream>
#include <vector>
#include <cstdint>

// Auto-test dedie a Blocknet : verifie que notre noyau CUDA "maison"
// produit exactement le meme resultat que notre implementation CPU
// deja validee (de vraies parts acceptees sur le pool officiel
// bntpool.com). Teste 4 hachages simultanes (nonces differents) dans
// le MEME lot - important car notre noyau traite 4 hachages par bloc
// CUDA (32 threads = 4 x 8 threads cooperants) : un test a un seul
// hachage ne verifierait que le premier des 4 emplacements en memoire
// partagee, sans jamais prouver l'absence d'interference entre eux.
int main()
{
    BlocknetArgon2idAlgorithm algo;

    std::vector<uint8_t> headerBase(92);
    for(size_t i = 0; i < headerBase.size(); i++)
    {
        headerBase[i] = static_cast<uint8_t>(i);
    }

    const size_t batchSize = 4;
    const uint64_t testNonces[batchSize] = {
        0x0123456789ABCDEFULL,
        0x1111111111111111ULL,
        0xFEDCBA9876543210ULL,
        0xAAAABBBBCCCCDDDDULL
    };

    auto hasher = algo.createGpuHasher(0, batchSize, 1, 2u * 1024u * 1024u);
    hasher->setSalt(headerBase.data(), headerBase.size());

    std::vector<std::vector<uint8_t>> cpuHashes(batchSize);
    for(size_t i = 0; i < batchSize; i++)
    {
        cpuHashes[i] = algo.hashCpu(headerBase, testNonces[i]);
        std::vector<uint8_t> password = algo.buildPassword(headerBase, testNonces[i]);
        hasher->setPassword(i, password.data(), password.size());
    }

    hasher->computeBatch();

    auto printHex = [](const std::vector<uint8_t>& v)
    {
        for(uint8_t b : v) printf("%02x", b);
        printf("\n");
    };

    bool allMatch = true;
    for(size_t i = 0; i < batchSize; i++)
    {
        std::vector<uint8_t> gpuHash(32);
        hasher->getHash(i, gpuHash.data());

        bool match = (cpuHashes[i] == gpuHash);
        allMatch = allMatch && match;

        std::cout << "--- Hachage " << i << " (nonce 0x" << std::hex << testNonces[i] << std::dec << ") ---\n";
        std::cout << "CPU hash: "; printHex(cpuHashes[i]);
        std::cout << "GPU hash: "; printHex(gpuHash);
        std::cout << (match ? "MATCH\n" : "MISMATCH\n");
    }

    std::cout << (allMatch
        ? "\nTOUT CONCORDE - le calcul GPU Blocknet (4 hachages simultanes) est correct.\n"
        : "\nAU MOINS UN MISMATCH - bug d'isolation entre hachages a corriger, NE PAS MINER EN GPU.\n");

    return allMatch ? 0 : 1;
}
