#include "gpu_device_info.h"
#include <cuda_runtime.h>
#include <cstdlib>
#include <cstring>

std::vector<CudaPciIdentity> cudaDeviceIdentities()
{
    std::vector<CudaPciIdentity> identities;

    int count = 0;
    if(cudaGetDeviceCount(&count) != cudaSuccess || count <= 0) return identities;

    identities.assign(static_cast<size_t>(count), CudaPciIdentity{});

    for(int d = 0; d < count; d++)
    {
        // Format renvoye par CUDA : "domain:bus:device.function" en
        // hexadecimal, ex "0000:0a:00.0" - on extrait "0a" (le bus),
        // converti en decimal (10). Meme extraction que le cote NVML
        // dans SystemMonitor::readGpuStats(), pour que les deux valeurs
        // soient directement comparables.
        char pciId[32] = {0};
        if(cudaDeviceGetPCIBusId(pciId, sizeof(pciId), d) != cudaSuccess) continue;

        CudaPciIdentity &id = identities[static_cast<size_t>(d)];
        id.pciBusId = pciId;

        const char *firstColon = std::strchr(pciId, ':');
        if(!firstColon) continue;
        const char *busStart = firstColon + 1;
        const char *secondColon = std::strchr(busStart, ':');
        if(!secondColon) continue;

        size_t len = static_cast<size_t>(secondColon - busStart);
        char busHex[16] = {0};
        if(len == 0 || len >= sizeof(busHex)) continue;
        std::memcpy(busHex, busStart, len);

        id.busDecimal = static_cast<int>(std::strtol(busHex, nullptr, 16));
    }

    return identities;
}
