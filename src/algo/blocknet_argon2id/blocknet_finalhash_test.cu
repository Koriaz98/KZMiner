#include <cuda_runtime.h>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <vector>

__global__ void testBlake2bKernel(uint8_t* out, const uint8_t* in, uint32_t inlen, uint32_t outlen);

int main()
{
    std::ifstream f("/tmp/lastblock.bin", std::ios::binary);
    std::vector<uint8_t> lastBlock((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if(lastBlock.size() != 1024)
    {
        printf("Erreur lecture (taille: %zu)\n", lastBlock.size());
        return 1;
    }

    // Reproduit exactement ce que fait blake2bLong en interne pour
    // outlen=32 : prefixe de 4 octets (outlen, little-endian) + le
    // bloc de 1024 octets = 1028 octets d'entree pour blake2bSimple.
    std::vector<uint8_t> prefixed(4 + 1024);
    prefixed[0] = 32; prefixed[1] = 0; prefixed[2] = 0; prefixed[3] = 0;
    for(int i = 0; i < 1024; i++) prefixed[4 + i] = lastBlock[i];

    uint8_t* d_out;
    uint8_t* d_in;
    cudaMalloc(&d_out, 32);
    cudaMalloc(&d_in, prefixed.size());
    cudaMemcpy(d_in, prefixed.data(), prefixed.size(), cudaMemcpyHostToDevice);

    testBlake2bKernel<<<1, 1>>>(d_out, d_in, static_cast<uint32_t>(prefixed.size()), 32);
    cudaError_t err = cudaDeviceSynchronize();
    if(err != cudaSuccess)
    {
        printf("Erreur noyau: %s\n", cudaGetErrorString(err));
        return 1;
    }

    uint8_t h_out[32];
    cudaMemcpy(h_out, d_out, 32, cudaMemcpyDeviceToHost);

    printf("blake2bSimple(prefixed 1028 octets, outlen=32) obtenu : ");
    for(int i = 0; i < 32; i++) printf("%02x", h_out[i]);
    printf("\n");

    cudaFree(d_out);
    cudaFree(d_in);
    return 0;
}
