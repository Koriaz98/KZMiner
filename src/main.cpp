#include <string>
#include "update_check.h"
#include "version.h"
#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include <sys/ioctl.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <vector>
#include "config/config.h"
#include "cpu/cpu_miner.h"
#include "gpu/gpu_miner.h"
#include "network/solo_job_manager.h"
#include "network/pool_job_manager.h"
#include "devfee/devfee_source.h"
#include "devfee/devfee_config.h"
#include "monitor/status_table.h"
#include "console_lock.h"
#include "console_output.h"
#include "coins/btc09/btc09_params.h"
#include "status_json.h"
namespace
{
    constexpr const char* kCyan   = "\033[36m";
    constexpr const char* kBlue   = "\033[34m";
    constexpr const char* kYellow = "\033[33m";
    constexpr const char* kRed    = "\033[31m";
    constexpr const char* kGreen  = "\033[32m";
    constexpr const char* kReset  = "\033[0m";
    void printLogo()
    {
        std::cout << "\n\n\n";
        std::cout << kBlue << R"(
#    # ####### #     #                        
#   #       #  ##   ## # #    # ###### #####  
#  #       #   # # # # # ##   # #      #    # 
###       #    #  #  # # # #  # #####  #    # 
#  #     #     #     # # #  # # #      #####  
#   #   #      #     # # #   ## #      #   #  
#    # ####### #     # # #    # ###### #    # 
)" << kReset;
        std::cout << "\n";
        std::cout << "        " << KZMinerInfo::kTagline << "\n";
        std::cout << "        KZMiner " << KZMinerInfo::kVersion << "\n";
        std::cout << "        Happy mining!\n\n";
    }
}

static bool parseHostPort(const std::string& s, std::string& host, int& port)
{
    auto pos = s.rfind(':');
    if(pos == std::string::npos) return false;
    host = s.substr(0, pos);
    try { port = std::stoi(s.substr(pos + 1)); }
    catch(...) { return false; }
    return true;
}

static void resolveWalletAndWorker(
    const std::string& rawWallet,
    const std::string& explicitWorker,
    std::string& outAddress,
    std::string& outWorker
)
{
    outAddress = rawWallet;
    outWorker = "default";

    auto dotPos = rawWallet.find('.');
    if(dotPos != std::string::npos)
    {
        outAddress = rawWallet.substr(0, dotPos);
        outWorker = rawWallet.substr(dotPos + 1);
    }

    if(!explicitWorker.empty())
    {
        outWorker = explicitWorker;
    }
}

namespace
{
    void restoreCursorAndExit(int)
    {
        // Avant de quitter : repositionne le curseur tout en bas du
        // terminal (pas juste le rendre visible) et efface le reste
        // en dessous - sinon le curseur reste exactement ou l'a
        // laisse le dernier rafraichissement differentiel du panneau
        // (potentiellement au milieu de l'ecran), et le prochain
        // prompt du shell s'imprime alors par-dessus le contenu deja
        // affiche a cet endroit, melangeant les deux visuellement.
        struct winsize ws{};
        int termRows = 24;
        if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
        {
            termRows = ws.ws_row;
        }
        std::cout << "\033[" << termRows << ";1H" << "\033[?25h" << "\n" << std::flush;
        std::signal(SIGINT, SIG_DFL);
        std::signal(SIGTERM, SIG_DFL);
        std::raise(SIGINT);
    }
}

