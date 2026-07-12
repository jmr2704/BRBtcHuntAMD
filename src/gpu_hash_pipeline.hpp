// gpu_hash_pipeline.hpp — High-level GPU hash pipeline: SHA-256 + RIPEMD-160
// Composes gpu_sha256, gpu_ripemd160, and gpu_utils primitives.
// Included directly by HashPipeline.cpp (single-TU style).
#pragma once

// Forward declarations (if included via extern "C" elsewhere)
__device__ void getSHA256_33bytes(const uint8_t* pubkey33, uint8_t sha[32]);
__device__ void getRIPEMD160_32bytes(const uint8_t* sha, uint8_t ripemd[20]);
__device__ void getHash160_33bytes(const uint8_t* pubkey33, uint8_t* hash20);

__device__ __forceinline__ void getSHA256_33bytes(const uint8_t* pubkey33, uint8_t sha[32]) {
    uint32_t M[16];
#pragma unroll
    for (int i = 0; i < 16; ++i) M[i] = 0;

#pragma unroll
    for (int i = 0; i < 33; ++i) {
        M[i >> 2] |= (uint32_t)pubkey33[i] << (24 - ((i & 3) << 3));
    }
    M[8] |= (uint32_t)0x80 << (24 - ((33 & 3) << 3));
    M[14] = 0;
    M[15] = 33u * 8u;

    uint32_t state[8];
    SHA256Initialize(state);
    SHA256Transform(state, M);

#pragma unroll
    for (int i = 0; i < 8; ++i) {
        sha[4 * i + 0] = (uint8_t)(state[i] >> 24);
        sha[4 * i + 1] = (uint8_t)(state[i] >> 16);
        sha[4 * i + 2] = (uint8_t)(state[i] >> 8);
        sha[4 * i + 3] = (uint8_t)(state[i] >> 0);
    }
}

__device__ __forceinline__ void getRIPEMD160_32bytes(const uint8_t* sha, uint8_t ripemd[20])
{
    uint8_t block[64] = {0};
    
    for (int i = 0; i < 32; i++) {
    block[i] = sha[i];
    }  
    block[32] = 0x80;
    const uint32_t bitLen = 256;  

    block[56] = bitLen & 0xFF;
    block[57] = (bitLen >> 8) & 0xFF;
    block[58] = (bitLen >> 16) & 0xFF;
    block[59] = (bitLen >> 24) & 0xFF;

    uint32_t W[16];
    
    for (int i = 0; i < 16; i++) {
        W[i] = ((uint32_t)block[4*i+3] << 24) |
               ((uint32_t)block[4*i+2] << 16) |
               ((uint32_t)block[4*i+1] << 8) |
               ((uint32_t)block[4*i]);
    }

    uint32_t state[5];
    RIPEMD160Initialize(state);
    RIPEMD160Transform(state, W);
   
    for (int i = 0; i < 5; i++) {
        ripemd[4*i]   = (state[i] >> 0) & 0xFF;
        ripemd[4*i+1] = (state[i] >> 8) & 0xFF;
        ripemd[4*i+2] = (state[i] >> 16) & 0xFF;
        ripemd[4*i+3] = (state[i] >> 24) & 0xFF;
    }
}

__device__ void getHash160_33bytes(const uint8_t* pubkey33, uint8_t* hash20);

__device__  void getHash160_33bytes(const uint8_t* pubkey33, uint8_t* hash20)
{
    uint8_t sha256[32];
    getSHA256_33bytes(pubkey33, sha256);
    getRIPEMD160_32bytes(sha256, hash20);
}

