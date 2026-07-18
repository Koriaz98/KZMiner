#pragma once
#include "system_monitor.h"
#include <cstdint>
#include <vector>
#include <string>

struct GpuRow
{
    GpuStats stats;
    uint64_t hashrate = 0;
};

struct DashboardData
{
    uint64_t totalHashrate = 0;
    uint64_t shares = 0;
    uint64_t accepted = 0;
    uint64_t rejected = 0;
    double difficulty = 0.0;
    uint64_t height = 0;

    int cpuThreads = 0;
    uint64_t cpuHashrate = 0;

    std::vector<GpuRow> gpuRows;
    std::string algoName;
    std::string walletAddress;
};

void printStatusTable(const DashboardData& data);
