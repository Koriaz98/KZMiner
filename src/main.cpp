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
#include <sstream>
#include <iomanip>
#include <algorithm>
#include "config/config.h"
#include "cpu/cpu_miner.h"
#include "gpu/gpu_miner.h"
#include "gpu/gpu_device_info.h"
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
    volatile std::sig_atomic_t g_stopRequested = 0;

    // Handler de signal STRICTEMENT async-signal-safe : il ne fait que
    // poser un flag (ecriture d'un volatile sig_atomic_t, la seule
    // operation garantie sure dans un handler). Tout le reste (curseur,
    // arret des workers, cudaFree) se fait dans main(), en contexte
    // normal. Faire du std::cout ici deadlockait sur le verrou de stdout
    // tenu par un thread interrompu, figeant le process -> contextes CUDA
    // jamais liberes -> kernels orphelins, GPU intuables, reboot force.
    // 2e signal (l'utilisateur reappuie, croyant que ca ne repond pas) ->
    // sortie dure immediate pour ne JAMAIS rester bloque (_exit est
    // async-signal-safe, contrairement a exit()).
    void requestStop(int)
    {
        if(g_stopRequested) _exit(130);
        g_stopRequested = 1;
    }

    // Repositionne le curseur tout en bas du terminal et le rend visible,
    // pour que le prochain prompt du shell ne s'imprime pas par-dessus le
    // panneau. Appele depuis main() en contexte NORMAL au moment de
    // l'arret propre - JAMAIS depuis un handler (std::cout n'est pas
    // async-signal-safe).
    void restoreCursor()
    {
        struct winsize ws{};
        int termRows = 24;
        if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
        {
            termRows = ws.ws_row;
        }
        std::cout << "\033[" << termRows << ";1H" << "\033[?25h" << "\n" << std::flush;
    }
}

