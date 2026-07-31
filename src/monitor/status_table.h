#pragma once
#include "system_monitor.h"
#include <cstdint>
#include <vector>
#include <string>

struct GpuRow
{
    GpuStats stats;
    double hashrate = 0.0;
    // Index CUDA compacte (0..N-1) : la cle de getDeviceHashes(). Ne
    // coincide avec la numerotation physique que si toutes les cartes
    // de la machine sont utilisees par ce process.
    int cudaIndex = -1;
    // Partie "bus" de l'adresse PCI en decimal - seul identifiant
    // stable entre numerotation CUDA et NVML, donc cle de jointure.
    int pciBusDecimal = -1;
    // false quand aucune ligne nvidia-smi ne correspond a ce bus PCI
    // (nvidia-smi absent ou en echec) : on affiche alors le hashrate,
    // qui vient de CUDA et reste juste, et "N/A" partout ailleurs
    // plutot que des zeros qu'on lirait a tort comme une carte au repos.
    bool telemetryAvailable = false;
};

struct DashboardData
{
    double totalHashrate = 0.0;
    uint64_t shares = 0;      // shares submitted (envoyees avec succes), user-only
    uint64_t accepted = 0;
    uint64_t rejected = 0;
    uint64_t sendFailed = 0;  // soumissions dont l'envoi a echoue (affiche si > 0)
    double difficulty = 0.0;
    uint64_t height = 0;

    int cpuThreads = 0;
    double cpuHashrate = 0.0;

    std::vector<GpuRow> gpuRows;
    std::string algoName;
    std::string walletAddress;
    std::string poolAddress;
    std::string mode;
    std::string workerName;
    uint64_t uptimeSeconds = 0;
};

void printStatusTable(const DashboardData& data);
