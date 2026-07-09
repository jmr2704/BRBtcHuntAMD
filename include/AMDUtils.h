/*
 * AMDUtils.h — HIP port of CUDAUtils.h
 *
 * Portable utility functions for AMD GPU (HIP/ROCm).
 * Warp intrinsics use the HIP / Clang builtin equivalents.
 */

#ifndef AMD_UTILS_H
#define AMD_UTILS_H

#include <cstdint>
#include <cstring>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#include <string>
#include "GpuPlatform.h"
#include <sstream>
#include <iomanip>
#include <iostream>

// ── 256-bit arithmetic (host-side) ──────────────────────────────────────

__host__ __forceinline__ void add256_u64(const uint64_t a[4], uint64_t b, uint64_t out[4]) {
    out[0] = a[0] + b;
    uint64_t carry = (out[0] < a[0]) ? 1ULL : 0ULL;
    for (int i = 1; i < 4; ++i) {
        out[i] = a[i] + carry;
        carry = (out[i] < a[i]) ? 1ULL : 0ULL;
    }
}

__host__ __forceinline__ void add256(const uint64_t a[4], const uint64_t b[4], uint64_t out[4]) {
    uint64_t carry = 0;
    for (int i = 0; i < 4; ++i) {
        uint64_t s = a[i] + b[i];
        uint64_t c = (s < a[i]) ? 1ULL : 0ULL;
        uint64_t s2 = s + carry;
        if (s2 < s) c = 1ULL;
        out[i] = s2;
        carry = c;
    }
}

__host__ __forceinline__ void sub256(const uint64_t a[4], const uint64_t b[4], uint64_t out[4]) {
    uint64_t borrow = 0;
    for (int i = 0; i < 4; ++i) {
        uint64_t diff = a[i] - borrow;
        uint64_t nb = (diff > a[i]) ? 1ULL : 0ULL;
        uint64_t diff2 = diff - b[i];
        if (diff2 > diff) nb = 1ULL;
        out[i] = diff2;
        borrow = nb;
    }
}

__host__ __forceinline__ void inc256(uint64_t a[4], uint64_t inc) {
    a[0] += inc;
    uint64_t carry = (a[0] < inc) ? 1ULL : 0ULL;
    for (int i = 1; i < 4 && carry; ++i) {
        ++a[i];
        carry = (a[i] == 0ULL) ? 1ULL : 0ULL;
    }
}

__host__ inline void divmod_256_by_u64(const uint64_t value[4], uint64_t divisor,
                                 uint64_t quotient[4], uint64_t &remainder) {
#if defined(_MSC_VER) && !defined(__clang__)
    remainder = 0;
    for (int i = 3; i >= 0; --i) {
        quotient[i] = _udiv128(remainder, value[i], divisor, &remainder);
    }
#else
    remainder = 0;
    for (int i = 3; i >= 0; --i) {
        __uint128_t cur = (__uint128_t(remainder) << 64) | value[i];
        quotient[i] = (uint64_t)(cur / divisor);
        remainder = (uint64_t)(cur % divisor);
    }
#endif
}

// ── Hex/format utilities ───────────────────────────────────────────────

inline bool hexToLE64(const std::string& h_in, uint64_t w[4]) {
    std::string h = h_in;
    if (h.size() >= 2 && (h[0] == '0') && (h[1] == 'x' || h[1] == 'X')) h = h.substr(2);
    if (h.size() > 64) return false;
    if (h.size() < 64) h = std::string(64 - h.size(), '0') + h;
    if (h.size() != 64) return false;
    for (int i = 0; i < 4; ++i) {
        std::string part = h.substr(i * 16, 16);
        w[3 - i] = std::stoull(part, nullptr, 16);
    }
    return true;
}

inline bool hexToHash160(const std::string& h, uint8_t hash160[20]) {
    if (h.size() != 40) return false;
    for (int i = 0; i < 20; ++i) {
        std::string byteStr = h.substr(i * 2, 2);
        hash160[i] = (uint8_t)std::stoul(byteStr, nullptr, 16);
    }
    return true;
}

inline std::string formatHex256(const uint64_t limbs[4]) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill('0');
    for (int i = 3; i >= 0; --i) oss << std::setw(16) << limbs[i];
    return oss.str();
}

// ── Device utilities ───────────────────────────────────────────────────

