// gpu_sha256.hpp — SHA-256 compression function primitives (device)
// Included directly by HashPipeline.cpp (single-TU style).
#pragma once

__device__ __forceinline__ uint32_t ror32(uint32_t x, int n)
{
return (x >> n) | (x << (32 - n));
}

__device__ __forceinline__ uint32_t bigS0(uint32_t x) { return ror32(x, 2) ^ ror32(x, 13) ^ ror32(x, 22); }
__device__ __forceinline__ uint32_t bigS1(uint32_t x) { return ror32(x, 6) ^ ror32(x, 11) ^ ror32(x, 25); }
__device__ __forceinline__ uint32_t smallS0(uint32_t x){ return ror32(x, 7) ^ ror32(x, 18) ^ (x >> 3); }
__device__ __forceinline__ uint32_t smallS1(uint32_t x){ return ror32(x,17) ^ ror32(x, 19) ^ (x >>10); }

__device__ __forceinline__ uint32_t Ch (uint32_t x,uint32_t y,uint32_t z){ return (x & y) ^ (~x & z); }
__device__ __forceinline__ uint32_t Maj(uint32_t x,uint32_t y,uint32_t z){ return (x & y) | (x & z) | (y & z); }

__device__ __constant__ uint32_t K[64] = {
    0x428A2F98,0x71374491,0xB5C0FBCF,0xE9B5DBA5,0x3956C25B,0x59F111F1,0x923F82A4,0xAB1C5ED5,
    0xD807AA98,0x12835B01,0x243185BE,0x550C7DC3,0x72BE5D74,0x80DEB1FE,0x9BDC06A7,0xC19BF174,
    0xE49B69C1,0xEFBE4786,0x0FC19DC6,0x240CA1CC,0x2DE92C6F,0x4A7484AA,0x5CB0A9DC,0x76F988DA,
    0x983E5152,0xA831C66D,0xB00327C8,0xBF597FC7,0xC6E00BF3,0xD5A79147,0x06CA6351,0x14292967,
    0x27B70A85,0x2E1B2138,0x4D2C6DFC,0x53380D13,0x650A7354,0x766A0ABB,0x81C2C92E,0x92722C85,
    0xA2BFE8A1,0xA81A664B,0xC24B8B70,0xC76C51A3,0xD192E819,0xD6990624,0xF40E3585,0x106AA070,
    0x19A4C116,0x1E376C08,0x2748774C,0x34B0BCB5,0x391C0CB3,0x4ED8AA4A,0x5B9CCA4F,0x682E6FF3,
    0x748F82EE,0x78A5636F,0x84C87814,0x8CC70208,0x90BEFFFA,0xA4506CEB,0xBEF9A3F7,0xC67178F2
};

__device__ __forceinline__ void SHA256Initialize(uint32_t s[8])
{
    s[0] = 0x6a09e667ul;
    s[1] = 0xbb67ae85ul;
    s[2] = 0x3c6ef372ul;
    s[3] = 0xa54ff53aul;
    s[4] = 0x510e527ful;
    s[5] = 0x9b05688cul;
    s[6] = 0x1f83d9abul;
    s[7] = 0x5be0cd19ul;
}

__device__ __forceinline__ void SHA256Transform(uint32_t state[8], const uint32_t W_in[16])
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    uint32_t w[16];
#pragma unroll
    for (int i = 0; i < 16; ++i) w[i] = W_in[i];

#pragma unroll 64
    for (int t = 0; t < 64; ++t) {
        if (t >= 16) {
            uint32_t s0 = smallS0(w[(t + 1)  & 15]);
            uint32_t s1 = smallS1(w[(t + 14) & 15]);
            uint32_t newW = w[t & 15] + s1 + w[(t + 9) & 15] + s0;
            w[t & 15] = newW;
        }
        uint32_t Wt = w[t & 15];
        uint32_t T1 = h + bigS1(e) + Ch(e, f, g) + K[t] + Wt;
        uint32_t T2 = bigS0(a) + Maj(a, b, c);

        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}
