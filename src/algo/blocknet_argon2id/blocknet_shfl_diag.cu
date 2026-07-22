// Diagnostic isole de la version FUSIONNEE (shuffle-warp) de fillBlock,
// reproduisant l'approche de seine (compress_block_coop, opti A82) :
// colonnes -> diagonales fusionnees en registres via __shfl_sync, au
// lieu de deux sous-rondes separees par une barriere via memoire
// partagee. A valider octet par octet contre la reference sequentielle
// deja prouvee (0baea8f1...) AVANT tout port dans le vrai noyau.
#include <cuda_runtime.h>
#include <cstdint>

__device__ __forceinline__ uint64_t rotr64s(uint64_t x, int n)
{
    return (x >> n) | (x << (64 - n));
}
__device__ __forceinline__ uint64_t fBlaMkaS(uint64_t x, uint64_t y)
{
    uint64_t m = 0xFFFFFFFFULL;
    uint64_t xy = (x & m) * (y & m);
    return x + y + 2 * xy;
}
__device__ __forceinline__ void permuteStep(uint64_t& a, uint64_t& b, uint64_t& c, uint64_t& d)
{
    a = fBlaMkaS(a, b); d = rotr64s(d ^ a, 32);
    c = fBlaMkaS(c, d); b = rotr64s(b ^ c, 24);
    a = fBlaMkaS(a, b); d = rotr64s(d ^ a, 16);
    c = fBlaMkaS(c, d); b = rotr64s(b ^ c, 63);
}
#define WARP_MASK 0xFFFFFFFFU

__global__ void testFillBlockShflKernel(
    uint64_t* __restrict__ memory,
    uint64_t* __restrict__ outResult
)
{
    unsigned int tid = threadIdx.x;
    __shared__ uint64_t scratchR[128];
    __shared__ uint64_t scratchQ[128];

    const uint64_t* ref  = memory + 1 * 128;
    const uint64_t* prev = memory + 0 * 128;
    uint64_t r0 = ref[tid]      ^ prev[tid];
    uint64_t r1 = ref[tid+32]   ^ prev[tid+32];
    uint64_t r2 = ref[tid+64]   ^ prev[tid+64];
    uint64_t r3 = ref[tid+96]   ^ prev[tid+96];
    scratchR[tid]=r0;    scratchQ[tid]=r0;
    scratchR[tid+32]=r1; scratchQ[tid+32]=r1;
    scratchR[tid+64]=r2; scratchQ[tid+64]=r2;
    scratchR[tid+96]=r3; scratchQ[tid+96]=r3;
    __syncwarp(WARP_MASK);

    const unsigned int state = tid >> 2;
    const unsigned int lane  = tid & 3U;
    const unsigned int shflBase = state * 4U;

    {
        const unsigned int base = state * 16U;
        uint64_t a = scratchQ[base + lane];
        uint64_t b = scratchQ[base + 4U + lane];
        uint64_t c = scratchQ[base + 8U + lane];
        uint64_t d = scratchQ[base + 12U + lane];
        permuteStep(a, b, c, d);
        uint64_t a2 = a;
        uint64_t b2 = __shfl_sync(WARP_MASK, b, shflBase + ((lane + 1U) & 3U));
        uint64_t c2 = __shfl_sync(WARP_MASK, c, shflBase + ((lane + 2U) & 3U));
        uint64_t d2 = __shfl_sync(WARP_MASK, d, shflBase + ((lane + 3U) & 3U));
        permuteStep(a2, b2, c2, d2);
        scratchQ[base + lane]                 = a2;
        scratchQ[base + 4U + ((lane+1U)&3U)]  = b2;
        scratchQ[base + 8U + ((lane+2U)&3U)]  = c2;
        scratchQ[base + 12U + ((lane+3U)&3U)] = d2;
    }
    __syncwarp(WARP_MASK);

    {
        const unsigned int b = state * 2U;
        const unsigned int colSub = (lane >> 1) * 16U + (lane & 1U);
        uint64_t a = scratchQ[b + colSub];
        uint64_t c = scratchQ[b + 32U + colSub];
        uint64_t d = scratchQ[b + 64U + colSub];
        uint64_t e = scratchQ[b + 96U + colSub];
        permuteStep(a, c, d, e);
        uint64_t a2 = a;
        uint64_t c2 = __shfl_sync(WARP_MASK, c, shflBase + ((lane + 1U) & 3U));
        uint64_t d2 = __shfl_sync(WARP_MASK, d, shflBase + ((lane + 2U) & 3U));
        uint64_t e2 = __shfl_sync(WARP_MASK, e, shflBase + ((lane + 3U) & 3U));
        permuteStep(a2, c2, d2, e2);
        const unsigned int p0 = lane;
        const unsigned int p1 = (lane + 1U) & 3U;
        const unsigned int p2 = (lane + 2U) & 3U;
        const unsigned int p3 = (lane + 3U) & 3U;
        scratchQ[b + (p0>>1)*16U + (p0&1U)]        = a2;
        scratchQ[b + 32U + (p1>>1)*16U + (p1&1U)]  = c2;
        scratchQ[b + 64U + (p2>>1)*16U + (p2&1U)]  = d2;
        scratchQ[b + 96U + (p3>>1)*16U + (p3&1U)]  = e2;
    }
    __syncwarp(WARP_MASK);

    outResult[tid]    = scratchQ[tid]    ^ scratchR[tid];
    outResult[tid+32] = scratchQ[tid+32] ^ scratchR[tid+32];
    outResult[tid+64] = scratchQ[tid+64] ^ scratchR[tid+64];
    outResult[tid+96] = scratchQ[tid+96] ^ scratchR[tid+96];
}
