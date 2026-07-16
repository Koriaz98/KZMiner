#include "../version.h"
#include "../console_lock.h"
#include "status_table.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <unistd.h>
#include <sys/ioctl.h>

namespace
{
    constexpr const char* kRed    = "\033[31m";
    constexpr const char* kGreen  = "\033[32m";
    constexpr const char* kYellow = "\033[33m";
    constexpr const char* kCyan   = "\033[36m";
    constexpr const char* kBlue   = "\033[34m";
    constexpr const char* kBold   = "\033[1m";
    constexpr const char* kReset  = "\033[0m";

    constexpr double kGpuTempWarnCelsius = 72.0;
    constexpr double kAtWallOverheadFraction = 0.20;

    std::string fmtTemp(double t)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision(1) << t << "C";
        return o.str();
    }

    std::string fmtGpuTemp(double t)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision(1) << t << "C";
        std::string s = o.str();
        if(t >= kGpuTempWarnCelsius)
        {
            return std::string(kRed) + s + kReset;
        }
        return s;
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

    std::string buildDashboardText(const DashboardData& data)
    {
        CpuStats cpu = SystemMonitor::readCpuStats();
        LoadAverage la = SystemMonitor::readLoadAverage();

        std::ostringstream out;

        out << kBold << kCyan << " KZMiner " << KZMinerInfo::kVersion << " - " << KZMinerInfo::kTagline << " " << kReset << "\n";
        out << kCyan << kSep << "\n" << kReset;

        out
            << kBold << "HASHRATE" << kReset << " " << kGreen << data.totalHashrate << " H/s" << kReset
            << "  |  " << kBold << "SHARES" << kReset << " " << kGreen << data.shares << kReset
            << " (" << kGreen << data.accepted << " accepted" << kReset
            << ", " << kRed << data.rejected << " rejected" << kReset << ")"
            << "  |  " << kBold << "DIFFICULTY" << kReset << " " << kYellow << data.difficulty << kReset
            << "  |  " << kBold << "HEIGHT" << kReset << " " << kYellow << data.height << kReset
            << "\n";

        int activeGpus = static_cast<int>(data.gpuRows.size());

        out
            << kBold << "LOAD AVG" << kReset << " "
            << (la.available ? (fmtLoad(la.load1) + " " + fmtLoad(la.load5) + " " + fmtLoad(la.load15)) : std::string("N/A"))
            << "  |  " << kBold << "CPU TEMP" << kReset << " " << (cpu.tempAvailable ? fmtTemp(cpu.tempCelsius) : std::string("N/A"))
            << "  |  " << kBold << "CPU USAGE" << kReset << " " << (cpu.usageAvailable ? fmtPercent(cpu.usagePercent) : std::string("N/A"))
            << "  |  " << kBold << "ACTIVE" << kReset << " " << (data.cpuThreads > 0 ? 1 : 0) << " CPU + " << activeGpus << " GPU"
            << "\n";

        out << kCyan << kSep << "\n" << kReset;

        if(data.cpuThreads > 0)
        {
            std::string cpuNamePadded = cpu.modelName.empty()
                ? std::string("Unknown CPU")
                : cpu.modelName;
            if(cpuNamePadded.size() > 28) cpuNamePadded = cpuNamePadded.substr(0, 28);
            cpuNamePadded.resize(28, ' ');

            out
                << kBold << " CPU " << kReset << "| "
                << kBlue << cpuNamePadded << kReset << " | "
                << std::left << std::setw(10) << (std::to_string(data.cpuThreads) + " threads")
                << " | " << std::left << std::setw(12) << (std::to_string(data.cpuHashrate) + " H/s")
                << " | temp " << std::left << std::setw(7) << (cpu.tempAvailable ? fmtTemp(cpu.tempCelsius) : std::string("N/A"))
                << " | usage " << (cpu.usageAvailable ? fmtPercent(cpu.usagePercent) : std::string("N/A"))
                << "\n";
            out << kCyan << kSep << "\n" << kReset;
        }

        if(!data.gpuRows.empty())
        {
            out
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

                std::string tempStr = fmtGpuTemp(g.tempCelsius);
                std::string tempPlain = fmtTemp(g.tempCelsius);
                std::string tempPadded = tempStr;
                if(tempPlain.size() < 9)
                {
                    tempPadded += std::string(9 - tempPlain.size(), ' ');
                }

                out
                    << " " << std::left << std::setw(4) << g.index << "| "
                    << kBlue << [&]{ std::string n = g.name.substr(0, 28); n.resize(28, ' '); return n; }() << kReset << " | "
                    << std::left << std::setw(13) << (std::to_string(row.hashrate) + " H/s") << " | "
                    << tempPadded << "| "
                    << std::left << std::setw(7) << (std::to_string(g.utilPercent) + "%") << "| "
                    << std::left << std::setw(20) << vram.str() << "| "
                    << std::left << std::setw(7) << fan.str() << "| "
                    << std::left << std::setw(8) << (std::to_string(static_cast<int>(g.powerWatts)) + "W")
                    << "\n";
            }

            out << kCyan << kSep << "\n" << kReset;
        }

        double totalGpuPower = 0.0;
        for(const auto &row : data.gpuRows) totalGpuPower += row.stats.powerWatts;

        double totalKnownPower = totalGpuPower;
        if(cpu.powerAvailable) totalKnownPower += cpu.powerWatts;

        out
            << kBold << "TOTAL" << kReset
            << " " << kGreen << data.totalHashrate << " H/s" << kReset
            << "  |  POWER (GPU) " << static_cast<int>(totalGpuPower) << " W"
            << "  | POWER (CPU) " << (cpu.powerAvailable ? (std::to_string(static_cast<int>(cpu.powerWatts)) + "W") : std::string("N/A"))
            << "  |  TOTAL AT WALL (CPU + GPU + 20%) " << (u8"\u2248")
            << static_cast<int>(totalKnownPower * (1.0 + kAtWallOverheadFraction)) << "W"
            << "\n";

        out << kCyan << kSep << kReset << "\n";

        return out.str();
    }

    int countLines(const std::string& text)
    {
        int n = 0;
        for(char c : text) if(c == '\n') n++;
        return n;
    }
}