__device__ __forceinline__ void inc256_device(uint64_t a[4], uint64_t inc) {
    uint64_t sum = a[0] + inc;
    a[0] = sum;
    uint64_t carry = (sum < inc) ? 1ULL : 0ULL;
    for (int i = 1; i < 4 && carry; ++i) {
        ++a[i];
        carry = (a[i] == 0ULL) ? 1ULL : 0ULL;
    }
}

static __device__ __forceinline__ uint32_t load_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static __device__ __forceinline__ bool hash160_matches_prefix_then_full(
    const uint8_t* __restrict__ h,
    const uint8_t* __restrict__ target,
    const uint32_t target_prefix_le)
{
    if (load_u32_le(h) != target_prefix_le) return false;
#pragma unroll
    for (int k = 4; k < 20; ++k) {
        if (h[k] != target[k]) return false;
    }
    return true;
}

__device__ __forceinline__ bool eq256_u64(const uint64_t a[4], uint64_t b) {
    return (a[0]==b) & (a[1]==0ull) & (a[2]==0ull) & (a[3]==0ull);
}

static __device__ __forceinline__ bool hash160_prefix_equals(
    const uint8_t* __restrict__ h, uint32_t target_prefix)
{
    return load_u32_le(h) == target_prefix;
}

__device__ __forceinline__ bool ge256_u64(const uint64_t a[4], uint64_t b) {
    if (a[3] | a[2] | a[1]) return true;
    return a[0] >= b;
}

__device__ __forceinline__ void sub256_u64_inplace(uint64_t a[4], uint64_t dec) {
    uint64_t borrow = (a[0] < dec) ? 1ull : 0ull;
    a[0] = a[0] - dec;
#pragma unroll
    for (int i = 1; i < 4; ++i) {
        uint64_t ai = a[i];
        uint64_t bi = borrow;
        a[i] = ai - bi;
        borrow = (ai < bi) ? 1ull : 0ull;
        if (!borrow) break;
    }
}

// ── Warp reduce (HIP compatible: __shfl_down_sync → __hip_shfl_down) ──
// The HIP compiler supports __shfl_down_sync for CUDA compatibility.
// For older HIP versions, use __hip_shfl_down.
__device__ __forceinline__ unsigned long long warp_reduce_add_ull(unsigned long long v) {
    uint64_t mask = 0xFFFFFFFFull;
    v += BTC_SHFL_DOWN_SYNC(mask, v, 16);
    v += BTC_SHFL_DOWN_SYNC(mask, v, 8);
    v += BTC_SHFL_DOWN_SYNC(mask, v, 4);
    v += BTC_SHFL_DOWN_SYNC(mask, v, 2);
    v += BTC_SHFL_DOWN_SYNC(mask, v, 1);
    return v;
}

// ── Host utilities ──────────────────────────────────────────────────────

static inline std::string human_bytes(double bytes) {
    static const char* u[]={"B","KB","MB","GB","TB","PB"};
    int k=0;
    while(bytes>=1024.0 && k<5){ bytes/=1024.0; ++k; }
    std::ostringstream o; o.setf(std::ios::fixed);
    o<<std::setprecision(bytes<10?2:1)<<bytes<<" "<<u[k];
    return o.str();
}

static inline long double ld_from_u256(const uint64_t v[4]) {
    return std::ldexp((long double)v[3],192)
         + std::ldexp((long double)v[2],128)
         + std::ldexp((long double)v[1],64)
         + (long double)v[0];
}

static inline std::string formatCompressedPubHex(const uint64_t Rx[4], const uint64_t Ry[4]) {
    uint8_t out[33];
    out[0] = (Ry[0] & 1ULL) ? 0x03 : 0x02;
    int off=1;
    for (int limb=3; limb>=0; --limb) {
        uint64_t v = Rx[limb];
        out[off+0]=(uint8_t)(v>>56); out[off+1]=(uint8_t)(v>>48);
        out[off+2]=(uint8_t)(v>>40); out[off+3]=(uint8_t)(v>>32);
        out[off+4]=(uint8_t)(v>>24); out[off+5]=(uint8_t)(v>>16);
        out[off+6]=(uint8_t)(v>> 8); out[off+7]=(uint8_t)(v>> 0);
        off+=8;
    }
    static const char* hexd="0123456789ABCDEF";
    std::string s; s.resize(66);
    for (int i=0;i<33;++i){ s[2*i]=hexd[(out[i]>>4)&0xF]; s[2*i+1]=hexd[out[i]&0xF]; }
    return s;
}

#endif // AMD_UTILS_H
