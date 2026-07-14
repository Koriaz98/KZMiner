#include "system_monitor.h"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <array>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
    std::string readFileTrim(const std::string &path)
    {
        std::ifstream f(path);
        std::string s;
        if(f) std::getline(f, s);
        while(!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        {
            s.pop_back();
        }
        return s;
    }

    struct CpuTimes
    {
        unsigned long long user = 0, nice = 0, system = 0, idle = 0;
        unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0;
    };

    bool readProcStatTotals(CpuTimes &out)
    {
        std::ifstream f("/proc/stat");
        if(!f) return false;

        std::string label;
        f >> label;
        if(label != "cpu") return false;

        f >> out.user >> out.nice >> out.system >> out.idle
          >> out.iowait >> out.irq >> out.softirq >> out.steal;

        return true;
    }
}

CpuStats SystemMonitor::readCpuStats()
{
    static CpuTimes previous;
    static bool hasPrevious = false;
    static std::string cachedModelName;
    static bool modelNameRead = false;

    CpuStats stats;

    if(!modelNameRead)
    {
        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string line;
        while(std::getline(cpuinfo, line))
        {
            if(line.rfind("model name", 0) == 0)
            {
                auto colonPos = line.find(':');
                if(colonPos != std::string::npos)
                {
                    cachedModelName = line.substr(colonPos + 1);
                    size_t start = cachedModelName.find_first_not_of(' ');
                    cachedModelName = (start == std::string::npos) ? "" : cachedModelName.substr(start);
                }
                break;
            }
        }
        modelNameRead = true;
    }
    stats.modelName = cachedModelName;

    const std::string hwmonRoot = "/sys/class/hwmon";
    std::error_code ec;
    if(fs::exists(hwmonRoot, ec))
    {
        for(const auto &entry : fs::directory_iterator(hwmonRoot, ec))
        {
            std::string driverName = readFileTrim(entry.path().string() + "/name");

            if(driverName == "k10temp" || driverName == "coretemp" ||
               driverName == "zenpower" || driverName == "cpu_thermal")
            {
                std::string raw = readFileTrim(entry.path().string() + "/temp1_input");
                if(!raw.empty())
                {
                    try
                    {
                        stats.tempCelsius = std::stod(raw) / 1000.0;
                        stats.tempAvailable = true;
                        break;
                    }
                    catch(...) {}
                }
            }
        }
    }

    CpuTimes current;
    if(readProcStatTotals(current))
    {
        if(hasPrevious)
        {
            unsigned long long prevIdle = previous.idle + previous.iowait;
            unsigned long long curIdle  = current.idle + current.iowait;

            unsigned long long prevNonIdle = previous.user + previous.nice + previous.system
                + previous.irq + previous.softirq + previous.steal;
            unsigned long long curNonIdle = current.user + current.nice + current.system
                + current.irq + current.softirq + current.steal;

            unsigned long long prevTotal = prevIdle + prevNonIdle;
            unsigned long long curTotal  = curIdle + curNonIdle;

            unsigned long long totalDelta = curTotal - prevTotal;
            unsigned long long idleDelta  = curIdle - prevIdle;

            if(totalDelta > 0)
            {
                stats.usagePercent = (double)(totalDelta - idleDelta) / (double)totalDelta * 100.0;
                stats.usageAvailable = true;
            }
        }

        previous = current;
        hasPrevious = true;
    }

    return stats;
}

LoadAverage SystemMonitor::readLoadAverage()
{
    LoadAverage la;
    std::ifstream f("/proc/loadavg");
    if(f)
    {
        f >> la.load1 >> la.load5 >> la.load15;
        la.available = true;
    }
    return la;
}

std::vector<GpuStats> SystemMonitor::readGpuStats()
{
    std::vector<GpuStats> result;

    const char *cmd =
        "nvidia-smi --query-gpu=index,name,temperature.gpu,utilization.gpu,"
        "memory.used,memory.total,power.draw,fan.speed "
        "--format=csv,noheader,nounits 2>/dev/null";

    FILE *pipe = popen(cmd, "r");
    if(!pipe) return result;

    std::array<char, 512> buffer;
    std::string output;
    while(fgets(buffer.data(), buffer.size(), pipe) != nullptr)
    {
        output += buffer.data();
    }
    pclose(pipe);

    std::istringstream stream(output);
    std::string line;
    while(std::getline(stream, line))
    {
        if(line.empty()) continue;

        std::istringstream ls(line);
        std::string token;
        std::vector<std::string> fields;

        while(std::getline(ls, token, ','))
        {
            size_t start = token.find_first_not_of(' ');
            fields.push_back(start == std::string::npos ? "" : token.substr(start));
        }

        if(fields.size() < 8) continue;

        GpuStats g;
        try
        {
            g.index = std::stoi(fields[0]);
            g.name = fields[1];
            g.tempCelsius = std::stod(fields[2]);
            g.utilPercent = std::stoi(fields[3]);
            g.memUsedMiB = std::stod(fields[4]);
            g.memTotalMiB = std::stod(fields[5]);
            g.powerWatts = std::stod(fields[6]);
            try { g.fanPercent = std::stoi(fields[7]); } catch(...) { g.fanPercent = -1; }
        }
        catch(...)
        {
            continue;
        }

        result.push_back(g);
    }

    return result;
}
