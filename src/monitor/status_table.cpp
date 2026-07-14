#include "../version.h"
#include "status_table.h"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace
{
    constexpr const char* kRed    = "\033[31m";
    constexpr const char* kGreen  = "\033[32m";
    constexpr const char* kYellow = "\033[33m";
    constexpr const char* kCyan   = "\033[36m";
    constexpr const char* kBlue   = "\033[34m";
    constexpr const char* kBold   = "\033[1m";
    constexpr const char* kReset  = "\033[0m";

    std::string fmtTemp(double t)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision(1) << t << "C";
        return o.str();
    }

    std::string fmtPercent(double p)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision(1) << p << "%";
        return o.str();
    }

    std::string fmtLoad(double l)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision(2) << l;
        return o.str();
    }

    const std::string kSep =
        "+---------------------------------------------------------------------------------------------------------------+";
}

void printStatusTable(const DashboardData& data)
{
    CpuStats cpu = SystemMonitor::readCpuStats();
    LoadAverage la = SystemMonitor::readLoadAverage();

    std::cout << "\n" << kBold << kCyan << " KZMiner " << KZMinerInfo::kVersion << " - " << KZMinerInfo::kTagline << " " << kReset << "\n";
    std::cout << kCyan << kSep << "\n" << kReset;

    std::cout
        << kBold << "HASHRATE" << kReset << " " << kRed << data.totalHashrate << " H/s" << kReset
        << "  |  " << kBold << "SHARES" << kReset << " " << kGreen << data.shares << kReset
        << "  |  " << kBold << "DIFFICULTY" << kReset << " " << kYellow << data.difficulty << kReset
        << "  |  " << kBold << "HEIGHT" << kReset << " " << data.height
        << "\n";

    std::cout
        << kBold << "LOAD AVG" << kReset << " "
        << (la.available ? (fmtLoad(la.load1) + " " + fmtLoad(la.load5) + " " + fmtLoad(la.load15)) : std::string("N/A"))
        << "  |  " << kBold << "CPU TEMP" << kReset << " " << (cpu.tempAvailable ? fmtTemp(cpu.tempCelsius) : std::string("N/A"))
        << "  |  " << kBold << "CPU USAGE" << kReset << " " << (cpu.usageAvailable ? fmtPercent(cpu.usagePercent) : std::string("N/A"))
        << "\n";

    std::cout << kCyan << kSep << "\n" << kReset;

    if(data.cpuThreads > 0)
    {
        std::string cpuNamePadded = cpu.modelName.empty()
            ? std::string("Unknown CPU")
            : cpu.modelName;
        if(cpuNamePadded.size() > 28) cpuNamePadded = cpuNamePadded.substr(0, 28);
        cpuNamePadded.resize(28, ' ');

        std::cout
            << kBold << " CPU " << kReset << "| "
            << kBlue << cpuNamePadded << kReset << " | "
            << std::left << std::setw(10) << (std::to_string(data.cpuThreads) + " threads")
            << " | " << std::left << std::setw(12) << (std::to_string(data.cpuHashrate) + " H/s")
            << " | temp " << std::left << std::setw(7) << (cpu.tempAvailable ? fmtTemp(cpu.tempCelsius) : std::string("N/A"))
            << " | usage " << (cpu.usageAvailable ? fmtPercent(cpu.usagePercent) : std::string("N/A"))
            << "\n";
        std::cout << kCyan << kSep << "\n" << kReset;
    }

    if(!data.gpuRows.empty())
    {
        std::cout
            << kCyan
            << " #   | GPU                          | Hashrate      | Temp     | Util   | VRAM                | Fan    | Power   \n"
            << kSep << "\n"
            << kReset;

        for(const auto &row : data.gpuRows)
        {
            const auto &g = row.stats;

            std::ostringstream vram;
            vram << static_cast<int>(g.memUsedMiB) << "/" << static_cast<int>(g.memTotalMiB) << "MiB";

            std::ostringstream fan;
            if(g.fanPercent >= 0) fan << g.fanPercent << "%"; else fan << "N/A";

            std::cout
                << " " << std::left << std::setw(4) << g.index << "| "
                << kBlue << [&]{ std::string n = g.name.substr(0, 28); n.resize(28, ' '); return n; }() << kReset << " | "
                << std::left << std::setw(13) << (std::to_string(row.hashrate) + " H/s") << " | "
                << std::left << std::setw(9) << fmtTemp(g.tempCelsius) << "| "
                << std::left << std::setw(7) << (std::to_string(g.utilPercent) + "%") << "| "
                << std::left << std::setw(20) << vram.str() << "| "
                << std::left << std::setw(7) << fan.str() << "| "
                << std::left << std::setw(8) << (std::to_string(static_cast<int>(g.powerWatts)) + "W")
                << "\n";
        }

        std::cout << kCyan << kSep << "\n" << kReset;
    }

    double totalPower = 0.0;
    for(const auto &row : data.gpuRows) totalPower += row.stats.powerWatts;

    int activeGpus = static_cast<int>(data.gpuRows.size());

    std::cout
        << kBold << "TOTAL" << kReset
        << " " << data.totalHashrate << " H/s"
        << "  |  POWER (GPU) " << static_cast<int>(totalPower) << " W"
        << "  |  ACTIVE " << (data.cpuThreads > 0 ? 1 : 0) << " CPU + " << activeGpus << " GPU"
        << "\n";

    std::cout << kCyan << kSep << kReset << "\n";
}