int main(int argc, char **argv)
{
    setenv("CUDA_DEVICE_ORDER", "PCI_BUS_ID", 1);
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);

    MinerConfig config = ConfigParser::parse(argc, argv);

    printLogo();

    std::cout
        << kYellow << "To check the latest version of KZMiner, go to https://github.com/"
        << KZMinerInfo::kRepo << "/releases" << kReset << "\n\n";

    checkForUpdate();

    for(int i = 5; i > 0 && !g_stopRequested; i--)
    {
        std::cout << "\rStarting in " << i << "s... " << std::flush;
        // Sleep fractionne : un Ctrl+C pendant le compte a rebours doit
        // sortir tout de suite, sans lancer inutilement les workers.
        for(int s = 0; s < 10 && !g_stopRequested; s++)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "\r                              \r";
    if(g_stopRequested) return 0;  // arret avant tout lancement de worker

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

    // Releve une seule fois : la topologie PCI ne change pas en cours
    // d'execution. Indexe par index CUDA, c'est ce qui permet de
    // rattacher chaque compteur de hachages a la bonne carte physique.
    std::vector<CudaPciIdentity> gpuPciIds;
    if(config.gpuEnabled) gpuPciIds = cudaDeviceIdentities();

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

        // Trace la correspondance retenue : c'est elle qui determine sur
        // quelle ligne du tableau atterrit chaque hashrate, et c'est la
        // seule facon de verifier depuis les logs que le sous-ensemble
        // de cartes selectionne par CUDA_VISIBLE_DEVICES est bien celui
        // attendu. On affiche l'adresse PCI complete ET les deux bases du
        // bus : nvidia-smi rapporte cette adresse en hexadecimal, la
        // valeur decimale est celle utilisee en interne comme cle de
        // jointure (et comme "bus_numbers" cote HiveOS) - les donner
        // toutes les deux evite toute conversion mentale au moment de
        // confronter ce log a `nvidia-smi --query-gpu=index,pci.bus_id`.
        for(int d = 0; d < gpuDeviceCount; d++)
        {
            std::ostringstream oss;
            oss << "GPU CUDA " << d << " -> PCI ";
            if(d < static_cast<int>(gpuPciIds.size()) && gpuPciIds[d].busDecimal >= 0)
            {
                oss << gpuPciIds[d].pciBusId
                    << " (bus 0x" << std::hex << std::setw(2) << std::setfill('0')
                    << gpuPciIds[d].busDecimal << std::dec
                    << " = " << gpuPciIds[d].busDecimal << " decimal)";
            }
            else
            {
                oss << "unknown (adresse PCI illisible, telemetrie NVML "
                       "indisponible pour cette carte)";
            }
            pushLogLine(oss.str());
        }
    }

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

    // Passe a true si une source signale une erreur fatale (ex. login user
    // rejete de maniere repetee) : declenche l'arret propre et un code de
    // sortie non nul.
    bool fatalExit = false;

    while(!g_stopRequested)
    {
        // Rafraichissement ~2s, mais fractionne pour reagir a un signal
        // d'arret en <200ms (sinon l'utilisateur, croyant que ca ne
        // repond pas, reappuie sur Ctrl+C).
        for(int i = 0; i < 20 && !g_stopRequested; i++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if(g_stopRequested) break;

        // Erreur fatale cote source (login user durablement rejete) : on
        // sort de la boucle vers le meme arret propre que Ctrl+C, plutot que
        // de continuer a miner en silence sur le wallet dev. La source a deja
        // journalise le motif (voir LoginFatalPolicy).
        if(source->hasFatalError())
        {
            fatalExit = true;
            break;
        }

        auto now = std::chrono::steady_clock::now();

        double cpuRate = lastDashboard.cpuHashrate;
        std::vector<GpuRow> gpuRows = lastDashboard.gpuRows;

        if(cpuMiner)
        {
            uint64_t currCpu = cpuMiner->getHashes();
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

            // Jointure par BUS PCI, jamais par index ni par position.
            // nvidia-smi n'honore pas CUDA_VISIBLE_DEVICES : il liste
            // toutes les cartes physiques de la machine, avec leurs
            // numeros d'origine (1, 3, 4...), alors que CUDA ne voit que
            // les cartes autorisees et les renumerote sans trou
            // (0, 1, 2...). Les DEUX numerotations divergent donc, y
            // compris l'ordre - joindre par position collait le hashrate
            // d'une carte sur la ligne d'une autre, et laissait les
            // cartes non minees a 0.0 H/s dans le tableau. Le bus PCI
            // est le seul identifiant commun (KZMiner force
            // CUDA_DEVICE_ORDER=PCI_BUS_ID, voir plus haut).
            std::vector<GpuStats> stats = SystemMonitor::readGpuStats();

            // Une ligne par device CUDA, et uniquement ceux-la : les
            // cartes que ce process ne mine pas n'ont rien a faire ici.
            if(static_cast<int>(gpuRows.size()) != gpuDeviceCount)
            {
                gpuRows.assign(static_cast<size_t>(gpuDeviceCount), GpuRow{});
            }

            for(int d = 0; d < gpuDeviceCount; d++)
            {
                GpuRow &row = gpuRows[static_cast<size_t>(d)];
                row.cudaIndex = d;
                row.pciBusDecimal = (d < static_cast<int>(gpuPciIds.size()))
                    ? gpuPciIds[static_cast<size_t>(d)].busDecimal : -1;

                row.telemetryAvailable = false;
                if(row.pciBusDecimal >= 0)
                {
                    for(const auto &s : stats)
                    {
                        if(s.pciBusDecimal == row.pciBusDecimal)
                        {
                            row.stats = s;
                            row.telemetryAvailable = true;
                            break;
                        }
                    }
                }

                if(!row.telemetryAvailable)
                {
                    // Pas de correspondance NVML : on garde quand meme
                    // la ligne (son hashrate vient de CUDA et reste
                    // juste), avec un repli sur l'index CUDA comme
                    // numero affiche.
                    row.stats = GpuStats{};
                    row.stats.index = d;
                }
                if(row.pciBusDecimal >= 0)
                {
                    // Aussi utilise comme "bus_numbers" par HiveOS via
                    // status_json / h-stats.sh, d'ou le renseignement
                    // meme sans telemetrie.
                    row.stats.pciBusDecimal = row.pciBusDecimal;
                }

                uint64_t curr = gpuMiner->getDeviceHashes(d);
                size_t w = static_cast<size_t>(d);
                if(w < gpuRateWindow.size())
                {
                    // On enregistre le point courant (temps, compteur cumule).
                    gpuRateWindow[w].push_back({now, curr});
                    // On purge les points plus vieux que la fenetre, mais on
                    // garde toujours au moins le plus ancien point encore utile
                    // pour avoir une base de comparaison des le debut.
                    while(gpuRateWindow[w].size() > 2)
                    {
                        double age = std::chrono::duration<double>(
                            now - gpuRateWindow[w].front().t).count();
                        double ageNext = std::chrono::duration<double>(
                            now - gpuRateWindow[w][1].t).count();
                        // On retire le plus ancien seulement si le suivant
                        // couvre encore toute la fenetre (evite de trop
                        // raccourcir la base de mesure).
                        if(age > kRateWindowSeconds && ageNext >= kRateWindowSeconds)
                            gpuRateWindow[w].pop_front();
                        else
                            break;
                    }
                    // Taux = hachages accumules sur la fenetre / temps reel
                    // ecoule entre le plus ancien point et maintenant.
                    const auto& oldest = gpuRateWindow[w].front();
                    double windowElapsed = std::chrono::duration<double>(
                        now - oldest.t).count();
                    if(windowElapsed >= 0.5 && curr >= oldest.hashes)
                    {
                        row.hashrate =
                            static_cast<double>(curr - oldest.hashes) / windowElapsed;
                    }
                }
            }
        }

        MiningJob job = source->getJob();

        DashboardData dashboard;
        dashboard.totalHashrate = cpuRate;
        for(const auto &row : gpuRows) dashboard.totalHashrate += row.hashrate;
        dashboard.shares = source->getSubmittedCount();
        dashboard.accepted = source->getAcceptedCount();
        dashboard.rejected = source->getRejectedCount();
        dashboard.sendFailed = source->getSendFailedCount();
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

    // --- Arret propre (contexte normal, hors handler de signal) ---
    // Ordre VERROUILLE : on JOINT tous les workers AVANT toute destruction
    // de source. Les workers appellent source->getJob()/submitNonce() ; si
    // source etait detruit alors qu'un worker tourne encore, ce serait un
    // use-after-free (meme classe de bug qu'issue #1). stop() pose stop_
    // puis join() : il ne rend la main qu'une fois TOUS les workers arretes.
    restoreCursor();
    if(gpuMiner) gpuMiner->stop();  // workers GPU stoppes ENTRE deux batches
                                    // -> hashers detruits -> cudaFree propre
                                    // (pas de kernel Argon2id en vol).
    if(cpuMiner) cpuMiner->stop();
    // Ici, plus aucun worker vivant.

    if(fatalExit)
    {
        // Erreur de configuration (adresse -u invalide) : on NE quitte PAS.
        // Le wrapper HiveOS fait `exec ./kzminer` (voir packaging/hiveos/
        // kzminer/h-run.sh) : tout process qui sort est considere mort et
        // RELANCE par l'agent, quel que soit le code de sortie. Quitter ici
        // creerait donc une boucle crash-relance rejouant des logins rejetes
        // toutes les ~15s -> risque de ban IP cote pool. On reste au
        // contraire en vie a 0 H/s : minage arrete, GPU liberes, plus aucune
        // reconnexion (le watchdog de la source s'est deja arrete), motif
        // affiche, jusqu'a un signal d'arret explicite (Ctrl+C).
        std::cout << "\n" << kRed << "ERREUR FATALE : " << source->fatalError()
                  << kReset << "\n"
                  << "Minage arrete (0 H/s), GPU liberes, aucune reconnexion. "
                     "Corrigez l'adresse -u puis relancez KZMiner.\n"
                  << "Ctrl+C pour quitter." << std::flush;
        while(!g_stopRequested)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        restoreCursor();
    }

    // La destruction en fin de scope (gpuMiner/cpuMiner deja stoppes, puis
    // source EN DERNIER car declare en premier) est sans course.
    return 0;
}
