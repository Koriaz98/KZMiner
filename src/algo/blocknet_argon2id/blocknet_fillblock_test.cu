#include <cuda_runtime.h>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <vector>

__global__ void testFillBlockKernel(
    uint64_t* out,
    const uint64_t* prev,
    const uint64_t* ref
);

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

    uint64_t* d_prev;
    uint64_t* d_ref;
    uint64_t* d_out;
    cudaMalloc(&d_prev, 1024);
    cudaMalloc(&d_ref, 1024);
    cudaMalloc(&d_out, 1024);
    cudaMemcpy(d_prev, block0.data(), 1024, cudaMemcpyHostToDevice);
    cudaMemcpy(d_ref, block1.data(), 1024, cudaMemcpyHostToDevice);

    testFillBlockKernel<<<1, 1>>>(d_out, d_prev, d_ref);
    cudaError_t err = cudaDeviceSynchronize();
    if(err != cudaSuccess)
    {
        printf("Erreur noyau: %s\n", cudaGetErrorString(err));
        return 1;
    }

    uint8_t h_out[1024];
    cudaMemcpy(h_out, d_out, 1024, cudaMemcpyDeviceToHost);

    printf("fillBlock obtenu (16 premiers octets) : ");
    for(int i = 0; i < 16; i++) printf("%02x", h_out[i]);
    printf("\n");

    std::ofstream outFile("/tmp/fillblock_gpu.bin", std::ios::binary);
    outFile.write(reinterpret_cast<char*>(h_out), 1024);

    cudaFree(d_prev);
    cudaFree(d_ref);
    cudaFree(d_out);
    return 0;
}
