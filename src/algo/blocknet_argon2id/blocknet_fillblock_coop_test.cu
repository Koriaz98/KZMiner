#include <cuda_runtime.h>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <vector>
#include <cstring>

__global__ void testFillBlockCoopKernel(uint64_t* memory, uint64_t* outResult);

std::vector<uint8_t> readFile(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main()
{
    std::vector<uint8_t> block0 = readFile("/tmp/block0.bin");
    std::vector<uint8_t> block1 = readFile("/tmp/block1.bin");
    if(block0.size() != 1024 || block1.size() != 1024)
    {
        printf("Erreur de lecture des fichiers (tailles: %zu, %zu)\n", block0.size(), block1.size());
        return 1;
    }

    // memory[0]=prev (block0), memory[1]=ref (block1)
    std::vector<uint8_t> memoryHost(2048);
    memcpy(memoryHost.data(), block0.data(), 1024);
    memcpy(memoryHost.data() + 1024, block1.data(), 1024);

    uint64_t* d_memory;
    uint64_t* d_out;
    cudaMalloc(&d_memory, 2048);
    cudaMalloc(&d_out, 1024);
    cudaMemcpy(d_memory, memoryHost.data(), 2048, cudaMemcpyHostToDevice);

    testFillBlockCoopKernel<<<1, 32>>>(d_memory, d_out);
    cudaError_t err = cudaDeviceSynchronize();
    if(err != cudaSuccess)
    {
        printf("Erreur noyau: %s\n", cudaGetErrorString(err));
        return 1;
    }

    uint8_t h_out[1024];
    cudaMemcpy(h_out, d_out, 1024, cudaMemcpyDeviceToHost);

    printf("fillBlock cooperatif obtenu (16 premiers octets) : ");
    for(int i = 0; i < 16; i++) printf("%02x", h_out[i]);
    printf("\n");
    printf("fillBlock sequentiel attendu (16 premiers octets) : 0baea8f1a1f3fefa3a03cb57ea205b9e\n");

    cudaFree(d_memory);
    cudaFree(d_out);
    return 0;
}
