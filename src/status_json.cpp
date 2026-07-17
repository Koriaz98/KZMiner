#include "status_json.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>

using json = nlohmann::json;

namespace
{
    constexpr const char* kStatusPath = "/tmp/kzminer-status.json";
    constexpr const char* kStatusPathTmp = "/tmp/kzminer-status.json.tmp";
}

void writeStatusJson(
    const DashboardData& data,
    double cpuTempCelsius,
    bool cpuTempAvailable,
    double cpuUsagePercent,
    bool cpuUsageAvailable,
    double cpuPowerWatts,
    bool cpuPowerAvailable,
    const std::string& cpuModelName,
    uint64_t uptimeSeconds,
    const std::string& version
)
{
    json j;

    j["ver"] = version;
    j["algo"] = data.algoName;
    j["uptime"] = uptimeSeconds;
    j["hashrate_total_hs"] = data.totalHashrate;
    j["shares"] = {
        {"total", data.shares},
        {"accepted", data.accepted},
        {"rejected", data.rejected}
    };
    j["difficulty"] = data.difficulty;
    j["height"] = data.height;

    json cpu;
    cpu["model"] = cpuModelName;
    cpu["threads"] = data.cpuThreads;
    cpu["hashrate_hs"] = data.cpuHashrate;
    cpu["temp_c"] = cpuTempAvailable ? json(cpuTempCelsius) : json(nullptr);
    cpu["usage_percent"] = cpuUsageAvailable ? json(cpuUsagePercent) : json(nullptr);
    cpu["power_w"] = cpuPowerAvailable ? json(cpuPowerWatts) : json(nullptr);
    j["cpu"] = cpu;

    json gpus = json::array();
    double totalGpuPower = 0.0;
    for(const auto &row : data.gpuRows)
    {
        const auto &g = row.stats;
        json gj;
        gj["index"] = g.index;
        gj["name"] = g.name;
        gj["hashrate_hs"] = row.hashrate;
        gj["temp_c"] = g.tempCelsius;
        gj["util_percent"] = g.utilPercent;
        gj["fan_percent"] = g.fanPercent;
        gj["power_w"] = g.powerWatts;
        gj["mem_used_mib"] = g.memUsedMiB;
        gj["mem_total_mib"] = g.memTotalMiB;
        gj["pci_bus_decimal"] = g.pciBusDecimal;
        gpus.push_back(gj);
        totalGpuPower += g.powerWatts;
    }
    j["gpus"] = gpus;

    double totalKnownPower = totalGpuPower;
    if(cpuPowerAvailable) totalKnownPower += cpuPowerWatts;
    j["power_gpu_w"] = totalGpuPower;
    j["power_at_wall_w"] = totalKnownPower * 1.20;

    // Ecriture atomique : on ecrit dans un fichier temporaire puis on
    // renomme, pour qu'un lecteur externe (h-stats.sh) ne tombe jamais
    // sur un fichier a moitie ecrit.
    std::ofstream out(kStatusPathTmp, std::ios::trunc);
    if(out)
    {
        out << j.dump();
        out.close();
        std::rename(kStatusPathTmp, kStatusPath);
    }
}
