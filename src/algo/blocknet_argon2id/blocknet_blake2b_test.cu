#include <cuda_runtime.h>
#include <cstdio>
#include <cstring>
#include <cstdint>

// Declaration du noyau de test defini dans blocknet_gpu_kernel.cu
__global__ void testBlake2bKernel(uint8_t* out, const uint8_t* in, uint32_t inlen, uint32_t outlen);

int main()
{
    // Vecteur de test standard, directement issu de la RFC 7693,
    // Annexe A (trace de calcul BLAKE2b-512("abc")), au format octet
    // par octet pour eviter tout risque de transcription :
    // BA 80 A5 3F 98 1C 4D 0D 6A 27 97 B6 9F 12 F6 E9
    // 4C 21 2F 14 68 5A C4 B7 4B 12 BB 6F DB FF A2 D1
    // 7D 87 C5 39 2A AB 79 2D C2 52 D5 DE 45 33 CC 95
    // 18 D3 8A A8 DB F1 92 5A B9 23 86 ED D4 00 99 23
    const char* expectedHex =
        "ba80a53f981c4d0d6a2797b69f12f6e9"
        "4c212f14685ac4b74b12bb6fdbffa2d1"
        "7d87c5392aab792dc252d5de4533cc95"
        "18d38aa8dbf1925ab92386edd4009923";
    const char* inputStr = "abc";

    uint8_t* d_out;
    uint8_t* d_in;
    cudaMalloc(&d_out, 64);
    cudaMalloc(&d_in, 3);
    cudaMemcpy(d_in, inputStr, 3, cudaMemcpyHostToDevice);

    testBlake2bKernel<<<1, 1>>>(d_out, d_in, 3, 64);
    cudaError_t err = cudaDeviceSynchronize();
    if(err != cudaSuccess)
    {
        printf("Erreur noyau: %s\n", cudaGetErrorString(err));
        return 1;
    }

    uint8_t h_out[64];
    cudaMemcpy(h_out, d_out, 64, cudaMemcpyDeviceToHost);

    char actualHex[129];
    for(int i = 0; i < 64; i++) sprintf(actualHex + i * 2, "%02x", h_out[i]);
    actualHex[128] = '\0';

    printf("Attendu : %s\n", expectedHex);
    printf("Obtenu  : %s\n", actualHex);
    bool match = (strcmp(expectedHex, actualHex) == 0);
    printf("%s\n", match ? "MATCH - Blake2b est correct" : "MISMATCH - bug dans Blake2b lui-meme");

    cudaFree(d_out);
    cudaFree(d_in);
    return match ? 0 : 1;
}
