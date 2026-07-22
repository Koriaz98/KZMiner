#include <cuda_runtime.h>
#include <cstdio>
#include <cstdint>
#include <vector>

__global__ void testH0Kernel(
    uint8_t* h0out,
    const uint8_t* salt, uint32_t saltLen,
    uint64_t nonce, uint32_t mCostKib
);

int main()
{
    std::vector<uint8_t> salt(92);
    for(size_t i = 0; i < 92; i++) salt[i] = static_cast<uint8_t>(i);
    const uint64_t nonce = 0x0123456789ABCDEFULL;
    const uint32_t mCostKib = 64;

    uint8_t* d_h0;
    uint8_t* d_salt;
    cudaMalloc(&d_h0, 64);
    cudaMalloc(&d_salt, 92);
    cudaMemcpy(d_salt, salt.data(), 92, cudaMemcpyHostToDevice);

    testH0Kernel<<<1, 1>>>(d_h0, d_salt, 92, nonce, mCostKib);
    cudaError_t err = cudaDeviceSynchronize();
    if(err != cudaSuccess)
    {
        printf("Erreur noyau: %s\n", cudaGetErrorString(err));
        return 1;
    }

    uint8_t h_h0[64];
    cudaMemcpy(h_h0, d_h0, 64, cudaMemcpyDeviceToHost);

    printf("H0 obtenu (noyau CUDA) : ");
    for(int i = 0; i < 64; i++) printf("%02x", h_h0[i]);
    printf("\n");

    cudaFree(d_h0);
    cudaFree(d_salt);
    return 0;
}
