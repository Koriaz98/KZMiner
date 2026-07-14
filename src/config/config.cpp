#include "config.h"
#include <iostream>
#include <cstdlib>

MinerConfig ConfigParser::parse(int argc, char **argv)
{
    MinerConfig config;
    for(int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if(arg == "-o" && i+1 < argc)
        {
            config.pool = argv[++i];
        }
        else if(arg == "-u" && i+1 < argc)
        {
            config.wallet = argv[++i];
        }
        else if(arg == "-p" && i+1 < argc)
        {
            config.password = argv[++i];
        }
        else if(arg == "--mode" && i+1 < argc)
        {
            config.mode = argv[++i];
        }
        else if(arg == "--cpu-threads" && i+1 < argc)
        {
            config.cpuThreads = std::atoi(argv[++i]);
        }
        else if(arg == "--cpu")
        {
            config.cpuEnabled = true;
        }
        else if(arg == "--gpu" || arg == "--cuda")
        {
            config.gpuEnabled = true;
        }
        else if(arg == "--worker" && i+1 < argc)
        {
            config.workerName = argv[++i];
        }
        else if(arg == "--intensity" && i+1 < argc)
        {
            config.intensity = std::atoi(argv[++i]);
            if(config.intensity < 1) config.intensity = 1;
            if(config.intensity > 5) config.intensity = 5;
        }
        else if(arg == "-h" || arg == "--help")
        {
            printHelp();
            exit(0);
        }
    }

    if(!config.cpuEnabled && !config.gpuEnabled)
    {
        config.cpuEnabled = true;
    }

    return config;
}

void ConfigParser::printHelp()
{
    std::cout
    << "\nKZMiner\n"
    << "============================\n\n"
    << "-o <pool>\n"
    << "    solo : coordinator URL, e.g. https://btc09.org\n"
    << "    pool : host:port, e.g. hk.ntmminer.com:8344\n\n"
    << "-u <wallet or wallet.rigname>\n"
    << "    BTC09 (09C) address. Optional .rigname suffix to identify this rig on the pool.\n\n"
    << "--worker <name>\n"
    << "    Rig/worker name (takes priority over the -u wallet.name suffix)\n\n"
    << "--mode <solo|pool>\n"
    << "    solo (default) : Open Mining Protocol v1, 100% of the block if found\n"
    << "    pool : third-party protocol, smoothed payout (PPLNS)\n\n"
    << "--cpu\n"
    << "    Enable CPU mining\n\n"
    << "--gpu\n"
    << "    Enable GPU mining (CUDA, Nvidia only)\n\n"
    << "    --cpu and --gpu can be combined to mine on both at once.\n"
    << "    If neither is specified, --cpu is used by default.\n\n"
    << "--cpu-threads <n>\n"
    << "    Number of CPU threads (default: all logical cores)\n\n"
    << "--intensity <1-5>\n"
    << "    Fraction of free VRAM used per GPU (1=15% ... 5=90%, default 3=50%)\n\n";
}