void printStatusTable(const DashboardData& data)
{
    std::lock_guard<std::mutex> lock(consoleMutex());

    static bool isTty = (isatty(STDOUT_FILENO) != 0);
    static bool firstCall = true;
    static int dashboardLines = 0;
    static int termRows = 50;

    std::string text = buildDashboardText(data);

    if(!isTty)
    {
        std::cout << "\n" << text << std::flush;
        return;
    }

    if(firstCall)
    {
        dashboardLines = countLines(text);

        struct winsize ws{};
        if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
        {
            termRows = ws.ws_row;
        }
        if(termRows <= dashboardLines + 2)
        {
            termRows = dashboardLines + 10;
        }

        // Repart d'un ecran completement vide, position connue et
        // fiable - les messages affiches avant ce premier appel (logo,
        // "Wallet: ...", etc.) resteront dans le scrollback du
        // terminal mais ne seront plus visibles une fois le panneau
        // actif, comme avec htop/nvtop/btop.
        std::cout << "\033[2J" << "\033[H";

        // Reserve les "dashboardLines" premieres lignes au panneau ;
        // le reste devient la zone de defilement des logs (DECSTBM).
        // Remonter dans l'historique du terminal peut etre limite
        // pendant que cette zone est active - comportement attendu de
        // ce type d'interface, pas un bug.
        std::cout << "\033[" << (dashboardLines + 1) << ";" << termRows << "r";
        std::cout << "\033[" << (dashboardLines + 1) << ";1H";

        firstCall = false;
    }

    std::cout << "\0337";
    std::cout << "\033[1;1H";

    // Efface et reecrit chaque ligne du panneau individuellement -
    // ne touche jamais la zone de logs en dessous.
    std::istringstream lineStream(text);
    std::string line;
    int lineCount = 0;
    while(std::getline(lineStream, line))
    {
        std::cout << "\033[2K" << line << "\n";
        lineCount++;
    }
    for(; lineCount < dashboardLines; lineCount++)
    {
        std::cout << "\033[2K" << "\n";
    }

    std::cout << "\0338";
    std::cout << std::flush;
}
