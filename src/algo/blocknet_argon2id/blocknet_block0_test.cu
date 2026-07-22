#include <cuda_runtime.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cstring>

__global__ void testBlock0Kernel(uint8_t* block0out, const uint8_t* h0in);

int main()
{
    // H0 deja valide independamment comme correct
    const char* h0hex = "a9756425f6006d7ead026e767e9fc8a04f4162f2d08d23e3963491ccee35f46"
                         "13ad2a76d8cfeec5abd5eb4d1bd1b3eff9107e74f1183088f48d5362ef1a89ea8";
    uint8_t h0[64];
    for(int i = 0; i < 64; i++)
    {
        unsigned int byte;
        sscanf(h0hex + i * 2, "%2x", &byte);
        h0[i] = static_cast<uint8_t>(byte);
    }

    uint8_t* d_h0;
    uint8_t* d_block0;
    cudaMalloc(&d_h0, 64);
    cudaMalloc(&d_block0, 1024);
    cudaMemcpy(d_h0, h0, 64, cudaMemcpyHostToDevice);

    testBlock0Kernel<<<1, 1>>>(d_block0, d_h0);
    cudaError_t err = cudaDeviceSynchronize();
    if(err != cudaSuccess)
    {
        printf("Erreur noyau: %s\n", cudaGetErrorString(err));
        return 1;
    }

    uint8_t h_block0[1024];
    cudaMemcpy(h_block0, d_block0, 1024, cudaMemcpyDeviceToHost);

    printf("Block0 obtenu (noyau CUDA, 16 premiers octets) : ");
    for(int i = 0; i < 16; i++) printf("%02x", h_block0[i]);
    printf("\n");
    printf("Block0 complet (hex) :\n");
    for(int i = 0; i < 1024; i++) printf("%02x", h_block0[i]);
    printf("\n");

    cudaFree(d_h0);
    cudaFree(d_block0);
    return 0;
}
