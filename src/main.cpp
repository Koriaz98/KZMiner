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
#include <deque>
#include <algorithm>
#include "config/config.h"
#include "cpu/cpu_miner.h"
#include "gpu/gpu_miner.h"
#include "network/solo_job_manager.h"
#include "network/pool_job_manager.h"
#include "network/blocknet_pool_job_manager.h"
#include "devfee/devfee_source.h"
#include "devfee/devfee_config.h"
#include "monitor/status_table.h"
#include "console_lock.h"
#include "console_output.h"
#include "algo/algorithm.h"
#include "coins/btc09/btc09_params.h"
#include "coins/blocknet/blocknet_params.h"
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

    if(config.algo == "argon2id-bnt")
    {
        // Blocknet : seul le mode pool officiel est implemente pour
        // l'instant (pas de solo/daemon), --mode est donc ignore pour
        // cet algorithme. Meme convention -o host:port que le pool
        // tiers BTC09, pour rester coherent au sein de KZMiner plutot
        // que de reproduire le format d'URL propre au client officiel
        // Blocknet (stratum+tcp://...). Adresse officielle confirmee :
        // bntpool.com:3333 (https://bntpool.com/start).
        std::string host;
        int port = 0;
        if(!parseHostPort(config.pool, host, port))
        {
            std::cerr << "Error: --algo argon2id-bnt requires -o host:port (got '" << config.pool << "')\n";
            return 1;
        }
        std::cout << "Pool: " << host << ":" << port << " (Blocknet official pool protocol)\n";
        userSource = std::make_unique<BlocknetPoolJobManager>(host, port, walletAddress, workerName);
        devSource = std::make_unique<BlocknetPoolJobManager>(host, port, DevFeeConfig::kDevWalletBlocknet, workerName + "-devfee");
    }
    else if(config.mode == "solo")
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

    std::unique_ptr<Algorithm> algorithm;
    if(config.algo == "argon2id-bnt")
    {
        algorithm = makeBlocknetAlgorithm();
    }
    else if(config.algo == "argon2id-09c")
    {
        algorithm = makeBtc09Algorithm();
    }
    else
    {
        std::cerr << "Error: unknown --algo '" << config.algo << "' (use 'argon2id-09c' or 'argon2id-bnt')\n";
        return 1;
    }

    int gpuDeviceCount = 0;
    if(config.gpuEnabled)
    {
        GpuMiner probe(source.get(), algorithm.get(), config.intensity, 0, 1);
        gpuDeviceCount = probe.getDeviceCount();
    }

    int totalWorkers = cpuThreads + gpuDeviceCount;
    if(totalWorkers == 0)
    {
        std::cerr << "Error: no worker enabled (need --cpu and/or --gpu)\n";
        return 1;
    }

    std::unique_ptr<CPUMiner> cpuMiner;
    if(config.cpuEnabled)
    {
        cpuMiner = std::make_unique<CPUMiner>(source.get(), algorithm.get(), cpuThreads, 0, totalWorkers);
        cpuMiner->launchWorkers();
    }

    std::unique_ptr<GpuMiner> gpuMiner;
    if(config.gpuEnabled)
    {
        gpuMiner = std::make_unique<GpuMiner>(source.get(), algorithm.get(), config.intensity, cpuThreads, totalWorkers);
        gpuMiner->launchWorkers();
    }

    std::vector<uint64_t> previousGpuHashes(gpuDeviceCount, 0);
    std::vector<std::chrono::steady_clock::time_point> lastGpuChangeTime(
        gpuDeviceCount, std::chrono::steady_clock::now()
    );
    // Fenetre temporelle glissante par GPU : on garde des couples
    // (temps, compteur cumule de hachages) sur ~10s, et on calcule le
    // taux comme (hachages sur la fenetre) / (temps reel de la fenetre).
    // C'est le vrai debit moyen (throughput_hps de seine) : stable ET
    // fidele a la charge reelle (donc coherent avec la puissance
    // consommee), contrairement a une mesure entre deux tours qui, sur
    // des lots, sous-estime ou oscille.
    const double kRateWindowSeconds = 10.0;
    struct RateSample { std::chrono::steady_clock::time_point t; uint64_t hashes; };
    std::vector<std::deque<RateSample>> gpuRateWindow(gpuDeviceCount);
    uint64_t previousCpuHashes = 0;
    auto lastCpuChangeTime = std::chrono::steady_clock::now();
    uint64_t previousAccepted = 0;

    auto programStartTime = std::chrono::steady_clock::now();

    // dernieres valeurs connues, reaffichees telles quelles tant que
    // le nombre de hachages n'a pas change, pour rafraichir le
    // panneau souvent (toutes les 2s) sans rendre le chiffre de
    // hashrate instable/bruite. Le taux n'est recalcule QUE lorsque
    // le compteur de hachages progresse reellement (pas sur une
    // fenetre de temps fixe) - un algorithme lent comme Blocknet peut
    // mettre plus de 10s a terminer un seul lot, ce qui ferait sinon
    // afficher 0 H/s a tort malgre un vrai travail en cours.
    DashboardData lastDashboard;

    while(true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        auto now = std::chrono::steady_clock::now();

        uint64_t shares = 0;
        double cpuRate = lastDashboard.cpuHashrate;
        std::vector<GpuRow> gpuRows = lastDashboard.gpuRows;

        if(cpuMiner)
        {
            uint64_t currCpu = cpuMiner->getHashes();
            shares += cpuMiner->getShares();
            if(currCpu != previousCpuHashes)
            {
                double elapsed = std::chrono::duration<double>(now - lastCpuChangeTime).count();
                if(elapsed < 0.1) elapsed = 0.1;
                cpuRate = static_cast<double>(currCpu - previousCpuHashes) / elapsed;
                previousCpuHashes = currCpu;
                lastCpuChangeTime = now;
            }
        }

        if(gpuMiner)
        {
            shares += gpuMiner->getShares();
            std::vector<GpuStats> stats = SystemMonitor::readGpuStats();
            if(gpuRows.size() != stats.size()) gpuRows.resize(stats.size());
            // On utilise la POSITION dans la liste (i), pas l'etiquette
            // nvidia-smi (stats[i].index), pour retrouver le hashrate
            // interne. Ces deux numerotations divergent des qu'un GPU
            // est absent/casse (nvidia-smi "saute" son numero, alors
            // que CUDA renumerote sans trou) ou que
            // CUDA_VISIBLE_DEVICES restreint les cartes visibles -
            // situations courantes en minage multi-GPU. Comme KZMiner
            // force CUDA_DEVICE_ORDER=PCI_BUS_ID en interne (voir plus
            // haut), l'ORDRE de la liste nvidia-smi correspond bien a
            // l'ordre interne CUDA, seule l'ETIQUETTE numerique differe -
            // d'ou l'utilisation de la position plutot que l'etiquette.
            for(size_t i = 0; i < stats.size(); i++)
            {
                gpuRows[i].stats = stats[i];

                uint64_t curr = gpuMiner->getDeviceHashes(static_cast<int>(i));
                uint64_t prevHash = (i < previousGpuHashes.size())
                    ? previousGpuHashes[i] : 0;
                if(i < gpuRateWindow.size())
                {
                    // On enregistre le point courant (temps, compteur cumule).
                    gpuRateWindow[i].push_back({now, curr});
                    // On purge les points plus vieux que la fenetre, mais on
                    // garde toujours au moins le plus ancien point encore utile
                    // pour avoir une base de comparaison des le debut.
                    while(gpuRateWindow[i].size() > 2)
                    {
                        double age = std::chrono::duration<double>(
                            now - gpuRateWindow[i].front().t).count();
                        double ageNext = std::chrono::duration<double>(
                            now - gpuRateWindow[i][1].t).count();
                        // On retire le plus ancien seulement si le suivant
                        // couvre encore toute la fenetre (evite de trop
                        // raccourcir la base de mesure).
                        if(age > kRateWindowSeconds && ageNext >= kRateWindowSeconds)
                            gpuRateWindow[i].pop_front();
                        else
                            break;
                    }
                    // Taux = hachages accumules sur la fenetre / temps reel
                    // ecoule entre le plus ancien point et maintenant.
                    const auto& oldest = gpuRateWindow[i].front();
                    double windowElapsed = std::chrono::duration<double>(
                        now - oldest.t).count();
                    if(windowElapsed >= 0.5 && curr >= oldest.hashes)
                    {
                        gpuRows[i].hashrate =
                            static_cast<double>(curr - oldest.hashes) / windowElapsed;
                    }
                    previousGpuHashes[i] = curr;
                    lastGpuChangeTime[i] = now;
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
        dashboard.workerName = workerName;
        dashboard.uptimeSeconds = static_cast<uint64_t>(
            std::chrono::duration<double>(now - programStartTime).count()
        );

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
            writeStatusJson(
                dashboard,
                cpuStatsSnapshot.tempCelsius, cpuStatsSnapshot.tempAvailable,
                cpuStatsSnapshot.usagePercent, cpuStatsSnapshot.usageAvailable,
                cpuStatsSnapshot.powerWatts, cpuStatsSnapshot.powerAvailable,
                cpuStatsSnapshot.modelName,
                dashboard.uptimeSeconds,
                KZMinerInfo::kVersion
            );
        }

        lastDashboard = dashboard;
    }

    return 0;
}