__device__ __forceinline__ void SHA256_33_from_limbs(uint8_t prefix02_03, const uint64_t x_be_limbs[4], uint32_t out_state[8]){
    const uint64_t v3 = x_be_limbs[3];
    const uint64_t v2 = x_be_limbs[2];
    const uint64_t v1 = x_be_limbs[1];
    const uint64_t v0 = x_be_limbs[0];
    uint32_t M[16];
    M[0] = pack_be4(prefix02_03, (uint8_t)(v3>>56), (uint8_t)(v3>>48), (uint8_t)(v3>>40));
    M[1] = pack_be4((uint8_t)(v3>>32), (uint8_t)(v3>>24), (uint8_t)(v3>>16), (uint8_t)(v3>>8));
    M[2] = pack_be4((uint8_t)(v3>>0), (uint8_t)(v2>>56), (uint8_t)(v2>>48), (uint8_t)(v2>>40));
    M[3] = pack_be4((uint8_t)(v2>>32), (uint8_t)(v2>>24), (uint8_t)(v2>>16), (uint8_t)(v2>>8));
    M[4] = pack_be4((uint8_t)(v2>>0), (uint8_t)(v1>>56), (uint8_t)(v1>>48), (uint8_t)(v1>>40));
    M[5] = pack_be4((uint8_t)(v1>>32), (uint8_t)(v1>>24), (uint8_t)(v1>>16), (uint8_t)(v1>>8));
    M[6] = pack_be4((uint8_t)(v1>>0), (uint8_t)(v0>>56), (uint8_t)(v0>>48), (uint8_t)(v0>>40));
    M[7] = pack_be4((uint8_t)(v0>>32), (uint8_t)(v0>>24), (uint8_t)(v0>>16), (uint8_t)(v0>>8));
    M[8] = pack_be4((uint8_t)(v0>>0), 0x80u, 0x00u, 0x00u);
#pragma unroll
    for(int i=9;i<16;++i) M[i]=0;
    M[15] = 33u*8u;
    uint32_t st[8];
    SHA256Initialize(st);
    SHA256Transform(st, M);
#pragma unroll
    for(int i=0;i<8;++i) out_state[i]=st[i];
}

__device__ __forceinline__ void RIPEMD160_from_SHA256_state(const uint32_t sha_state_be[8],
                                                            uint8_t ripemd20[20])
{
    uint32_t W[16];
#pragma unroll
    for(int i=0;i<8;++i) W[i] = bswap32(sha_state_be[i]);
    W[8]  = 0x00000080u;
#pragma unroll
    for(int i=9;i<14;++i) W[i]=0;
    W[14] = 256u;
    W[15] = 0u;

    uint32_t s[5];
    RIPEMD160Initialize(s);
    RIPEMD160Transform(s, W);
#pragma unroll
    for (int i = 0; i < 5; ++i) {
        ripemd20[4*i+0] = (uint8_t)(s[i] >> 0);
        ripemd20[4*i+1] = (uint8_t)(s[i] >> 8);
        ripemd20[4*i+2] = (uint8_t)(s[i] >>16);
        ripemd20[4*i+3] = (uint8_t)(s[i] >>24);
    }
}

__device__ __forceinline__ void RIPEMD160_from_SHA256_state_words(const uint32_t sha_state_be[8],
                                                                  uint32_t h160[5])
{
    uint32_t W[16];
#pragma unroll
    for(int i=0;i<8;++i) W[i] = bswap32(sha_state_be[i]);
    W[8]  = 0x00000080u;
#pragma unroll
    for(int i=9;i<14;++i) W[i]=0;
    W[14] = 256u;
    W[15] = 0u;

    uint32_t s[5];
    RIPEMD160Initialize(s);
    RIPEMD160Transform(s, W);
#pragma unroll
    for (int i = 0; i < 5; ++i) h160[i] = s[i];
}

__device__ __forceinline__ void getHash160_33_from_limbs(uint8_t prefix02_03,
                                                         const uint64_t x_be_limbs[4],
                                                         uint8_t out20[20])
{
    uint32_t sha_state[8];
    SHA256_33_from_limbs(prefix02_03, x_be_limbs, sha_state);
    RIPEMD160_from_SHA256_state(sha_state, out20);
}

__device__ __forceinline__ bool getHash160_33_from_limbs_matches(uint8_t prefix02_03,
                                                                 const uint64_t x_be_limbs[4],
                                                                 const uint8_t target_hash160[20],
                                                                 uint32_t target_prefix_le,
                                                                 uint32_t h160_out[5])
{
    uint32_t sha_state[8];
    uint32_t h160[5];
    SHA256_33_from_limbs(prefix02_03, x_be_limbs, sha_state);
    RIPEMD160_from_SHA256_state_words(sha_state, h160);

    if (h160_out) {
        h160_out[0] = h160[0]; h160_out[1] = h160[1];
        h160_out[2] = h160[2]; h160_out[3] = h160[3];
        h160_out[4] = h160[4];
    }

    if (h160[0] != target_prefix_le) return false;
    return h160[1] == hash_load_u32_le(target_hash160 + 4) &&
           h160[2] == hash_load_u32_le(target_hash160 + 8) &&
           h160[3] == hash_load_u32_le(target_hash160 + 12) &&
           h160[4] == hash_load_u32_le(target_hash160 + 16);
}