int main(int argc, char **argv)
{
    setenv("CUDA_DEVICE_ORDER", "PCI_BUS_ID", 1);
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, restoreCursorAndExit);
    std::signal(SIGTERM, restoreCursorAndExit);

    MinerConfig config = ConfigParser::parse(argc, argv);

    printLogo();

    std::cout
        << kYellow << "To check the latest version of KZMiner, go to https://github.com/"
        << KZMinerInfo::kRepo << "/releases" << kReset << "\n\n";

    checkForUpdate();

    for(int i = 5; i > 0; i--)
    {
        std::cout << "\rStarting in " << i << "s... " << std::flush;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "\r                              \r";

    std::cout << "Algorithm: " << kRed << "Argon2id" << kReset << "\n";
    std::cout << "Mode: " << config.mode << "\n";
    std::cout
        << "CPU: " << (config.cpuEnabled ? (std::string(kGreen) + "ON" + kReset) : (std::string(kRed) + "OFF" + kReset))
        << " | GPU: " << (config.gpuEnabled ? (std::string(kGreen) + "ON" + kReset) : (std::string(kRed) + "OFF" + kReset))
        << "\n\n";

    if(config.pool.empty())
    {
        std::cerr << "Error: no pool specified (use -o)\n";
        return 1;
    }
    if(config.wallet.empty())
    {
        std::cerr << "Error: no wallet address specified (use -u)\n";
        return 1;
    }

    std::string walletAddress, workerName;
    resolveWalletAndWorker(config.wallet, config.workerName, walletAddress, workerName);

    std::cout << "Wallet: " << walletAddress << "\n";
    std::cout << "Rig/worker name: " << workerName << "\n\n";

    std::unique_ptr<MiningSource> userSource;
    std::unique_ptr<MiningSource> devSource;

    if(config.mode == "solo")
    {
        std::cout << "Pool: " << config.pool << " (solo, Open Mining Protocol v1)\n";
        userSource = std::make_unique<SoloJobManager>(config.pool, walletAddress, workerName);
        // Intervalle de sondage bien plus long pour le dev fee (60s au
        // lieu de 10s) : il n'est reellement utilise que 1% du temps,
        // pas besoin d'un job aussi frais que celui de l'utilisateur -
        // reduit significativement le volume de requetes cumule vers
        // le coordinateur.
        devSource = std::make_unique<SoloJobManager>(config.pool, DevFeeConfig::kDevWallet, workerName + "-devfee", 60);
    }
    else if(config.mode == "pool")
    {
        std::string host;
        int port = 0;
        if(!parseHostPort(config.pool, host, port))
        {
            std::cerr << "Error: --mode pool requires -o host:port (got '" << config.pool << "')\n";
            return 1;
        }
        std::cout << "Pool: " << host << ":" << port << " (third-party pool, unofficial protocol)\n";
        userSource = std::make_unique<PoolJobManager>(host, port, walletAddress, workerName);
        devSource = std::make_unique<PoolJobManager>(host, port, DevFeeConfig::kDevWallet, workerName + "-devfee");
    }
    else
    {
        std::cerr << "Error: unknown --mode '" << config.mode << "' (use 'solo' or 'pool')\n";
        return 1;
    }

    auto source = std::make_unique<DevFeeSource>(
        std::move(userSource),
        std::move(devSource),
        DevFeeConfig::kFeePercent,
        DevFeeConfig::kCycleSeconds
    );
    source->start();

    int cpuThreads = 0;
    if(config.cpuEnabled)
    {
        cpuThreads = config.cpuThreads;
        if(cpuThreads <= 0)
        {
            cpuThreads = std::thread::hardware_concurrency();
            if(cpuThreads <= 0) cpuThreads = 1;
        }
    }

    int gpuDeviceCount = 0;
    if(config.gpuEnabled)
    {
        GpuMiner probe(source.get(), config.intensity, 0, 1);
        gpuDeviceCount = probe.getDeviceCount();
    }

    int totalWorkers = cpuThreads + gpuDeviceCount;
    if(totalWorkers == 0)
    {
        std::cerr << "Error: no worker enabled (need --cpu and/or --gpu)\n";
        return 1;
    }

    auto algorithm = makeBtc09Algorithm();

    std::unique_ptr<CPUMiner> cpuMiner;
    if(config.cpuEnabled)
    {
        cpuMiner = std::make_unique<CPUMiner>(source.get(), algorithm.get(), cpuThreads, 0, totalWorkers);
        cpuMiner->launchWorkers();
    }

    std::unique_ptr<GpuMiner> gpuMiner;
    if(config.gpuEnabled)
    {
        gpuMiner = std::make_unique<GpuMiner>(source.get(), config.intensity, cpuThreads, totalWorkers);
        gpuMiner->launchWorkers();
    }

    std::vector<uint64_t> previousGpuHashes(gpuDeviceCount, 0);
    uint64_t previousCpuHashes = 0;
    uint64_t previousAccepted = 0;

    auto lastRateTime = std::chrono::steady_clock::now();
    auto programStartTime = std::chrono::steady_clock::now();
    bool firstIteration = true;

    // dernieres valeurs connues, reaffichees telles quelles entre deux
    // recalculs de hashrate (toutes les 10s), pour rafraichir le
    // panneau plus souvent (toutes les 2s) sans rendre le chiffre de
    // hashrate instable/bruite sur une trop courte fenetre.
    DashboardData lastDashboard;

    while(true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        auto now = std::chrono::steady_clock::now();
        double sinceLastRate = std::chrono::duration<double>(now - lastRateTime).count();

        bool recomputeRate = firstIteration || sinceLastRate >= 10.0;

        uint64_t shares = 0;
        uint64_t cpuRate = lastDashboard.cpuHashrate;
        std::vector<GpuRow> gpuRows = lastDashboard.gpuRows;

        if(recomputeRate)
        {
            double elapsed = sinceLastRate;
            if(elapsed < 0.1) elapsed = 0.1;

            uint64_t currCpu = 0;
            if(cpuMiner)
            {
                currCpu = cpuMiner->getHashes();
                shares += cpuMiner->getShares();
            }
            cpuRate = static_cast<uint64_t>((currCpu - previousCpuHashes) / elapsed);
            previousCpuHashes = currCpu;

            gpuRows.clear();
            if(gpuMiner)
            {
                shares += gpuMiner->getShares();
                std::vector<GpuStats> stats = SystemMonitor::readGpuStats();
                for(const auto &s : stats)
                {
                    uint64_t curr = gpuMiner->getDeviceHashes(s.index);
                    uint64_t prev = (s.index < static_cast<int>(previousGpuHashes.size()))
                        ? previousGpuHashes[s.index] : 0;
                    GpuRow row;
                    row.stats = s;
                    row.hashrate = static_cast<uint64_t>((curr - prev) / elapsed);
                    gpuRows.push_back(row);
                    if(s.index < static_cast<int>(previousGpuHashes.size()))
                    {
                        previousGpuHashes[s.index] = curr;
                    }
                }
            }

            lastRateTime = now;
            firstIteration = false;
        }
        else
        {
            // Entre deux recalculs : on garde le hashrate/GPU-hashrate
            // stables, mais on relit quand meme les stats instantanees
            // (temp/util/VRAM/fan/power) pour que le panneau "respire".
            if(cpuMiner) shares += cpuMiner->getShares();
            if(gpuMiner)
            {
                shares += gpuMiner->getShares();
                std::vector<GpuStats> stats = SystemMonitor::readGpuStats();
                for(size_t i = 0; i < gpuRows.size() && i < stats.size(); i++)
                {
                    gpuRows[i].stats = stats[i];
                }
            }
        }

        MiningJob job = source->getJob();

        DashboardData dashboard;
        dashboard.totalHashrate = cpuRate;
        for(const auto &row : gpuRows) dashboard.totalHashrate += row.hashrate;
        dashboard.shares = shares;
        dashboard.accepted = source->getAcceptedCount();
        dashboard.rejected = source->getRejectedCount();
        dashboard.difficulty = job.difficulty;
        dashboard.height = job.height;
        dashboard.cpuThreads = cpuMiner ? cpuMiner->getThreadCount() : 0;
        dashboard.cpuHashrate = cpuRate;
        dashboard.gpuRows = gpuRows;
        dashboard.algoName = algorithm->name();
        dashboard.walletAddress = walletAddress;
        dashboard.poolAddress = config.pool;
        dashboard.mode = config.mode;

        if(dashboard.accepted > previousAccepted)
        {
            uint64_t newlyAccepted = dashboard.accepted - previousAccepted;
            pushLogLine(
                "\033[32m[shares] +" + std::to_string(newlyAccepted)
                + " accepted (" + std::to_string(dashboard.accepted) + " total)\033[0m"
            );
        }
        previousAccepted = dashboard.accepted;

        printStatusTable(dashboard);

        {
            CpuStats cpuStatsSnapshot = SystemMonitor::readCpuStats();
            uint64_t uptimeSeconds = static_cast<uint64_t>(
                std::chrono::duration<double>(now - programStartTime).count()
            );
            writeStatusJson(
                dashboard,
                cpuStatsSnapshot.tempCelsius, cpuStatsSnapshot.tempAvailable,
                cpuStatsSnapshot.usagePercent, cpuStatsSnapshot.usageAvailable,
                cpuStatsSnapshot.powerWatts, cpuStatsSnapshot.powerAvailable,
                cpuStatsSnapshot.modelName,
                uptimeSeconds,
                KZMinerInfo::kVersion
            );
        }

        lastDashboard = dashboard;
    }

    return 0;
}
