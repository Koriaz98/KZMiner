#include "../version.h"
#include "../console_lock.h"
#include "../console_output.h"
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
        "+-----------------------------------------------------------------------------------------------------------------+";

    std::string buildDashboardText(const DashboardData& data)
    {
        CpuStats cpu = SystemMonitor::readCpuStats();
        LoadAverage la = SystemMonitor::readLoadAverage();

        std::ostringstream out;

        out << kBold << kCyan << " KZMiner " << KZMinerInfo::kVersion << " - " << KZMinerInfo::kTagline << " " << kReset << "\n";
        out << kBold << kCyan << " Wallet: " << data.walletAddress << " " << kReset << "\n";
        out << kBold << kCyan << " Pool: " << data.poolAddress << " " << kReset << "\n";
        out << "\n";
        out << kCyan << kSep << "\n" << kReset;

        out
            << kBold << "HASHRATE" << kReset << " " << kGreen << data.totalHashrate << " H/s" << kReset
            << "  |  " << kBold << "SHARES" << kReset << " " << kGreen << data.shares << kReset
            << " (" << kGreen << data.accepted << " accepted" << kReset
            << ", " << kRed << data.rejected << " rejected" << kReset << ")";

        // En solo, "accepted" represente deja des blocs entiers valides
        // (pas des parts partielles comme en pool) - ce compteur
        // dedie le rend explicite, evitant toute ambiguite sur ce que
        // "accepted" signifie reellement selon le mode. Non affiche en
        // pool : le protocole des pools tiers ne remonte generalement
        // pas si une part acceptee correspondait aussi a un bloc reseau
        // complet, on ne dispose donc d'aucune donnee fiable a montrer.
        if(data.mode == "solo")
        {
            out << "  |  " << kBold << "BLOCK FOUND" << kReset << " " << kGreen << data.accepted << kReset;
        }

        out
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
            << "  |  " << kBold << "ALGO" << kReset << " " << data.algoName
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
}

void printStatusTable(const DashboardData& data)
{
    static bool isTty = (isatty(STDOUT_FILENO) != 0);

    std::string text = buildDashboardText(data);

    if(!isTty)
    {
        std::lock_guard<std::mutex> lock(consoleMutex());
        std::cout << "\n" << text << std::flush;
        return;
    }

    // Approche par "differentiel" (comme ncurses/htop) : on compare
    // le nouveau cadre au precedent, ligne par ligne, et on ne touche
    // que les lignes qui ont reellement change (chiffres qui varient)
    // - les separateurs, en-tetes et logs deja affiches restent
    // parfaitement intacts d'un cycle a l'autre. Elimine le
    // clignotement d'un reaffichage complet a chaque cycle, sans
    // revenir a la zone de defilement restreinte (DECSTBM) qui se
    // corrompait sous certains multiplexeurs de terminal. Cette
    // fonction n'est appelee que par le thread principal (les autres
    // threads deposent leurs messages via pushLogLine()), pas besoin
    // de proteger la comparaison elle-meme, seule l'ecriture finale
    // sur le terminal reste sous mutex.
    std::vector<std::string> newLines;
    {
        std::istringstream textStream(text);
        std::string textLine;
        while(std::getline(textStream, textLine)) newLines.push_back(textLine);
    }
    int dashboardLines = static_cast<int>(newLines.size());
    newLines.push_back("");

    int termRows = 50;
    struct winsize ws{};
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
    {
        termRows = ws.ws_row;
    }

    int availableForLogs = termRows - dashboardLines - 2;
    if(availableForLogs < 0) availableForLogs = 0;

    std::vector<std::string> logs = recentLogLines();
    if(static_cast<int>(logs.size()) > availableForLogs)
    {
        logs.erase(logs.begin(), logs.end() - availableForLogs);
    }
    for(const auto &line : logs) newLines.push_back(line);

    // previousLines persiste entre les appels (fonction statique,
    // un seul thread appelant - voir commentaire plus haut). Vide au
    // tout premier appel, ce qui force naturellement une premiere
    // ecriture complete, necessaire pour etablir l'etat initial de
    // l'ecran - comme le ferait n'importe quel outil de ce type.
    static std::vector<std::string> previousLines;
    static bool firstCall = true;
    static int previousTermRows = 0;

    std::ostringstream frame;
    bool anyChange = false;

    // Un redimensionnement de terminal (frequent lors d'un
    // rattachement "screen -r", si la fenetre a change de taille
    // entre-temps) invalide silencieusement notre memoire de ce qui
    // est reellement affiche : l'ecran peut avoir ete redessine ou
    // reorganise par le multiplexeur sans que notre logique de
    // differentiel en soit informee, laissant des residus visuels
    // (lignes jugees "inchangees" a tort). On force alors un
    // redessin complet, comme au tout premier appel.
    bool forceFullRedraw = firstCall || (termRows != previousTermRows);
    previousTermRows = termRows;

    if(forceFullRedraw)
    {
        // Un effacement complet, au tout premier appel ou apres un
        // redimensionnement detecte - pour repartir d'un ecran propre
        // (le logo et les lignes affichees avant l'activation du
        // panneau restent dans le scrollback mais ne doivent plus se
        // melanger avec le debut du tableau). En dehors de ces cas,
        // seule la logique differentielle ci-dessous s'applique, sans
        // jamais effacer l'ecran entier.
        //
        // Le curseur du terminal est aussi masque (\033[?25l) : sans
        // cela, il reste visible a l'endroit exact de la derniere
        // ligne modifiee a chaque cycle, ce qui donne l'impression
        // d'un petit rectangle qui se deplace sans arret - comme
        // htop/nvtop, qui masquent egalement leur curseur en mode
        // plein ecran.
        frame << "\033[?25l";
        frame << "\033[2J\033[H";
        previousLines.clear();
        firstCall = false;
        anyChange = true;
    }

    for(size_t i = 0; i < newLines.size(); i++)
    {
        bool differs = (i >= previousLines.size()) || (previousLines[i] != newLines[i]);
        if(!differs) continue;

        anyChange = true;
        // Positionnement direct sur la ligne concernee (acces
        // aleatoire), plutot que de suivre sequentiellement depuis le
        // haut - permet de ne jamais toucher aux lignes inchangees,
        // meme situees entre deux lignes modifiees.
        frame << "\033[" << (i + 1) << ";1H" << newLines[i] << "\033[K";
    }

    // Si le nouveau cadre est plus court que le precedent (moins de
    // lignes de logs affichees, cas rare), efface le surplus residuel.
    if(newLines.size() < previousLines.size())
    {
        anyChange = true;
        frame << "\033[" << (newLines.size() + 1) << ";1H" << "\033[J";
    }

    previousLines = newLines;

    if(!anyChange) return;

    std::lock_guard<std::mutex> lock(consoleMutex());
    std::cout << frame.str() << std::flush;
}
