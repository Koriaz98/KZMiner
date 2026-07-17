#pragma once
#include "monitor/status_table.h"
#include <string>

// Ecrit un instantane de l'etat du mineur au format JSON, a un
// emplacement fixe sur disque, pour que des outils externes (HiveOS
// h-stats.sh, un futur exporteur Prometheus, etc.) puissent lire des
// statistiques machine-readable sans avoir a analyser le texte du
// tableau de bord humain (fragile, sujet a changer).
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
);
