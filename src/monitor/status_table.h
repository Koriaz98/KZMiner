#pragma once
#include "system_monitor.h"
#include <cstdint>
#include <vector>
#include <string>

struct GpuRow
{
    GpuStats stats;
    double hashrate = 0.0;
};

struct DashboardData
{
    double totalHashrate = 0.0;
    uint64_t shares = 0;
    uint64_t accepted = 0;
    uint64_t rejected = 0;
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
