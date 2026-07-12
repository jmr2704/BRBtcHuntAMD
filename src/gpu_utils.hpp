// gpu_utils.hpp — GPU device utility functions (endian, compare, add)
// Included directly by HashPipeline.cpp (single-TU style).
#pragma once

__device__ __forceinline__ uint64_t loadU64BE(const uint8_t* p) {
    return ((uint64_t)p[0] << 56) |
           ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) |
           ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) |
           ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] <<  8) |
           ((uint64_t)p[7] <<  0);
}

__device__ __forceinline__ void storeU64BE(uint8_t* p, uint64_t x) {
    p[0] = (uint8_t)(x >> 56);
    p[1] = (uint8_t)(x >> 48);
    p[2] = (uint8_t)(x >> 40);
    p[3] = (uint8_t)(x >> 32);
    p[4] = (uint8_t)(x >> 24);
    p[5] = (uint8_t)(x >> 16);
    p[6] = (uint8_t)(x >>  8);
    p[7] = (uint8_t)(x >>  0);
}

__device__ __forceinline__ void addBigEndian256(uint8_t* key33, uint64_t offset)
{
    uint8_t* coord = key33 + 1;
    uint64_t x0 = loadU64BE(coord);        
    uint64_t x1 = loadU64BE(coord + 8);
    uint64_t x2 = loadU64BE(coord + 16);
    uint64_t x3 = loadU64BE(coord + 24);     

    uint64_t new_x3 = x3 + offset;

    if (new_x3 >= x3) {
        x3 = new_x3;
    }
    else {
        x3 = new_x3;
        uint64_t new_x2 = x2 + 1;
        if (new_x2 >= x2) {
            x2 = new_x2;
        }
        else {
            x2 = new_x2;
            uint64_t new_x1 = x1 + 1;
            if (new_x1 >= x1) {
                x1 = new_x1;
            }
            else {
                x1 = new_x1;
                x0 = x0 + 1;
            }
        }
    }

    storeU64BE(coord,     x0);
    storeU64BE(coord + 8, x1);
    storeU64BE(coord + 16, x2);
    storeU64BE(coord + 24, x3);
}

__device__ __forceinline__ bool compare20(const uint8_t* h, const uint8_t* ref) {
    ulonglong2 a, b;
    uint32_t c, d;
    
    memcpy(&a, h, sizeof(ulonglong2));
    memcpy(&b, ref, sizeof(ulonglong2));
    
    memcpy(&c, h + 16, sizeof(uint32_t));
    memcpy(&d, ref + 16, sizeof(uint32_t));
    
    return (a.x == b.x) && (a.y == b.y) && (c == d);
}

__device__ __forceinline__ uint32_t bswap32(uint32_t x){
    return __builtin_bswap32(x);
}

__device__ __forceinline__ uint32_t pack_be4(uint8_t a,uint8_t b,uint8_t c,uint8_t d){
    return ((uint32_t)a<<24)|((uint32_t)b<<16)|((uint32_t)c<<8)|((uint32_t)d);
}

__device__ __forceinline__ uint32_t hash_load_u32_le(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
