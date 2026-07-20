#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <memory>
class GpuHasher;

// Interface commune a tous les algorithmes de minage supportes par
// KZMiner. Chaque algorithme concret (Argon2id, KawPow, ...) implemente
// cette interface, independamment du coin/reseau qui l'utilise (voir
// src/coins/ pour la partie reseau, decouplee de cette interface).
class Algorithm
{
public:
    virtual ~Algorithm() = default;

    // Identifiant lisible, ex: "argon2id-btc09", "kawpow-rvn".
    virtual std::string name() const = 0;

    // Hash CPU. "input" est le buffer complet a hasher (header, deja
    // construit par le MiningSource du coin) ; "nonce" est fourni
    // separement car son emplacement/role varie selon l'algorithme
    // (ex: BTC09 l'integre dans le header, Blocknet l'utilise comme
    // mot de passe Argon2 separe du header/salt).
    // t_cost/m_cost_kib : parametres Argon2-specifiques, ignores par les
    // algorithmes qui n'en ont pas la notion (KawPow). Valeur 0 = utiliser
    // les valeurs par defaut internes de l'implementation concrete.
    virtual std::vector<uint8_t> hashCpu(
        const std::vector<uint8_t>& input,
        uint64_t nonce,
        uint32_t tCost = 0,
        uint32_t mCostKib = 0
    ) const = 0;

    // Taille en octets du buffer "input" attendu par hashCpu/hashGpu.
    virtual size_t inputSize() const = 0;

    // VRAM consommee par hash sur GPU, pour le calcul de batch-size
    // existant dans GpuMiner (deja parametrique, aucun changement
    // structurel necessaire de ce cote).
    virtual size_t gpuMemoryPerHashBytes() const = 0;
    // Cree un hasheur GPU pour le device et la taille de batch donnes,
    // avec les parametres memoire/iterations annonces par le job en
    // cours (peuvent differer des valeurs par defaut de l'algorithme).
    // L'appelant (GpuMiner) reste entierement decouple du backend
    // concret (CUDA/argon2-gpu aujourd'hui, potentiellement autre
    // chose pour un futur algorithme).
    virtual std::unique_ptr<GpuHasher> createGpuHasher(
        int deviceIndex,
        size_t batchSize,
        uint32_t tCost,
        uint32_t mCostKib
    ) const = 0;
    // Nombre de GPU compatibles detectes pour cet algorithme (le
    // mecanisme de detection depend du backend concret - CUDA/argon2-gpu
    // aujourd'hui - mais GpuMiner n'a besoin de connaitre que ce
    // resultat, jamais le mecanisme lui-meme).
    virtual int gpuDeviceCount() const = 0;
    // VRAM libre/totale (en octets) du device GPU donne, pour le calcul
    // de batch-size dans GpuMiner. Le mecanisme de lecture depend du
    // backend concret (cudaMemGetInfo aujourd'hui), jamais expose a
    // l'appelant.
    virtual void queryGpuMemory(
        int deviceIndex,
        size_t& freeBytes,
        size_t& totalBytes
    ) const = 0;
    // Construit le mot de passe complet (nonce integre selon le schema
    // propre a l'algorithme concret - position dans l'input pour BTC09,
    // ou meme absence totale d'integration pour un schema invers comme
    // celui de Blocknet) a partir de l'input brut et du nonce. Utilise
    // en interne par hashCpu, et directement par GpuMiner pour preparer
    // chaque emplacement d'un batch - la logique d'integration du nonce
    // n'est ainsi ecrite qu'une seule fois, jamais dupliquee entre CPU
    // et GPU.
    virtual std::vector<uint8_t> buildPassword(
        const std::vector<uint8_t>& input,
        uint64_t nonce
    ) const = 0;

    // --- Hooks pour algorithmes a dataset/kernel dynamique (KawPow) ---
    // No-op par defaut : les algorithmes simples comme Argon2id n'ont
    // rien a faire ici.

    // true si l'algorithme necessite un dataset precalcule (DAG) qui
    // doit etre regenere periodiquement (epoch).
    virtual bool requiresDataset() const { return false; }

    // (Re)genere le dataset pour l'epoch donnee si necessaire. Les
    // implementations concernees (KawPow) verifient elles-memes si un
    // recalcul est requis (epoch differente de la precedente).
    virtual void prepareDatasetForEpoch(uint64_t epoch) {}

    // true si l'algorithme necessite une recompilation periodique du
    // kernel GPU (ex: KawPow via NVRTC, programme aleatoire qui change
    // plus frequemment que le dataset lui-meme).
    virtual bool requiresDynamicKernel() const { return false; }

    // Recompile le kernel GPU si la "period" a change depuis le dernier
    // appel. No-op pour les algorithmes a kernel fixe (Argon2id).
    virtual void recompileKernelIfNeeded(uint64_t period) {}
};
