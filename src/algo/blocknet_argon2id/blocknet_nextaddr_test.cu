#include <cuda_runtime.h>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <vector>
#include <cstring>

__global__ void testNextAddrSeqKernel(uint64_t* inputInOut, uint64_t* addrOut);
__global__ void testNextAddrCoopKernel(uint64_t* inputInOut, uint64_t* addrOut);

std::vector<uint8_t> readFile(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool runOne(const char* label, void(*launch)(uint64_t*,uint64_t*),
            const std::vector<uint8_t>& inputBlock, const std::vector<uint8_t>& expected)
{
    uint64_t* d_input; uint64_t* d_addr;
    cudaMalloc(&d_input, 1024); cudaMalloc(&d_addr, 1024);
    cudaMemcpy(d_input, inputBlock.data(), 1024, cudaMemcpyHostToDevice);
    cudaMemset(d_addr, 0, 1024);
    launch(d_input, d_addr);
    cudaError_t err = cudaDeviceSynchronize();
    if(err != cudaSuccess) { printf("%s: Erreur noyau: %s\n", label, cudaGetErrorString(err)); return false; }
    uint8_t h_addr[1024];
    cudaMemcpy(h_addr, d_addr, 1024, cudaMemcpyDeviceToHost);
    printf("%s obtenu (16 premiers octets) : ", label);
    for(int i = 0; i < 16; i++) printf("%02x", h_addr[i]);
    printf("\n");
    bool match = (memcmp(h_addr, expected.data(), 1024) == 0);
    printf("%s : %s\n", label, match ? "MATCH COMPLET (1024 octets)" : "MISMATCH");
    cudaFree(d_input); cudaFree(d_addr);
    return match;
}

void launchSeq(uint64_t* i, uint64_t* o){ testNextAddrSeqKernel<<<1,32>>>(i,o); }
void launchCoop(uint64_t* i, uint64_t* o){ testNextAddrCoopKernel<<<1,32>>>(i,o); }

int main()
{
    std::vector<uint8_t> inputBlock = readFile("/tmp/nextaddr_input.bin");
    std::vector<uint8_t> expected = readFile("/tmp/nextaddr_expected.bin");
    if(inputBlock.size() != 1024 || expected.size() != 1024)
    {
        printf("Erreur lecture (tailles: %zu, %zu)\n", inputBlock.size(), expected.size());
        return 1;
    }
    printf("Reference attendue : dcb284731475db87771bad284438e24a\n\n");
    bool s = runOne("SEQUENTIEL", launchSeq, inputBlock, expected);
    printf("\n");
    bool c = runOne("COOPERATIF", launchCoop, inputBlock, expected);
    printf("\n=> %s\n", (s && c) ? "LES DEUX CONCORDENT" : "AU MOINS UN ECHEC");
    return (s && c) ? 0 : 1;
}
