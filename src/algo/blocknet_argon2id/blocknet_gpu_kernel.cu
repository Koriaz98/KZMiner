// Implementation "maison" d'Argon2id en CUDA pour Blocknet (lanes=1,
// t_cost=1, m_cost variable par job). Chaque thread traite un hash
// complet (un nonce) de bout en bout, sequentiellement - le
// remplissage memoire est intrinsequement sequentiel avec une seule
// lane, donc pas de parallelisme interne a un hash ; le parallelisme
// vient du nombre de hachages independants traites simultanement.
//
// Verifie ligne par ligne contre libargon2 (CC0/Apache-2.0,
// third_party/argon2/src/{core,ref}.c et blake2/*), pas un portage du
// noyau optimise de seine.

#include "blocknet_gpu_hasher.h"
#include <cuda_runtime.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstring>

namespace
{

// ---------------------------------------------------------------
// Blake2b standard (RFC 7693) - utilise pour H0 et pour la variante
// "H'" a sortie etendue (fill_first_blocks, sortie finale).
// ---------------------------------------------------------------

__device__ __constant__ uint64_t kBlake2bIV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

__device__ __constant__ uint8_t kBlake2bSigma[12][16] = {
    {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
    {14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3},
    {11,8,12,0,5,2,15,13,10,14,3,6,7,1,9,4},
    {7,9,3,1,13,12,11,14,2,6,5,10,4,0,15,8},
    {9,0,5,7,2,4,10,15,14,1,11,12,6,8,3,13},
    {2,12,6,10,0,11,8,3,4,13,7,5,15,14,1,9},
    {12,5,1,15,14,13,4,10,0,7,6,3,9,2,8,11},
    {13,11,7,14,12,1,3,9,5,0,15,4,8,6,2,10},
    {6,15,14,9,11,3,0,8,12,2,13,7,1,4,10,5},
    {10,2,8,4,7,6,1,5,15,11,9,14,3,12,13,0},
    {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
    {14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3}
};

__device__ __forceinline__ uint64_t rotr64d(uint64_t x, int n)
{
    return (x >> n) | (x << (64 - n));
}

// Compression Blake2b standard (pas BlaMka - c'est la primitive de
// hachage generale, utilisee comme sous-routine par Argon2 pour H0 et
// H', distincte de la fonction de melange BlaMka du remplissage
// memoire ci-dessous).
__device__ void blake2bCompress(uint64_t h[8], const uint64_t m[16], uint64_t t0, uint64_t t1, bool isLast)
{
    uint64_t v[16];
    for(int i = 0; i < 8; i++) v[i] = h[i];
    for(int i = 0; i < 8; i++) v[8 + i] = kBlake2bIV[i];
    v[12] ^= t0;
    v[13] ^= t1;
    if(isLast) v[14] = ~v[14];

    for(int round = 0; round < 12; round++)
    {
        const uint8_t* s = kBlake2bSigma[round];
        #define BG(a,b,c,d,x,y) \
            v[a] = v[a] + v[b] + x; v[d] = rotr64d(v[d] ^ v[a], 32); \
            v[c] = v[c] + v[d];     v[b] = rotr64d(v[b] ^ v[c], 24); \
            v[a] = v[a] + v[b] + y; v[d] = rotr64d(v[d] ^ v[a], 16); \
            v[c] = v[c] + v[d];     v[b] = rotr64d(v[b] ^ v[c], 63);

        BG(0,4,8,12, m[s[0]], m[s[1]]);
        BG(1,5,9,13, m[s[2]], m[s[3]]);
        BG(2,6,10,14, m[s[4]], m[s[5]]);
        BG(3,7,11,15, m[s[6]], m[s[7]]);
        BG(0,5,10,15, m[s[8]], m[s[9]]);
        BG(1,6,11,12, m[s[10]], m[s[11]]);
        BG(2,7,8,13, m[s[12]], m[s[13]]);
        BG(3,4,9,14, m[s[14]], m[s[15]]);
        #undef BG
    }
    for(int i = 0; i < 8; i++) h[i] ^= v[i] ^ v[8 + i];
}

// Blake2b generique, buffer d'entree borne (largement suffisant pour
// nos usages : H0 en entree ~140 octets, H' en entree <=76 octets).
// outlen <= 64 (un seul appel a compress, pas de chainage).
__device__ void blake2bSimple(uint8_t* out, uint32_t outlen, const uint8_t* in, uint32_t inlen)
{
    uint64_t h[8];
    for(int i = 0; i < 8; i++) h[i] = kBlake2bIV[i];
    h[0] ^= 0x01010000ULL ^ (static_cast<uint64_t>(outlen));

    uint64_t t0 = 0;
    uint32_t offset = 0;
    uint8_t block[128];

    while(inlen - offset > 128)
    {
        memcpy(block, in + offset, 128);
        t0 += 128;
        blake2bCompress(h, reinterpret_cast<const uint64_t*>(block), t0, 0, false);
        offset += 128;
    }

    uint32_t remaining = inlen - offset;
    memset(block, 0, 128);
    memcpy(block, in + offset, remaining);
    t0 += remaining;
    blake2bCompress(h, reinterpret_cast<const uint64_t*>(block), t0, 0, true);

    uint8_t digest[64];
    memcpy(digest, h, 64);
    memcpy(out, digest, outlen);
}

// H' (blake2b_long) : sortie de longueur arbitraire, prefixee par
// outlen sur 4 octets little-endian. Utilise pour generer les blocs
// initiaux (1024 octets) et pour l'extraction finale (32 octets, ce
// qui reste dans le cas <=64 gere par blake2bSimple directement, la
// boucle ci-dessous n'est donc utile qu'a la generation des blocs
// initiaux, mais ecrite de facon generique).
__device__ void blake2bLong(uint8_t* out, uint32_t outlen, const uint8_t* in, uint32_t inlen)
{
    // BUG CRITIQUE CORRIGE (trouve via diagnostic approfondi le
    // 22 juillet) : ce tampon etait dimensionne pour seulement 108
    // octets (suffisant pour H0/generation des blocs initiaux), mais
    // cette fonction est AUSSI appelee pour le hash final avec une
    // entree de 1024 octets (le dernier bloc complet) - soit 1028
    // octets necessaires avec le prefixe de 4 octets. Le debordement
    // resultant (~920 octets au-dela du tableau, dans la pile du
    // thread) n'etait detecte par AUCUN outil de diagnostic (memoire
    // "possedee" par le thread, pas un acces illegal a proprement
    // parler), et son effet variait selon l'agencement de pile propre
    // a chaque architecture cible - explique pourquoi le calcul
    // semblait correct avec l'ancienne cible de compilation
    // (compute_52) mais pas avec la bonne (sm_120, Blackwell).
    uint8_t prefixed[4 + 1024];
    uint8_t outlenBytes[4];
    outlenBytes[0] = static_cast<uint8_t>(outlen & 0xff);
    outlenBytes[1] = static_cast<uint8_t>((outlen >> 8) & 0xff);
    outlenBytes[2] = static_cast<uint8_t>((outlen >> 16) & 0xff);
    outlenBytes[3] = static_cast<uint8_t>((outlen >> 24) & 0xff);

    memcpy(prefixed, outlenBytes, 4);
    memcpy(prefixed + 4, in, inlen);
    uint32_t totalIn = 4 + inlen;

    if(outlen <= 64)
    {
        blake2bSimple(out, outlen, prefixed, totalIn);
        return;
    }

    uint8_t outBuffer[64];
    blake2bSimple(outBuffer, 64, prefixed, totalIn);
    memcpy(out, outBuffer, 32);
    out += 32;
    uint32_t toProduce = outlen - 32;

    while(toProduce > 64)
    {
        uint8_t inBuffer[64];
        memcpy(inBuffer, outBuffer, 64);
        blake2bSimple(outBuffer, 64, inBuffer, 64);
        memcpy(out, outBuffer, 32);
        out += 32;
        toProduce -= 32;
    }

    uint8_t inBuffer[64];
    memcpy(inBuffer, outBuffer, 64);
    blake2bSimple(outBuffer, 64, inBuffer, 64);
    memcpy(out, outBuffer, toProduce);
}

// ---------------------------------------------------------------
// Argon2 : fonction de melange BlaMka (distincte de Blake2b standard
// ci-dessus), utilisee uniquement pour le remplissage memoire.
// ---------------------------------------------------------------

__device__ __forceinline__ uint64_t fBlaMka(uint64_t x, uint64_t y)
{
    const uint64_t m = 0xFFFFFFFFULL;
    const uint64_t xy = (x & m) * (y & m);
    return x + y + 2 * xy;
}

#define BLAMKA_G(a,b,c,d) \
    a = fBlaMka(a, b); d = rotr64d(d ^ a, 32); \
    c = fBlaMka(c, d); b = rotr64d(b ^ c, 24); \
    a = fBlaMka(a, b); d = rotr64d(d ^ a, 16); \
    c = fBlaMka(c, d); b = rotr64d(b ^ c, 63);

// fill_block : combine prev_block et ref_block dans next_block,
// exactement selon ref.c::fill_block (avec_xor toujours faux ici -
// une seule passe, t_cost=1, jamais de contenu precedent a XORer).
__device__ void fillBlock(const uint64_t* prevBlock, const uint64_t* refBlock, uint64_t* nextBlock)
{
    uint64_t blockR[128], blockTmp[128];
    for(int i = 0; i < 128; i++)
    {
        blockR[i] = refBlock[i] ^ prevBlock[i];
        blockTmp[i] = blockR[i];
    }

    for(int i = 0; i < 8; i++)
    {
        uint64_t* v = blockR + 16 * i;
        BLAMKA_G(v[0], v[4], v[8], v[12]);
        BLAMKA_G(v[1], v[5], v[9], v[13]);
        BLAMKA_G(v[2], v[6], v[10], v[14]);
        BLAMKA_G(v[3], v[7], v[11], v[15]);
        BLAMKA_G(v[0], v[5], v[10], v[15]);
        BLAMKA_G(v[1], v[6], v[11], v[12]);
        BLAMKA_G(v[2], v[7], v[8], v[13]);
        BLAMKA_G(v[3], v[4], v[9], v[14]);
    }

    for(int i = 0; i < 8; i++)
    {
        uint64_t a = blockR[2*i], b = blockR[2*i+1];
        uint64_t c = blockR[2*i+16], d = blockR[2*i+17];
        uint64_t e = blockR[2*i+32], f = blockR[2*i+33];
        uint64_t g = blockR[2*i+48], hh = blockR[2*i+49];
        uint64_t ii = blockR[2*i+64], jj = blockR[2*i+65];
        uint64_t kk = blockR[2*i+80], ll = blockR[2*i+81];
        uint64_t mm = blockR[2*i+96], nn = blockR[2*i+97];
        uint64_t oo = blockR[2*i+112], pp = blockR[2*i+113];
        BLAMKA_G(a, e, ii, mm);
        BLAMKA_G(b, f, jj, nn);
        BLAMKA_G(c, g, kk, oo);
        BLAMKA_G(d, hh, ll, pp);
        BLAMKA_G(a, f, kk, pp);
        BLAMKA_G(b, g, ll, mm);
        BLAMKA_G(c, hh, ii, nn);
        BLAMKA_G(d, e, jj, oo);
        blockR[2*i]=a; blockR[2*i+1]=b; blockR[2*i+16]=c; blockR[2*i+17]=d;
        blockR[2*i+32]=e; blockR[2*i+33]=f; blockR[2*i+48]=g; blockR[2*i+49]=hh;
        blockR[2*i+64]=ii; blockR[2*i+65]=jj; blockR[2*i+80]=kk; blockR[2*i+81]=ll;
        blockR[2*i+96]=mm; blockR[2*i+97]=nn; blockR[2*i+112]=oo; blockR[2*i+113]=pp;
    }

    for(int i = 0; i < 128; i++)
    {
        nextBlock[i] = blockTmp[i] ^ blockR[i];
    }
}

// next_addresses : generateur d'adresses pour l'adressage independant
// des donnees (tranches 0-1). inputBlock.v[6] est le compteur de bloc
// d'adresse, incremente a chaque appel.
__device__ void nextAddresses(uint64_t* addressBlock, uint64_t* inputBlock)
{
    uint64_t zero[128];
    for(int i = 0; i < 128; i++) zero[i] = 0;
    inputBlock[6]++;
    fillBlock(zero, inputBlock, addressBlock);
    fillBlock(zero, addressBlock, addressBlock);
}

} // namespace anonyme

// ---------------------------------------------------------------
// Noyau cooperatif : un warp entier (32 threads) = 4 hachages traites
// simultanement, chacun avec 8 threads cooperants (voir le
// commentaire precedent sur l'independance des rondes de fillBlock).
// Avant cette version, un bloc n'utilisait que 8 threads sur les 32
// que le materiel CUDA planifie de toute facon par "warp" - 75% de la
// capacite de calcul etait gaspillee. Ici, 4 hachages independants se
// partagent un meme warp complet, sans rien gaspiller.
// Declarations anticipees (definitions plus bas dans le fichier) -
// necessaires car le noyau principal les appelle avant leur definition.
__device__ __forceinline__ void compressBlockCoop(
    const uint64_t* __restrict__ prev, const uint64_t* __restrict__ ref,
    uint64_t* __restrict__ out, uint64_t* __restrict__ scratchR,
    uint64_t* __restrict__ scratchQ, unsigned int tid);
__device__ __forceinline__ void nextAddressesCoop(
    uint64_t* __restrict__ addressBlock, uint64_t* __restrict__ inputBlock,
    uint64_t* __restrict__ zeroBlock, uint64_t* __restrict__ scratchR,
    uint64_t* __restrict__ scratchQ, unsigned int tid);

__global__ void blocknetArgon2idKernel(
    const uint8_t* __restrict__ salt,
    uint32_t saltLen,
    const uint64_t* __restrict__ nonces,
    uint32_t mCostKib,
    uint32_t activeHashes,
    uint64_t* __restrict__ memoryPool,
    uint8_t* __restrict__ outHashes
)
{
    // Nombre de hachages par bloc calcule dynamiquement a partir de
    // la configuration de lancement reelle (blockDim.x / 8), plutot
    // que fige en dur - permet de varier ce paramtre (voir
    // computeBatch()) sans jamais desynchroniser ce calcul d'indice.
    unsigned int hashesPerBlock = blockDim.x / 32;
    unsigned int hashSlot = threadIdx.x / 32;
    unsigned int tid = threadIdx.x % 32;        // 0..31, role cooperatif
    unsigned int hashIdx = blockIdx.x * hashesPerBlock + hashSlot;
    if(hashIdx >= activeHashes) return;

    uint64_t* memory = memoryPool + static_cast<size_t>(hashIdx) * mCostKib * 128;

    // ATTENTION : ces tableaux sont dimensionnes pour EXACTEMENT le
    // nombre de hachages par bloc actuellement utilise cote hote
    // (computeBatch() : blockDim.x=8, donc hashesPerBlock=1). Si ce
    // nombre change un jour (ex: retour a 4 pour comparaison), ces
    // dimensions DOIVENT etre mises a jour en consequence, sinon
    // debordement silencieux. Reduit ici de 4 a 1 pour liberer la
    // memoire partagee gaspillee (mesure : permet davantage de blocs
    // residents simultanement par SM, donc un meilleur recouvrement
    // de la latence memoire).
    __shared__ uint64_t blockR[1][128];
    __shared__ uint64_t blockTmp[1][128];
    __shared__ uint64_t addressBlock[1][128];
    __shared__ uint64_t inputBlock[1][128];
    __shared__ uint64_t zeroBlockShared[128];  // bloc de zeros pour nextAddressesCoop
    __shared__ uint32_t sCurrOffset[1];
    __shared__ uint32_t sPrevOffset[1];
    __shared__ uint32_t sRefIndex[1];

    // --- Etape serielle (un representant par hash, tid==0) : H0 + blocs initiaux ---
    if(tid == 0)
    {
        uint8_t h0input[8 + 92 + 4 + 8];
        uint32_t pos = 0;
        auto put32 = [&](uint32_t v) {
            h0input[pos+0] = static_cast<uint8_t>(v & 0xff);
            h0input[pos+1] = static_cast<uint8_t>((v>>8) & 0xff);
            h0input[pos+2] = static_cast<uint8_t>((v>>16) & 0xff);
            h0input[pos+3] = static_cast<uint8_t>((v>>24) & 0xff);
            pos += 4;
        };
        put32(1); put32(32); put32(mCostKib); put32(1); put32(0x13); put32(2);
        put32(8);
        uint64_t nonce = nonces[hashIdx];
        for(int b = 0; b < 8; b++) h0input[pos++] = static_cast<uint8_t>((nonce >> (8*b)) & 0xff);
        put32(saltLen);
        for(uint32_t i = 0; i < saltLen; i++) h0input[pos++] = salt[i];
        put32(0); put32(0);

        uint8_t h0[64];
        blake2bSimple(h0, 64, h0input, pos);

        uint8_t seedInput[64 + 4 + 4];
        memcpy(seedInput, h0, 64);
        seedInput[64]=0; seedInput[65]=0; seedInput[66]=0; seedInput[67]=0;
        seedInput[68]=0; seedInput[69]=0; seedInput[70]=0; seedInput[71]=0;
        uint8_t block0bytes[1024];
        blake2bLong(block0bytes, 1024, seedInput, 72);
        memcpy(memory + 0, block0bytes, 1024);

        seedInput[64] = 1;
        uint8_t block1bytes[1024];
        blake2bLong(block1bytes, 1024, seedInput, 72);
        memcpy(memory + 128, block1bytes, 1024);

        for(int i = 0; i < 128; i++) inputBlock[hashSlot][i] = 0;
        inputBlock[hashSlot][0] = 0; inputBlock[hashSlot][1] = 0;
        inputBlock[hashSlot][3] = mCostKib; inputBlock[hashSlot][4] = 1; inputBlock[hashSlot][5] = 2;
    }
    __syncwarp(0xFFFFFFFFU);

    // Init cooperative du bloc de zeros (pour nextAddressesCoop).
    zeroBlockShared[tid]=0; zeroBlockShared[tid+32]=0;
    zeroBlockShared[tid+64]=0; zeroBlockShared[tid+96]=0;
    __syncwarp(0xFFFFFFFFU);

    const uint32_t segmentLength = mCostKib / 4;

    for(uint32_t slice = 0; slice < 4; slice++)
    {
        bool dataIndependent = (slice < 2);
        uint32_t startingIndex = (slice == 0) ? 2 : 0;

        if(dataIndependent)
        {
            if(tid == 0)
            {
                inputBlock[hashSlot][2] = slice;
                inputBlock[hashSlot][6] = 0;
            }
            __syncwarp(0xFFFFFFFFU);
            nextAddressesCoop(addressBlock[hashSlot], inputBlock[hashSlot],
                              zeroBlockShared, blockR[hashSlot], blockTmp[hashSlot], tid);
        }
        if(tid == 0)
        {
            sCurrOffset[hashSlot] = slice * segmentLength + startingIndex;
            sPrevOffset[hashSlot] = (sCurrOffset[hashSlot] == 0) ? (mCostKib - 1) : (sCurrOffset[hashSlot] - 1);
        }
        __syncwarp(0xFFFFFFFFU);

        for(uint32_t i = startingIndex; i < segmentLength; i++)
        {
            // Regeneration COOPERATIVE du bloc d'adresses (32 threads),
            // sortie du if(tid==0) pour que tous les threads participent.
            if(dataIndependent)
            {
                uint32_t addrIdx = i % 128;
                if(i != startingIndex && addrIdx == 0)
                {
                    nextAddressesCoop(addressBlock[hashSlot], inputBlock[hashSlot],
                                      zeroBlockShared, blockR[hashSlot], blockTmp[hashSlot], tid);
                }
            }
            if(tid == 0)
            {
                if(sCurrOffset[hashSlot] % mCostKib == 1) sPrevOffset[hashSlot] = sCurrOffset[hashSlot] - 1;

                uint64_t pseudoRand;
                if(dataIndependent)
                {
                    uint32_t addrIdx = i % 128;
                    pseudoRand = addressBlock[hashSlot][addrIdx];
                }
                else
                {
                    pseudoRand = memory[static_cast<size_t>(sPrevOffset[hashSlot]) * 128];
                }

                uint32_t referenceAreaSize = (slice == 0)
                    ? (i - 1)
                    : (slice * segmentLength + i - 1);
                uint32_t pseudoRand32 = static_cast<uint32_t>(pseudoRand & 0xFFFFFFFFULL);
                uint64_t relativePosition = pseudoRand32;
                relativePosition = (relativePosition * relativePosition) >> 32;
                relativePosition = referenceAreaSize - 1 -
                    ((static_cast<uint64_t>(referenceAreaSize) * relativePosition) >> 32);
                sRefIndex[hashSlot] = static_cast<uint32_t>(relativePosition) % mCostKib;
            }
            __syncwarp(0xFFFFFFFFU);

            // --- fillBlock cooperatif FUSIONNE (shuffle-warp, opti
            // inspiree de compress_block_coop / A82 de seine) : au lieu
            // de 4 sous-rondes separees par une barriere via memoire
            // partagee, on fusionne colonnes+diagonales de chaque phase
            // en gardant les valeurs en registres et en les echangeant
            // entre les 4 threads d'un groupe via __shfl_sync. Economise
            // 2 barrieres + de nombreux acces memoire partagee par bloc.
            // Valide octet par octet contre la reference sequentielle.
            // NB : phase "groupes contigus (16*state)" EN PREMIER (ce que
            // ref.c fait en premier), puis phase "motif espace (2*state)".
            const uint64_t* ref = memory + static_cast<size_t>(sRefIndex[hashSlot]) * 128;
            const uint64_t* prev = memory + static_cast<size_t>(sPrevOffset[hashSlot]) * 128;
            uint64_t* br = blockR[hashSlot];
            uint64_t* bt = blockTmp[hashSlot];
            {
                uint64_t x0 = ref[tid]    ^ prev[tid];
                uint64_t x1 = ref[tid+32] ^ prev[tid+32];
                uint64_t x2 = ref[tid+64] ^ prev[tid+64];
                uint64_t x3 = ref[tid+96] ^ prev[tid+96];
                br[tid]=x0; bt[tid]=x0;
                br[tid+32]=x1; bt[tid+32]=x1;
                br[tid+64]=x2; bt[tid+64]=x2;
                br[tid+96]=x3; bt[tid+96]=x3;
            }
            __syncwarp(0xFFFFFFFFU);

            {
                const unsigned int state = tid >> 2;
                const unsigned int lane  = tid & 3U;
                const unsigned int shflBase = state * 4U;

                // Phase 1 : groupes contigus de 16 mots (colonnes+diagonales fusionnees)
                {
                    const unsigned int base = state * 16U;
                    uint64_t a = br[base + lane];
                    uint64_t b = br[base + 4U + lane];
                    uint64_t c = br[base + 8U + lane];
                    uint64_t d = br[base + 12U + lane];
                    BLAMKA_G(a, b, c, d);
                    uint64_t a2 = a;
                    uint64_t b2 = __shfl_sync(0xFFFFFFFFU, b, shflBase + ((lane + 1U) & 3U));
                    uint64_t c2 = __shfl_sync(0xFFFFFFFFU, c, shflBase + ((lane + 2U) & 3U));
                    uint64_t d2 = __shfl_sync(0xFFFFFFFFU, d, shflBase + ((lane + 3U) & 3U));
                    BLAMKA_G(a2, b2, c2, d2);
                    br[base + lane]                = a2;
                    br[base + 4U + ((lane+1U)&3U)] = b2;
                    br[base + 8U + ((lane+2U)&3U)] = c2;
                    br[base + 12U + ((lane+3U)&3U)]= d2;
                }
                __syncwarp(0xFFFFFFFFU);

                // Phase 2 : motif espace (2*state) (colonnes+diagonales fusionnees)
                {
                    const unsigned int b0 = state * 2U;
                    const unsigned int colSub = (lane >> 1) * 16U + (lane & 1U);
                    uint64_t a = br[b0 + colSub];
                    uint64_t c = br[b0 + 32U + colSub];
                    uint64_t d = br[b0 + 64U + colSub];
                    uint64_t e = br[b0 + 96U + colSub];
                    BLAMKA_G(a, c, d, e);
                    uint64_t a2 = a;
                    uint64_t c2 = __shfl_sync(0xFFFFFFFFU, c, shflBase + ((lane + 1U) & 3U));
                    uint64_t d2 = __shfl_sync(0xFFFFFFFFU, d, shflBase + ((lane + 2U) & 3U));
                    uint64_t e2 = __shfl_sync(0xFFFFFFFFU, e, shflBase + ((lane + 3U) & 3U));
                    BLAMKA_G(a2, c2, d2, e2);
                    const unsigned int p0 = lane;
                    const unsigned int p1 = (lane + 1U) & 3U;
                    const unsigned int p2 = (lane + 2U) & 3U;
                    const unsigned int p3 = (lane + 3U) & 3U;
                    br[b0 + (p0>>1)*16U + (p0&1U)]       = a2;
                    br[b0 + 32U + (p1>>1)*16U + (p1&1U)] = c2;
                    br[b0 + 64U + (p2>>1)*16U + (p2&1U)] = d2;
                    br[b0 + 96U + (p3>>1)*16U + (p3&1U)] = e2;
                }
                __syncwarp(0xFFFFFFFFU);
            }

            uint64_t* curr = memory + static_cast<size_t>(sCurrOffset[hashSlot]) * 128;
            curr[tid]    = bt[tid]    ^ br[tid];
            curr[tid+32] = bt[tid+32] ^ br[tid+32];
            curr[tid+64] = bt[tid+64] ^ br[tid+64];
            curr[tid+96] = bt[tid+96] ^ br[tid+96];
            __syncwarp(0xFFFFFFFFU);

            if(tid == 0)
            {
                sCurrOffset[hashSlot]++;
                sPrevOffset[hashSlot]++;
            }
            __syncwarp(0xFFFFFFFFU);
        }
    }

    if(tid == 0)
    {
        uint64_t* lastBlock = memory + static_cast<size_t>(mCostKib - 1) * 128;
        blake2bLong(outHashes + hashIdx * 32, 32, reinterpret_cast<const uint8_t*>(lastBlock), 1024);
    }
}

// Precharge physiquement chaque page de 4 KiB du tampon memoire AVANT
// le vrai calcul Argon2id - cudaMalloc n'engage pas physiquement les
// pages tant qu'on n'y accede pas pour la premiere fois, et l'acces
// pseudo-aleatoire d'Argon2id sur l'ensemble des 2 GiB pourrait sinon
// declencher ce cout de "premier acces" de facon repetee tout au long
// du calcul chronometre lui-meme. Appele UNE SEULE FOIS a
// l'allocation (pas a chaque lot), le tampon persistant entre les
// appels a computeBatch().
__global__ void touchMemoryKernel(uint64_t* __restrict__ memory, size_t totalWords)
{
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    size_t stride = static_cast<size_t>(blockDim.x) * gridDim.x;
    for(size_t word = idx * 512; word < totalWords; word += stride * 512)
    {
        uint64_t v = memory[word];
        memory[word] = v;
    }
}

// Noyau de diagnostic isole : teste UNIQUEMENT blake2bSimple, sans
// aucune logique Argon2 - permet de comparer a un vecteur de test
// Blake2b standard et connu, independamment de tout le reste.
// Compression fusionnee cooperative (32 threads), extraite pour etre
// reutilisable : prev/ref -> out. scratchR/scratchQ sont des buffers de
// memoire partagee de 128 mots fournis par l'appelant. Meme logique que
// la boucle principale (opti A82 shuffle), validee octet par octet.
__device__ __forceinline__ void compressBlockCoop(
    const uint64_t* __restrict__ prev,
    const uint64_t* __restrict__ ref,
    uint64_t* __restrict__ out,
    uint64_t* __restrict__ scratchR,
    uint64_t* __restrict__ scratchQ,
    unsigned int tid
)
{
    uint64_t x0 = ref[tid]    ^ prev[tid];
    uint64_t x1 = ref[tid+32] ^ prev[tid+32];
    uint64_t x2 = ref[tid+64] ^ prev[tid+64];
    uint64_t x3 = ref[tid+96] ^ prev[tid+96];
    scratchR[tid]=x0; scratchQ[tid]=x0;
    scratchR[tid+32]=x1; scratchQ[tid+32]=x1;
    scratchR[tid+64]=x2; scratchQ[tid+64]=x2;
    scratchR[tid+96]=x3; scratchQ[tid+96]=x3;
    __syncwarp(0xFFFFFFFFU);

    const unsigned int state = tid >> 2;
    const unsigned int lane  = tid & 3U;
    const unsigned int shflBase = state * 4U;

    // Phase 1 : groupes contigus (16*state)
    {
        const unsigned int base = state * 16U;
        uint64_t a = scratchQ[base + lane];
        uint64_t b = scratchQ[base + 4U + lane];
        uint64_t c = scratchQ[base + 8U + lane];
        uint64_t d = scratchQ[base + 12U + lane];
        BLAMKA_G(a, b, c, d);
        uint64_t a2 = a;
        uint64_t b2 = __shfl_sync(0xFFFFFFFFU, b, shflBase + ((lane + 1U) & 3U));
        uint64_t c2 = __shfl_sync(0xFFFFFFFFU, c, shflBase + ((lane + 2U) & 3U));
        uint64_t d2 = __shfl_sync(0xFFFFFFFFU, d, shflBase + ((lane + 3U) & 3U));
        BLAMKA_G(a2, b2, c2, d2);
        scratchQ[base + lane]                = a2;
        scratchQ[base + 4U + ((lane+1U)&3U)] = b2;
        scratchQ[base + 8U + ((lane+2U)&3U)] = c2;
        scratchQ[base + 12U + ((lane+3U)&3U)]= d2;
    }
    __syncwarp(0xFFFFFFFFU);

    // Phase 2 : motif espace (2*state)
    {
        const unsigned int b0 = state * 2U;
        const unsigned int colSub = (lane >> 1) * 16U + (lane & 1U);
        uint64_t a = scratchQ[b0 + colSub];
        uint64_t c = scratchQ[b0 + 32U + colSub];
        uint64_t d = scratchQ[b0 + 64U + colSub];
        uint64_t e = scratchQ[b0 + 96U + colSub];
        BLAMKA_G(a, c, d, e);
        uint64_t a2 = a;
        uint64_t c2 = __shfl_sync(0xFFFFFFFFU, c, shflBase + ((lane + 1U) & 3U));
        uint64_t d2 = __shfl_sync(0xFFFFFFFFU, d, shflBase + ((lane + 2U) & 3U));
        uint64_t e2 = __shfl_sync(0xFFFFFFFFU, e, shflBase + ((lane + 3U) & 3U));
        BLAMKA_G(a2, c2, d2, e2);
        const unsigned int p0 = lane;
        const unsigned int p1 = (lane + 1U) & 3U;
        const unsigned int p2 = (lane + 2U) & 3U;
        const unsigned int p3 = (lane + 3U) & 3U;
        scratchQ[b0 + (p0>>1)*16U + (p0&1U)]       = a2;
        scratchQ[b0 + 32U + (p1>>1)*16U + (p1&1U)] = c2;
        scratchQ[b0 + 64U + (p2>>1)*16U + (p2&1U)] = d2;
        scratchQ[b0 + 96U + (p3>>1)*16U + (p3&1U)] = e2;
    }
    __syncwarp(0xFFFFFFFFU);

    out[tid]    = scratchR[tid]    ^ scratchQ[tid];
    out[tid+32] = scratchR[tid+32] ^ scratchQ[tid+32];
    out[tid+64] = scratchR[tid+64] ^ scratchQ[tid+64];
    out[tid+96] = scratchR[tid+96] ^ scratchQ[tid+96];
    __syncwarp(0xFFFFFFFFU);
}

// Version COOPERATIVE de nextAddresses (32 threads). addressBlock et
// inputBlock sont en memoire partagee (fournis par l'appelant), zero /
// scratchR / scratchQ aussi. Reproduit : input[6]++, addr=compress(zero,
// input), addr=compress(zero,addr).
__device__ __forceinline__ void nextAddressesCoop(
    uint64_t* __restrict__ addressBlock,
    uint64_t* __restrict__ inputBlock,
    uint64_t* __restrict__ zeroBlock,
    uint64_t* __restrict__ scratchR,
    uint64_t* __restrict__ scratchQ,
    unsigned int tid
)
{
    if(tid == 0) inputBlock[6]++;
    __syncwarp(0xFFFFFFFFU);
    compressBlockCoop(zeroBlock, inputBlock, addressBlock, scratchR, scratchQ, tid);
    compressBlockCoop(zeroBlock, addressBlock, addressBlock, scratchR, scratchQ, tid);
}

// DIAGNOSTIC : version cooperative, a valider contre l'etalon sequentiel.
__global__ void testNextAddrCoopKernel(uint64_t* inputInOut, uint64_t* addrOut)
{
    __shared__ uint64_t s_input[128];
    __shared__ uint64_t s_addr[128];
    __shared__ uint64_t s_zero[128];
    __shared__ uint64_t s_scratchR[128];
    __shared__ uint64_t s_scratchQ[128];
    unsigned int tid = threadIdx.x;

    s_input[tid]    = inputInOut[tid];
    s_input[tid+32] = inputInOut[tid+32];
    s_input[tid+64] = inputInOut[tid+64];
    s_input[tid+96] = inputInOut[tid+96];
    s_zero[tid]=0; s_zero[tid+32]=0; s_zero[tid+64]=0; s_zero[tid+96]=0;
    __syncwarp(0xFFFFFFFFU);

    nextAddressesCoop(s_addr, s_input, s_zero, s_scratchR, s_scratchQ, tid);

    addrOut[tid]    = s_addr[tid];
    addrOut[tid+32] = s_addr[tid+32];
    addrOut[tid+64] = s_addr[tid+64];
    addrOut[tid+96] = s_addr[tid+96];
}

// DIAGNOSTIC : appelle notre nextAddresses SEQUENTIEL actuel (1 thread),
// pour confirmer que la reference independante modelise bien le vrai code
// avant de construire/valider une version cooperative.
__global__ void testNextAddrSeqKernel(uint64_t* inputInOut, uint64_t* addrOut)
{
    if(threadIdx.x == 0)
    {
        nextAddresses(addrOut, inputInOut);
    }
}

__global__ void testBlake2bKernel(uint8_t* out, const uint8_t* in, uint32_t inlen, uint32_t outlen)
{
    blake2bSimple(out, outlen, in, inlen);
}

// Noyau de diagnostic : calcule UNIQUEMENT H0 (meme construction que
// dans le noyau principal, copiee ici a l'identique pour isolation),
// sans aller plus loin - permet de comparer directement a un calcul
// independant (Python/hashlib) du meme H0.
__global__ void testH0Kernel(
    uint8_t* h0out,
    const uint8_t* salt, uint32_t saltLen,
    uint64_t nonce, uint32_t mCostKib
)
{
    uint8_t h0input[8 + 92 + 4 + 8];
    uint32_t pos = 0;
    auto put32 = [&](uint32_t v) {
        h0input[pos+0] = static_cast<uint8_t>(v & 0xff);
        h0input[pos+1] = static_cast<uint8_t>((v>>8) & 0xff);
        h0input[pos+2] = static_cast<uint8_t>((v>>16) & 0xff);
        h0input[pos+3] = static_cast<uint8_t>((v>>24) & 0xff);
        pos += 4;
    };
    put32(1); put32(32); put32(mCostKib); put32(1); put32(0x13); put32(2);
    put32(8);
    for(int b = 0; b < 8; b++) h0input[pos++] = static_cast<uint8_t>((nonce >> (8*b)) & 0xff);
    put32(saltLen);
    for(uint32_t i = 0; i < saltLen; i++) h0input[pos++] = salt[i];
    put32(0); put32(0);

    blake2bSimple(h0out, 64, h0input, pos);
}

// Noyau de diagnostic : calcule UNIQUEMENT block0 a partir d'un H0
// deja connu (fourni directement, pas recalcule) - isole precisement
// la logique blake2bLong (le chemin "sortie longue, chainee") pour
// comparaison independante.
__global__ void testBlock0Kernel(uint8_t* block0out, const uint8_t* h0in)
{
    uint8_t seedInput[64 + 4 + 4];
    for(int i = 0; i < 64; i++) seedInput[i] = h0in[i];
    seedInput[64]=0; seedInput[65]=0; seedInput[66]=0; seedInput[67]=0;
    seedInput[68]=0; seedInput[69]=0; seedInput[70]=0; seedInput[71]=0;
    blake2bLong(block0out, 1024, seedInput, 72);
}

// Noyau de diagnostic : appelle DIRECTEMENT notre fillBlock() (la
// fonction sequentielle a un seul thread, pas la version cooperative
// utilisee dans le noyau principal) sur deux blocs fournis - isole
// precisement le melange BlaMka de tout le reste.
__global__ void testFillBlockKernel(
    uint64_t* __restrict__ out,
    const uint64_t* __restrict__ prev,
    const uint64_t* __restrict__ ref
)
{
    fillBlock(prev, ref, out);
}

// Noyau de diagnostic : reproduit EXACTEMENT le code cooperatif de la
// boucle principale (32 threads, memoire partagee, __syncwarp reel),
// sur les memes deux blocs de test - permet de comparer directement
// a la version sequentielle deja validee comme correcte, isolant
// precisement si le bug vient de LA PARALLELISATION elle-meme.
__global__ void testFillBlockCoopKernel(
    uint64_t* __restrict__ memory, // memory[0]=prev, memory[1]=ref, memory[2]=curr (out)
    uint64_t* __restrict__ outResult
)
{
    unsigned int tid = threadIdx.x;
    __shared__ uint64_t blockR[128];
    __shared__ uint64_t blockTmp[128];

    const uint64_t* ref = memory + 1 * 128;
    const uint64_t* prev = memory + 0 * 128;
    blockR[tid]      = ref[tid]      ^ prev[tid];
    blockR[tid+32]   = ref[tid+32]   ^ prev[tid+32];
    blockR[tid+64]   = ref[tid+64]   ^ prev[tid+64];
    blockR[tid+96]   = ref[tid+96]   ^ prev[tid+96];
    blockTmp[tid]    = blockR[tid];
    blockTmp[tid+32] = blockR[tid+32];
    blockTmp[tid+64] = blockR[tid+64];
    blockTmp[tid+96] = blockR[tid+96];
    __syncwarp(0xFFFFFFFFU);

    {
        unsigned int group = tid / 4;
        unsigned int subop = tid % 4;
        uint64_t* v = blockR + 16 * group;
        if(subop == 0)      { BLAMKA_G(v[0], v[4], v[8],  v[12]); }
        else if(subop == 1) { BLAMKA_G(v[1], v[5], v[9],  v[13]); }
        else if(subop == 2) { BLAMKA_G(v[2], v[6], v[10], v[14]); }
        else                { BLAMKA_G(v[3], v[7], v[11], v[15]); }
    }
    __syncwarp(0xFFFFFFFFU);
    {
        unsigned int group = tid / 4;
        unsigned int subop = tid % 4;
        uint64_t* v = blockR + 16 * group;
        if(subop == 0)      { BLAMKA_G(v[0], v[5], v[10], v[15]); }
        else if(subop == 1) { BLAMKA_G(v[1], v[6], v[11], v[12]); }
        else if(subop == 2) { BLAMKA_G(v[2], v[7], v[8],  v[13]); }
        else                { BLAMKA_G(v[3], v[4], v[9],  v[14]); }
    }
    __syncwarp(0xFFFFFFFFU);

    {
        unsigned int group = tid / 4;
        unsigned int subop = tid % 4;
        int base = 2 * static_cast<int>(group);
        int idx[16] = {
            base, base+1, base+16, base+17,
            base+32, base+33, base+48, base+49,
            base+64, base+65, base+80, base+81,
            base+96, base+97, base+112, base+113
        };
        if(subop == 0)      { BLAMKA_G(blockR[idx[0]], blockR[idx[4]], blockR[idx[8]],  blockR[idx[12]]); }
        else if(subop == 1) { BLAMKA_G(blockR[idx[1]], blockR[idx[5]], blockR[idx[9]],  blockR[idx[13]]); }
        else if(subop == 2) { BLAMKA_G(blockR[idx[2]], blockR[idx[6]], blockR[idx[10]], blockR[idx[14]]); }
        else                { BLAMKA_G(blockR[idx[3]], blockR[idx[7]], blockR[idx[11]], blockR[idx[15]]); }
    }
    __syncwarp(0xFFFFFFFFU);
    {
        unsigned int group = tid / 4;
        unsigned int subop = tid % 4;
        int base = 2 * static_cast<int>(group);
        int idx[16] = {
            base, base+1, base+16, base+17,
            base+32, base+33, base+48, base+49,
            base+64, base+65, base+80, base+81,
            base+96, base+97, base+112, base+113
        };
        if(subop == 0)      { BLAMKA_G(blockR[idx[0]], blockR[idx[5]], blockR[idx[10]], blockR[idx[15]]); }
        else if(subop == 1) { BLAMKA_G(blockR[idx[1]], blockR[idx[6]], blockR[idx[11]], blockR[idx[12]]); }
        else if(subop == 2) { BLAMKA_G(blockR[idx[2]], blockR[idx[7]], blockR[idx[8]],  blockR[idx[13]]); }
        else                { BLAMKA_G(blockR[idx[3]], blockR[idx[4]], blockR[idx[9]],  blockR[idx[14]]); }
    }
    __syncwarp(0xFFFFFFFFU);

    outResult[tid]    = blockTmp[tid]    ^ blockR[tid];
    outResult[tid+32] = blockTmp[tid+32] ^ blockR[tid+32];
    outResult[tid+64] = blockTmp[tid+64] ^ blockR[tid+64];
    outResult[tid+96] = blockTmp[tid+96] ^ blockR[tid+96];
}

// ---------------------------------------------------------------
// Wrapper hote (C++)
// ---------------------------------------------------------------

struct BlocknetGpuHasher::Impl
{
    int deviceIndex;
    size_t batchSizeValue;
    uint32_t mCostKib;

    uint64_t* d_memoryPool = nullptr;
    uint64_t* d_nonces = nullptr;
    uint8_t* d_salt = nullptr;
    uint8_t* d_outHashes = nullptr;

    std::vector<uint64_t> h_nonces;
    std::vector<uint8_t> h_outHashes;
    std::vector<uint8_t> h_salt;

    Impl(int deviceIndex_, size_t batchSizeArg, uint32_t mCostKib_)
    : deviceIndex(deviceIndex_), batchSizeValue(batchSizeArg), mCostKib(mCostKib_)
    {
        cudaError_t err = cudaSetDevice(deviceIndex);
        if(err != cudaSuccess)
        {
            throw std::runtime_error(std::string("cudaSetDevice failed: ") + cudaGetErrorString(err));
        }

        size_t memoryBytes = static_cast<size_t>(batchSizeValue) * mCostKib * 128 * sizeof(uint64_t);
        err = cudaMalloc(&d_memoryPool, memoryBytes);
        if(err != cudaSuccess)
        {
            throw std::runtime_error(std::string("cudaMalloc (memory pool) failed: ") + cudaGetErrorString(err));
        }

        // Prechargement physique unique de tout le tampon (voir le
        // commentaire du noyau touchMemoryKernel plus haut).
        size_t totalWords = static_cast<size_t>(batchSizeValue) * mCostKib * 128;
        unsigned int touchThreads = 256;
        unsigned int touchBlocks = 256;
        touchMemoryKernel<<<touchBlocks, touchThreads>>>(d_memoryPool, totalWords);
        cudaError_t touchErr = cudaDeviceSynchronize();
        if(touchErr != cudaSuccess)
        {
            throw std::runtime_error(std::string("touchMemoryKernel failed: ") + cudaGetErrorString(touchErr));
        }

        cudaMalloc(&d_nonces, batchSizeValue * sizeof(uint64_t));
        cudaMalloc(&d_salt, 92);
        cudaMalloc(&d_outHashes, batchSizeValue * 32);

        h_nonces.resize(batchSizeValue, 0);
        h_outHashes.resize(batchSizeValue * 32, 0);
        h_salt.resize(92, 0);
    }

    ~Impl()
    {
        if(d_memoryPool) cudaFree(d_memoryPool);
        if(d_nonces) cudaFree(d_nonces);
        if(d_salt) cudaFree(d_salt);
        if(d_outHashes) cudaFree(d_outHashes);
    }
};

BlocknetGpuHasher::BlocknetGpuHasher(
    int deviceIndex,
    size_t batchSizeArg,
    uint32_t /*tCost*/,
    uint32_t mCostKib
)
{
    impl_ = std::make_unique<Impl>(deviceIndex, batchSizeArg, mCostKib);
}

BlocknetGpuHasher::~BlocknetGpuHasher() = default;

void BlocknetGpuHasher::setPassword(size_t index, const uint8_t* data, size_t len)
{
    uint64_t nonce = 0;
    size_t n = (len < 8) ? len : 8;
    for(size_t b = 0; b < n; b++)
    {
        nonce |= static_cast<uint64_t>(data[b]) << (8 * b);
    }
    impl_->h_nonces[index] = nonce;
}

void BlocknetGpuHasher::setSalt(const uint8_t* data, size_t len)
{
    size_t n = (len < 92) ? len : 92;
    for(size_t i = 0; i < n; i++) impl_->h_salt[i] = data[i];
    cudaMemcpy(impl_->d_salt, impl_->h_salt.data(), 92, cudaMemcpyHostToDevice);
}

void BlocknetGpuHasher::computeBatch()
{
    cudaSetDevice(impl_->deviceIndex);
    cudaMemcpy(impl_->d_nonces, impl_->h_nonces.data(),
               impl_->batchSizeValue * sizeof(uint64_t), cudaMemcpyHostToDevice);

    // 1 hachage par bloc (8 threads cooperants) - maximise le nombre
    // de blocs actifs simultanement, donc le nombre de multiprocesseurs
    // de flux (SM) reellement utilises. Avec un lot limite par la VRAM
    // (quelques hachages seulement, bien moins que le nombre de SM
    // d'un GPU moderne), maximiser la largeur (nombre de blocs) prime
    // sur l'efficacite interne a chaque bloc (occupation du warp) -
    // voir la discussion de session sur ce compromis.
    // 32 threads cooperants par hachage (exploite les 32 operations
    // veritablement paralleles par phase - voir le noyau), 1 hachage
    // par bloc pour maximiser le nombre de SM actifs.
    unsigned int threadsPerBlock = 32;
    unsigned int blocks = static_cast<unsigned int>(impl_->batchSizeValue);

    blocknetArgon2idKernel<<<blocks, threadsPerBlock>>>(
        impl_->d_salt, 92,
        impl_->d_nonces,
        impl_->mCostKib,
        static_cast<uint32_t>(impl_->batchSizeValue),
        impl_->d_memoryPool,
        impl_->d_outHashes
    );
    cudaError_t launchErr = cudaGetLastError();
    if(launchErr != cudaSuccess)
    {
        throw std::runtime_error(std::string("kernel launch failed: ") + cudaGetErrorString(launchErr));
    }
    cudaError_t syncErr = cudaDeviceSynchronize();
    if(syncErr != cudaSuccess)
    {
        throw std::runtime_error(std::string("kernel execution failed: ") + cudaGetErrorString(syncErr));
    }

    cudaMemcpy(impl_->h_outHashes.data(), impl_->d_outHashes,
               impl_->batchSizeValue * 32, cudaMemcpyDeviceToHost);
}

void BlocknetGpuHasher::getHash(size_t index, uint8_t* out) const
{
    memcpy(out, impl_->h_outHashes.data() + index * 32, 32);
}

size_t BlocknetGpuHasher::batchSize() const
{
    return impl_->batchSizeValue;
}

std::vector<uint8_t> BlocknetGpuHasher::debugDumpMemory(size_t index, size_t mCostKib) const
{
    size_t bytesPerHash = mCostKib * 128 * sizeof(uint64_t);
    std::vector<uint8_t> result(bytesPerHash);
    cudaMemcpy(
        result.data(),
        reinterpret_cast<uint8_t*>(impl_->d_memoryPool) + index * bytesPerHash,
        bytesPerHash,
        cudaMemcpyDeviceToHost
    );
    return result;
}

int blocknetGpuDeviceCount()
{
    int count = 0;
    cudaGetDeviceCount(&count);
    return count;
}

void blocknetGpuMemoryInfo(int deviceIndex, size_t& freeBytes, size_t& totalBytes)
{
    cudaSetDevice(deviceIndex);
    cudaMemGetInfo(&freeBytes, &totalBytes);
}
