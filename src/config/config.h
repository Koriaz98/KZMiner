#pragma once
#include <string>

struct MinerConfig
{
    std::string pool;
    std::string wallet;
    std::string password = "x";
    std::string mode = "solo";
    std::string algo = "argon2id-09c";
    std::string workerName;
    int cpuThreads = 0;
    bool cpuEnabled = false;
    bool gpuEnabled = false;
    int intensity = 3;
};

class ConfigParser
{
public:
    static MinerConfig parse(int argc, char **argv);
    static void printHelp();
};
