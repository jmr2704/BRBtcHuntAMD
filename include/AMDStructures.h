#ifndef AMD_STRUCTURES_H
#define AMD_STRUCTURES_H

#include <cstdint>
#include "GpuPlatform.h"

#define WARP_SIZE 32
#define FOUND_NONE  0
#define FOUND_LOCK  1
#define FOUND_READY 2

struct FoundResult {
    int      threadId;
    int      iter;
    uint64_t scalar[4];
    uint64_t Rx[4];
    uint64_t Ry[4];
};

// ── Backend device-side target constants ──────────────────────────────
__device__ uint8_t  c_target_hash160[20];
__device__ uint32_t c_target_prefix;

// ── Kernel forward declarations ────────────────────────────────────────
__global__ void scalarMulKernelBase(const uint64_t* scalars_in,
                                     uint64_t* outX, uint64_t* outY, int N);

// ── Backend error checking macro ───────────────────────────────────────
#define HIP_CHECK(ans) BTC_GPU_CHECK(ans)

__device__ __constant__ uint64_t Gx_d[4];
__device__ __constant__ uint64_t Gy_d[4];

#endif // AMD_STRUCTURES_H
