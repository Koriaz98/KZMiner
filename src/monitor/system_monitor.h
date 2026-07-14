#pragma once
#include <string>
#include <vector>

struct CpuStats
{
    bool tempAvailable = false;
    double tempCelsius = 0.0;
    bool usageAvailable = false;
    double usagePercent = 0.0;
    std::string modelName;
};

struct LoadAverage
{
    bool available = false;
    double load1 = 0.0;
    double load5 = 0.0;
    double load15 = 0.0;
};

struct GpuStats
{
    int index = 0;
    std::string name;
    double tempCelsius = 0.0;
    int utilPercent = 0;
    double memUsedMiB = 0.0;
    double memTotalMiB = 0.0;
    double powerWatts = 0.0;
    int fanPercent = -1;
};

class SystemMonitor
{
public:
    static CpuStats readCpuStats();
    static LoadAverage readLoadAverage();
    static std::vector<GpuStats> readGpuStats();
};
