// GPUWorker.cpp — Combined GPU kernel TU (kernels + HashPipeline).
// Kept as a single TU because AMDMath.h device functions are not inline.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <cmath>
#include <csignal>
#include <atomic>
#include <mutex>
#include <vector>
#include <array>
#include <random>
#include <fstream>

#include "AMDUtils.h"
#include "GpuPlatform.h"
#include "GPUWorker.h"
#include "AMDMath.h"
#include "AMDHash.h"
#include "Lang.h"

// ── Signal flag (defined in main.cpp, referenced from run_on_gpu) ──────
extern volatile sig_atomic_t g_sigint;

// ── Device-side target constants (definitions) ─────────────────────────
__device__ uint8_t  c_target_hash160[20];
__device__ uint32_t c_target_prefix;

// ── GPU constants ──────────────────────────────────────────────────────
#ifndef MAX_BATCH_SIZE
#define MAX_BATCH_SIZE 2048
#endif
#ifndef WARP_SIZE
#define WARP_SIZE 32
#endif

// ── HashPipeline: included as a single compilation unit ────────────────
#include "HashPipeline.cpp"

__device__ __forceinline__ int load_found_flag_relaxed(const int* p) {
    return *((const volatile int*)p);
}
__device__ __forceinline__ bool warp_found_ready(const int* __restrict__ d_found_flag,
                                                 uint64_t full_mask,
                                                 unsigned lane)
{
    int f = 0;
    if (lane == 0) f = load_found_flag_relaxed(d_found_flag);
    f = BTC_SHFL_SYNC(full_mask, f, 0);
    return f == FOUND_READY;
}

#ifndef MAX_BATCH_SIZE
#define MAX_BATCH_SIZE 2048
#endif
#ifndef WARP_SIZE
#define WARP_SIZE 32
#endif

__constant__ uint64_t c_Gx[(MAX_BATCH_SIZE/2) * 4];
__constant__ uint64_t c_Gy[(MAX_BATCH_SIZE/2) * 4];
__constant__ uint64_t c_Jx[4];
__constant__ uint64_t c_Jy[4];

// ── Vanity constants — device-side ──────────────────────────────────
__constant__ uint32_t c_vanity_compare_bytes;
__constant__ uint32_t c_vanity_match_nibble;
__device__ VanityResult* d_vanity_results;
__device__ uint32_t* d_vanity_count;

// ── Vanity check helper ─────────────────────────────────────────────────────
// Compares the first N bytes (+ optional high nibble) of h160 against c_target_hash160.
// On match, writes privkey, pubkey, hash160, prefix to the global result buffer.
__device__ __forceinline__ void vanity_check_and_save(
    const uint32_t h160[5],
    uint8_t prefix,
    const uint64_t privkey[4],
    const uint64_t pubkey_x[4],
    uint32_t* vanity_count,
    VanityResult* vanity_buf,
    uint32_t max_results)
{
    // Compare bytes
    const uint8_t* h = (const uint8_t*)h160;
    for (uint32_t b = 0; b < c_vanity_compare_bytes; ++b) {
        if (h[b] != c_target_hash160[b]) return;
    }
    // Compare optional extra high nibble
    if (c_vanity_match_nibble) {
        uint8_t hn = (h[c_vanity_compare_bytes] >> 4) & 0x0f;
        uint8_t tn = (c_target_hash160[c_vanity_compare_bytes] >> 4) & 0x0f;
        if (hn != tn) return;
    }

    // Match — write to buffer
    uint32_t slot = atomicAdd(vanity_count, 1);
    if (slot < max_results && vanity_buf) {
        VanityResult& r = vanity_buf[slot];
        for (int k = 0; k < 4; ++k) r.privkey[k] = privkey[k];
        for (int k = 0; k < 4; ++k) r.pubkey_x[k] = pubkey_x[k];
        for (int k = 0; k < 5; ++k) r.hash160[k] = h160[k];
        r.prefix = prefix;
    }
}

__launch_bounds__(256, 2)
__global__ void kernel_point_add_and_check_oneinv(
    const uint64_t* __restrict__ Px,
    const uint64_t* __restrict__ Py,
    uint64_t* __restrict__ Rx,
    uint64_t* __restrict__ Ry,
    uint64_t* __restrict__ start_scalars,
    uint64_t* __restrict__ counts256,
    uint64_t threadsTotal,
    uint32_t batch_size,
    uint32_t max_batches_per_launch,
    int* __restrict__ d_found_flag,
    FoundResult* __restrict__ d_found_result,
    unsigned long long* __restrict__ hashes_accum,
    unsigned int* __restrict__ d_any_left,
    uint32_t* __restrict__ d_vanity_count,
    VanityResult* __restrict__ d_vanity_buf,
    uint32_t vanity_max_results
)
{
    const int B = (int)batch_size;
    if (B <= 0 || (B & 1) || B > MAX_BATCH_SIZE) return;
    const int half = B >> 1;

    const uint64_t gid = (uint64_t)blockIdx.x * blockDim.x + threadIdx.x;
    if (gid >= threadsTotal) return;

    const unsigned lane      = (unsigned)(threadIdx.x & (WARP_SIZE - 1));
    const uint64_t full_mask = 0xFFFFFFFFull;
    if (warp_found_ready(d_found_flag, full_mask, lane)) return;

    const uint32_t target_prefix = c_target_prefix;
    const bool _vanity_active = (c_vanity_compare_bytes > 0 || c_vanity_match_nibble > 0);

    unsigned int local_hashes = 0;
    #define FLUSH_THRESHOLD 65536u
    #define WARP_FLUSH_HASHES() do { \
        unsigned long long v = warp_reduce_add_ull((unsigned long long)local_hashes); \
        if (lane == 0 && v) atomicAdd(hashes_accum, v); \
        local_hashes = 0; \
    } while (0)
    #define MAYBE_WARP_FLUSH() do { if ((local_hashes & (FLUSH_THRESHOLD - 1u)) == 0u) WARP_FLUSH_HASHES(); } while (0)

    uint64_t x1[4], y1[4], S[4];
#pragma unroll
    for (int i = 0; i < 4; ++i) {
        const uint64_t idx = gid * 4 + i;
        x1[i] = Px[idx];
        y1[i] = Py[idx];
        S[i]  = start_scalars[idx];
    }
    uint64_t rem[4];
#pragma unroll
    for (int i = 0; i < 4; ++i) rem[i] = counts256[gid*4 + i];

    if ((rem[0]|rem[1]|rem[2]|rem[3]) == 0ull) {
#pragma unroll
        for (int i = 0; i < 4; ++i) { Rx[gid*4+i] = x1[i]; Ry[gid*4+i] = y1[i]; }
        WARP_FLUSH_HASHES(); return;
    }

    uint32_t batches_done = 0;

    while (batches_done < max_batches_per_launch && ge256_u64(rem, (uint64_t)B)) {
        if (warp_found_ready(d_found_flag, full_mask, lane)) { WARP_FLUSH_HASHES(); return; }

        {
            uint8_t prefix = (uint8_t)(y1[0] & 1ULL) ? 0x03 : 0x02;
            uint32_t _h160_i[5];
            bool matched = getHash160_33_from_limbs_matches(prefix, x1, c_target_hash160, target_prefix,
                _vanity_active ? _h160_i : nullptr);
            ++local_hashes; MAYBE_WARP_FLUSH();
            if (_vanity_active) {
                vanity_check_and_save(_h160_i, prefix, S, x1, d_vanity_count, d_vanity_buf, vanity_max_results);
            }

            if (BTC_ANY_SYNC(full_mask, matched)) {
                if (matched) {
                    if (atomicCAS(d_found_flag, FOUND_NONE, FOUND_LOCK) == FOUND_NONE) {
                        d_found_result->threadId = (int)gid;
                        d_found_result->iter     = 0;
#pragma unroll
                        for (int k=0;k<4;++k) d_found_result->scalar[k]=S[k];
#pragma unroll
                        for (int k=0;k<4;++k) d_found_result->Rx[k]=x1[k];
#pragma unroll
                        for (int k=0;k<4;++k) d_found_result->Ry[k]=y1[k];
                        __threadfence_system();
                        atomicExch(d_found_flag, FOUND_READY);
                    }
                }
                BTC_SYNCWARP(full_mask); WARP_FLUSH_HASHES(); return;
            }
        }

        uint64_t subp[MAX_BATCH_SIZE/2][4];
        uint64_t acc[4], tmp[4];

#pragma unroll
        for (int j=0;j<4;++j) acc[j] = c_Jx[j];
        ModSub256(acc, acc, x1);
#pragma unroll
        for (int j=0;j<4;++j) subp[half-1][j] = acc[j];

        for (int i = half - 2; i >= 0; --i) {
#pragma unroll
            for (int j=0;j<4;++j) tmp[j] = c_Gx[(size_t)(i+1)*4 + j];
            ModSub256(tmp, tmp, x1);
            _ModMult(acc, acc, tmp);
#pragma unroll
            for (int j=0;j<4;++j) subp[i][j] = acc[j];
        }

        uint64_t d0[4], inverse[5];
#pragma unroll
        for (int j=0;j<4;++j) d0[j] = c_Gx[0*4 + j];
        ModSub256(d0, d0, x1);
#pragma unroll
        for (int j=0;j<4;++j) inverse[j] = d0[j];
        _ModMult(inverse, subp[0]);
        inverse[4] = 0ull;
        _ModInv(inverse);

        uint64_t sy_neg[4], sx_neg[4];
        ModNeg256(sy_neg, y1);
        ModNeg256(sx_neg, x1);

        for (int i = 0; i < half - 1; ++i) {
            if (warp_found_ready(d_found_flag, full_mask, lane)) { WARP_FLUSH_HASHES(); return; }

            uint64_t dx_inv_i[4];
            _ModMult(dx_inv_i, subp[i], inverse);

            {
                uint64_t px3[4], s[4], lam[4];
                uint64_t px_i[4], py_i[4];
#pragma unroll
                for (int j=0;j<4;++j) { px_i[j]=c_Gx[(size_t)i*4+j]; py_i[j]=c_Gy[(size_t)i*4+j]; }

                ModSub256(s, py_i, y1);
                _ModMult(lam, s, dx_inv_i);

                _ModSqr(px3, lam);
                ModSub256(px3, px3, x1);
                ModSub256(px3, px3, px_i);

                // Deferred parity: try 0x02, then 0x03 if no local match
                uint32_t _h160_a[5];
                bool m02 = getHash160_33_from_limbs_matches(0x02, px3, c_target_hash160, target_prefix,
                    _vanity_active ? _h160_a : nullptr);
                ++local_hashes; MAYBE_WARP_FLUSH();
                if (_vanity_active) {
                    vanity_check_and_save(_h160_a, 0x02, S, px3, d_vanity_count, d_vanity_buf, vanity_max_results);
                }
                bool matched;
                if (!m02) {
                    uint32_t _h160_b[5];
                    bool m03 = getHash160_33_from_limbs_matches(0x03, px3, c_target_hash160, target_prefix,
                        _vanity_active ? _h160_b : nullptr);
                    ++local_hashes; MAYBE_WARP_FLUSH();
                    if (_vanity_active) {
                        vanity_check_and_save(_h160_b, 0x03, S, px3, d_vanity_count, d_vanity_buf, vanity_max_results);
                    }
                    matched = m03;
                } else {
                    matched = true;
                }

                if (BTC_ANY_SYNC(full_mask, matched)) {
                    if (matched) {
                        if (atomicCAS(d_found_flag, FOUND_NONE, FOUND_LOCK) == FOUND_NONE) {
                            uint64_t fs[4]; for (int k=0;k<4;++k) fs[k]=S[k];
                            uint64_t addv=(uint64_t)(i+1);
                            for (int k=0;k<4 && addv;++k){ uint64_t old=fs[k]; fs[k]=old+addv; addv=(fs[k]<old)?1ull:0ull; }
#pragma unroll
                            for (int k=0;k<4;++k) d_found_result->scalar[k]=fs[k];
#pragma unroll
                            for (int k=0;k<4;++k) d_found_result->Rx[k]=px3[k];

                            uint64_t y3[4]; uint64_t t[4]; ModSub256(t, x1, px3); _ModMult(y3, t, lam); ModSub256(y3, y3, y1);
#pragma unroll
                            for (int k=0;k<4;++k) d_found_result->Ry[k]=y3[k];
                            d_found_result->threadId = (int)gid;
                            d_found_result->iter     = 0;
                            __threadfence_system();
                            atomicExch(d_found_flag, FOUND_READY);
                        }
                    }
                    BTC_SYNCWARP(full_mask); WARP_FLUSH_HASHES(); return;
                }
            }

            {
                uint64_t px3[4], s[4], lam[4];
                uint64_t px_i[4], py_i[4];
#pragma unroll
                for (int j=0;j<4;++j) { px_i[j]=c_Gx[(size_t)i*4+j]; py_i[j]=c_Gy[(size_t)i*4+j]; }
                ModNeg256(py_i, py_i);

                ModSub256(s, py_i, y1);
                _ModMult(lam, s, dx_inv_i);

                _ModSqr(px3, lam);
                ModSub256(px3, px3, x1);
                ModSub256(px3, px3, px_i);

                // Deferred parity: try 0x02, then 0x03
                uint32_t _h160_a[5];
                bool m02 = getHash160_33_from_limbs_matches(0x02, px3, c_target_hash160, target_prefix,
                    _vanity_active ? _h160_a : nullptr);
                ++local_hashes; MAYBE_WARP_FLUSH();
                if (_vanity_active) {
                    vanity_check_and_save(_h160_a, 0x02, S, px3, d_vanity_count, d_vanity_buf, vanity_max_results);
                }
                bool matched;
                if (!m02) {
                    uint32_t _h160_b[5];
                    bool m03 = getHash160_33_from_limbs_matches(0x03, px3, c_target_hash160, target_prefix,
                        _vanity_active ? _h160_b : nullptr);
                    ++local_hashes; MAYBE_WARP_FLUSH();
                    if (_vanity_active) {
                        vanity_check_and_save(_h160_b, 0x03, S, px3, d_vanity_count, d_vanity_buf, vanity_max_results);
                    }
                    matched = m03;
                } else {
                    matched = true;
                }

                if (BTC_ANY_SYNC(full_mask, matched)) {
                    if (matched) {
                        if (atomicCAS(d_found_flag, FOUND_NONE, FOUND_LOCK) == FOUND_NONE) {
                            uint64_t fs[4]; for (int k=0;k<4;++k) fs[k]=S[k];
                            uint64_t sub=(uint64_t)(i+1);
                            for (int k=0;k<4 && sub;++k){ uint64_t old=fs[k]; fs[k]=old-sub; sub=(old<sub)?1ull:0ull; }
#pragma unroll
                            for (int k=0;k<4;++k) d_found_result->scalar[k]=fs[k];
#pragma unroll
                            for (int k=0;k<4;++k) d_found_result->Rx[k]=px3[k];
                            uint64_t y3[4]; uint64_t t[4]; ModSub256(t, x1, px3); _ModMult(y3, t, lam); ModSub256(y3, y3, y1);
#pragma unroll
                            for (int k=0;k<4;++k) d_found_result->Ry[k]=y3[k];
                            d_found_result->threadId = (int)gid;
                            d_found_result->iter     = 0;
                            __threadfence_system();
                            atomicExch(d_found_flag, FOUND_READY);
                        }
                    }
                    BTC_SYNCWARP(full_mask); WARP_FLUSH_HASHES(); return;
                }
            }

            uint64_t gxmi[4];
#pragma unroll
            for (int j=0;j<4;++j) gxmi[j] = c_Gx[(size_t)i*4 + j];
            ModSub256(gxmi, gxmi, x1);
            _ModMult(inverse, inverse, gxmi);
        }

        {
            const int i = half - 1;
            uint64_t dx_inv_i[4];
            _ModMult(dx_inv_i, subp[i], inverse);

            uint64_t px3[4], s[4], lam[4];
            uint64_t px_i[4], py_i[4];
#pragma unroll
            for (int j=0;j<4;++j) { px_i[j]=c_Gx[(size_t)i*4+j]; py_i[j]=c_Gy[(size_t)i*4+j]; }
            ModNeg256(py_i, py_i);

            ModSub256(s, py_i, y1);
            _ModMult(lam, s, dx_inv_i);

            _ModSqr(px3, lam);
            ModSub256(px3, px3, x1);
            ModSub256(px3, px3, px_i);

            // Deferred parity: try 0x02, then 0x03
            uint32_t _h160_a[5];
            bool m02 = getHash160_33_from_limbs_matches(0x02, px3, c_target_hash160, target_prefix,
                _vanity_active ? _h160_a : nullptr);
            ++local_hashes; MAYBE_WARP_FLUSH();
            if (_vanity_active) {
                vanity_check_and_save(_h160_a, 0x02, S, px3, d_vanity_count, d_vanity_buf, vanity_max_results);
            }
            bool matched;
            if (!m02) {
                uint32_t _h160_b[5];
                bool m03 = getHash160_33_from_limbs_matches(0x03, px3, c_target_hash160, target_prefix,
                    _vanity_active ? _h160_b : nullptr);
                ++local_hashes; MAYBE_WARP_FLUSH();
                if (_vanity_active) {
                    vanity_check_and_save(_h160_b, 0x03, S, px3, d_vanity_count, d_vanity_buf, vanity_max_results);
                }
                matched = m03;
            } else {
                matched = true;
            }

            if (BTC_ANY_SYNC(full_mask, matched)) {
                if (matched) {
                    if (atomicCAS(d_found_flag, FOUND_NONE, FOUND_LOCK) == FOUND_NONE) {
                        uint64_t fs[4]; for (int k=0;k<4;++k) fs[k]=S[k];
                        uint64_t sub=(uint64_t)half;
                        for (int k=0;k<4 && sub;++k){ uint64_t old=fs[k]; fs[k]=old-sub; sub=(old<sub)?1ull:0ull; }
#pragma unroll
                        for (int k=0;k<4;++k) d_found_result->scalar[k]=fs[k];
#pragma unroll
                        for (int k=0;k<4;++k) d_found_result->Rx[k]=px3[k];
                        uint64_t y3[4]; uint64_t t[4]; ModSub256(t, x1, px3); _ModMult(y3, t, lam); ModSub256(y3, y3, y1);
#pragma unroll
                        for (int k=0;k<4;++k) d_found_result->Ry[k]=y3[k];
                        d_found_result->threadId = (int)gid;
                        d_found_result->iter     = 0;
                        __threadfence_system();
                        atomicExch(d_found_flag, FOUND_READY);
                    }
                }
                BTC_SYNCWARP(full_mask); WARP_FLUSH_HASHES(); return;
            }

            uint64_t last_dx[4];
#pragma unroll
            for (int j=0;j<4;++j) last_dx[j] = c_Gx[(size_t)i*4 + j];
            ModSub256(last_dx, last_dx, x1);
            _ModMult(inverse, inverse, last_dx);
        }

        {
            uint64_t lam[4], s[4], x3[4], y3[4];

            uint64_t Jy_minus_y1[4];
#pragma unroll
            for (int j=0;j<4;++j) Jy_minus_y1[j] = c_Jy[j];
            ModSub256(Jy_minus_y1, Jy_minus_y1, y1);

            _ModMult(lam, Jy_minus_y1, inverse);
            _ModSqr(x3, lam);
            ModSub256(x3, x3, x1);
            uint64_t Jx_local[4]; for (int j=0;j<4;++j) Jx_local[j]=c_Jx[j];
            ModSub256(x3, x3, Jx_local);

            ModSub256(s, x1, x3);
            _ModMult(y3, s, lam);
            ModSub256(y3, y3, y1);

#pragma unroll
            for (int j=0;j<4;++j) { x1[j] = x3[j]; y1[j] = y3[j]; }
        }

        {
            uint64_t addv=(uint64_t)B;
            for (int k=0;k<4 && addv;++k){ uint64_t old=S[k]; S[k]=old+addv; addv=(S[k]<old)?1ull:0ull; }
            sub256_u64_inplace(rem, (uint64_t)B);
        }
        ++batches_done;
    }

#pragma unroll
    for (int i = 0; i < 4; ++i) {
        Rx[gid*4+i] = x1[i];
        Ry[gid*4+i] = y1[i];
        counts256[gid*4+i] = rem[i];
        start_scalars[gid*4+i] = S[i];
    }
    if ((rem[0] | rem[1] | rem[2] | rem[3]) != 0ull) {
        atomicAdd(d_any_left, 1u);
    }

    WARP_FLUSH_HASHES();
    #undef MAYBE_WARP_FLUSH
    #undef WARP_FLUSH_HASHES
    #undef FLUSH_THRESHOLD
}


// ── Print mutex (shared across GPU worker threads) ────────────────
std::mutex g_print_mutex;

void run_on_gpu(
    int            gpu_id,
    const uint64_t range_start[4],
    const uint64_t range_end[4],
    const uint8_t  target_hash160[20],
    uint32_t       runtime_points_batch_size,
    uint32_t       runtime_batches_per_sm,
    uint32_t       slices_per_launch,
    bool           random_mode,
    GpuShared&     shared
) {
    hipSetDevice(gpu_id);
    
    auto ck = [&](hipError_t e, const char* msg) {
        if (e != hipSuccess) {
            fprintf(stderr, "\n[GPU %d] %s: %s\n", gpu_id, msg, hipGetErrorString(e));
            BTC_HOST_FAIL();
        }
    };
    
    ck(hipFree(0), "hipFree(0)");

    hipDeviceProp_t prop{};
    ck(hipGetDeviceProperties(&prop, gpu_id), "hipGetDeviceProperties");

    int threadsPerBlock = 256;
    if (threadsPerBlock > (int)prop.maxThreadsPerBlock) threadsPerBlock = prop.maxThreadsPerBlock;
    if (threadsPerBlock < 32) threadsPerBlock = 32;

    uint64_t gpu_range_len[4];
    sub256(range_end, range_start, gpu_range_len);
    add256_u64(gpu_range_len, 1ull, gpu_range_len);


    const uint64_t bytesPerThread = 2ull * 4ull * sizeof(uint64_t);
    size_t totalGlobalMem = prop.totalGlobalMem;
    const uint64_t reserveBytes = 64ull * 1024 * 1024;
    uint64_t usableMem = (totalGlobalMem > reserveBytes) ? (totalGlobalMem - reserveBytes) : (totalGlobalMem / 2);
    uint64_t maxThreadsByMem = usableMem / bytesPerThread;

    uint64_t q_div_batch[4]; uint64_t r_batch = 0ull;
    divmod_256_by_u64(gpu_range_len, (uint64_t)runtime_points_batch_size, q_div_batch, r_batch);
    if (r_batch != 0ull) {
        uint64_t adjust = (uint64_t)runtime_points_batch_size - r_batch;
        add256_u64(gpu_range_len, adjust, gpu_range_len);
        divmod_256_by_u64(gpu_range_len, (uint64_t)runtime_points_batch_size, q_div_batch, r_batch);
    }
    if ((q_div_batch[3] | q_div_batch[2] | q_div_batch[1]) != 0ull) {
        std::lock_guard<std::mutex> lk(g_print_mutex);
        fprintf(stderr, "[GPU %d] %s\n", gpu_id, ERR_RANGE_LARGE());
        std::exit(EXIT_FAILURE);
    }
    uint64_t total_batches_u64 = q_div_batch[0];

    uint64_t userUpper = (uint64_t)prop.multiProcessorCount * (uint64_t)runtime_batches_per_sm * (uint64_t)threadsPerBlock;
    if (userUpper == 0ull) userUpper = UINT64_MAX;

    uint64_t desired_upper = maxThreadsByMem;
    if (userUpper < desired_upper) desired_upper = userUpper;
    uint64_t threadsTotal = (desired_upper / (uint64_t)threadsPerBlock) * (uint64_t)threadsPerBlock;
    if (threadsTotal < (uint64_t)threadsPerBlock) threadsTotal = (uint64_t)threadsPerBlock;
    if (total_batches_u64 < threadsTotal) {
        threadsTotal = (total_batches_u64 / (uint64_t)threadsPerBlock) * (uint64_t)threadsPerBlock;
        if (threadsTotal < (uint64_t)threadsPerBlock) threadsTotal = (uint64_t)threadsPerBlock;
    }
    if ((total_batches_u64 % threadsTotal) != 0ull) {
        uint64_t rem = total_batches_u64 % threadsTotal;
        total_batches_u64 += threadsTotal - rem;
        add256_u64(gpu_range_len, (threadsTotal - rem) * (uint64_t)runtime_points_batch_size, gpu_range_len);
    }

    shared.total_keys_adjusted = ld_from_u256(gpu_range_len);

    int blocks = (int)(threadsTotal / (uint64_t)threadsPerBlock);

    uint64_t per_thread_cnt[4]; uint64_t r_u64 = 0ull;
    if (random_mode) {
        // Each kernel launch covers exactly one fixed-size chunk per thread
        per_thread_cnt[0] = (uint64_t)runtime_points_batch_size * slices_per_launch;
        per_thread_cnt[1] = per_thread_cnt[2] = per_thread_cnt[3] = 0ull;
    } else {
        divmod_256_by_u64(gpu_range_len, threadsTotal, per_thread_cnt, r_u64);
    }

    const uint32_t B    = runtime_points_batch_size;
    const uint32_t half = B >> 1;

    // Target constants (per-device constant memory)
    {
        uint32_t prefix_le = (uint32_t)target_hash160[0]
                           | ((uint32_t)target_hash160[1] << 8)
                           | ((uint32_t)target_hash160[2] << 16)
                           | ((uint32_t)target_hash160[3] << 24);

        ck(hipMemcpyToSymbol(HIP_SYMBOL(c_target_prefix),  &prefix_le,    sizeof(prefix_le)), "ToSymbol c_target_prefix");
        ck(hipMemcpyToSymbol(HIP_SYMBOL(c_target_hash160), target_hash160, 20),               "ToSymbol c_target_hash160");
    }

    // Vanity constants & buffers
    VanityResult *d_vanity_buf = nullptr;
    uint32_t *d_vanity_count_gpu = nullptr;
    if (shared.vanity_nibbles > 0) {
        uint32_t compare_bytes = shared.vanity_nibbles / 2;
        uint32_t match_nibble  = shared.vanity_nibbles % 2;
        ck(hipMemcpyToSymbol(HIP_SYMBOL(c_vanity_compare_bytes), &compare_bytes, 4), "vanity_cmp_bytes");
        ck(hipMemcpyToSymbol(HIP_SYMBOL(c_vanity_match_nibble),  &match_nibble,  4), "vanity_match_nib");
        ck(hipMalloc(&d_vanity_count_gpu, sizeof(uint32_t)), "vanity_count");
        ck(hipMalloc(&d_vanity_buf, (size_t)shared.vanity_max_results * sizeof(VanityResult)), "vanity_buf");
        uint32_t vz = 0;
        ck(hipMemcpy(d_vanity_count_gpu, &vz, sizeof(uint32_t), hipMemcpyHostToDevice), "init vanity_count");
    }

    // Host buffers (plain malloc — no hipHostAlloc needed for one-time upload)
    std::vector<uint64_t> h_counts256(threadsTotal * 4);
    std::vector<uint64_t> h_start_scalars(threadsTotal * 4);

    for (uint64_t i = 0; i < threadsTotal; ++i) {
        h_counts256[i*4+0] = per_thread_cnt[0];
        h_counts256[i*4+1] = per_thread_cnt[1];
        h_counts256[i*4+2] = per_thread_cnt[2];
        h_counts256[i*4+3] = per_thread_cnt[3];
    }

    {
        uint64_t cur[4] = { range_start[0], range_start[1], range_start[2], range_start[3] };
        for (uint64_t i = 0; i < threadsTotal; ++i) {
            uint64_t Sc[4]; add256_u64(cur, (uint64_t)half, Sc);
            h_start_scalars[i*4+0] = Sc[0];
            h_start_scalars[i*4+1] = Sc[1];
            h_start_scalars[i*4+2] = Sc[2];
            h_start_scalars[i*4+3] = Sc[3];
            uint64_t next[4]; add256(cur, per_thread_cnt, next);
            cur[0]=next[0]; cur[1]=next[1]; cur[2]=next[2]; cur[3]=next[3];
        }
        shared.cur_scalar_lo.store(h_start_scalars[0], std::memory_order_relaxed);
        shared.cur_scalar_hi.store(h_start_scalars[1], std::memory_order_relaxed);
    }

    // Device buffers
    uint64_t *d_start_scalars=nullptr, *d_Px=nullptr, *d_Py=nullptr,
             *d_Rx=nullptr,           *d_Ry=nullptr, *d_counts256=nullptr;
    int            *d_found_flag   = nullptr;
    FoundResult    *d_found_result = nullptr;
    unsigned long long *d_hashes_accum = nullptr;
    unsigned int       *d_any_left     = nullptr;

    ck(hipMalloc(&d_start_scalars, threadsTotal * 4 * sizeof(uint64_t)), "malloc start_scalars");
    if (threadsTotal == 0) {
        shared.init_done.fetch_add(1, std::memory_order_release);
        return;
    }
    ck(hipMalloc(&d_Px,            threadsTotal * 4 * sizeof(uint64_t)), "malloc Px");
    ck(hipMalloc(&d_Py,            threadsTotal * 4 * sizeof(uint64_t)), "malloc Py");
    ck(hipMalloc(&d_Rx,            threadsTotal * 4 * sizeof(uint64_t)), "malloc Rx");
    ck(hipMalloc(&d_Ry,            threadsTotal * 4 * sizeof(uint64_t)), "malloc Ry");
    ck(hipMalloc(&d_counts256,     threadsTotal * 4 * sizeof(uint64_t)), "malloc C256");
    ck(hipMalloc(&d_found_flag,    sizeof(int)),                          "malloc FFlag");
    ck(hipMalloc(&d_found_result,  sizeof(FoundResult)),                  "malloc FRes");
    ck(hipMalloc(&d_hashes_accum,  sizeof(unsigned long long)),           "malloc HAcc");
    ck(hipMalloc(&d_any_left,      sizeof(unsigned int)),                 "malloc ALeft");
    
    ck(hipMemcpy(d_start_scalars, h_start_scalars.data(), threadsTotal * 4 * sizeof(uint64_t), hipMemcpyHostToDevice), "cpy start_scalars");
    ck(hipMemcpy(d_counts256,     h_counts256.data(),     threadsTotal * 4 * sizeof(uint64_t), hipMemcpyHostToDevice), "cpy counts256");
    { int z = FOUND_NONE; unsigned long long z64 = 0ull;
      ck(hipMemcpy(d_found_flag,   &z,   sizeof(int),                hipMemcpyHostToDevice), "init found_flag");
      ck(hipMemcpy(d_hashes_accum, &z64, sizeof(unsigned long long), hipMemcpyHostToDevice), "init hashes_accum"); }

    // Precompute initial EC points
    {
        int bs = (int)((threadsTotal + threadsPerBlock - 1) / threadsPerBlock);
        scalarMulKernelBase<<<bs, threadsPerBlock>>>(d_start_scalars, d_Px, d_Py, (int)threadsTotal);
        ck(hipDeviceSynchronize(), "scalarMulKernelBase sync");
        ck(hipGetLastError(),      "scalarMulKernelBase launch");
    }

    // Precompute G*1..G*half → constant memory c_Gx / c_Gy
    {
        std::vector<uint64_t> h_scalars_half(half * 4, 0);
        for (uint32_t k = 0; k < half; ++k) h_scalars_half[(size_t)k*4] = (uint64_t)(k + 1);

        uint64_t *d_sh=nullptr, *d_Gxh=nullptr, *d_Gyh=nullptr;
        ck(hipMalloc(&d_sh,  (size_t)half * 4 * sizeof(uint64_t)), "hipMalloc(d_sh)");
        ck(hipMalloc(&d_Gxh, (size_t)half * 4 * sizeof(uint64_t)), "hipMalloc(d_Gxh)");
        ck(hipMalloc(&d_Gyh, (size_t)half * 4 * sizeof(uint64_t)), "hipMalloc(d_Gyh)");
        ck(hipMemcpy(d_sh, h_scalars_half.data(), (size_t)half * 4 * sizeof(uint64_t), hipMemcpyHostToDevice), "cpy half scalars");

        int bs = (int)((half + threadsPerBlock - 1) / threadsPerBlock);
        scalarMulKernelBase<<<bs, threadsPerBlock>>>(d_sh, d_Gxh, d_Gyh, (int)half);
        ck(hipDeviceSynchronize(), "scalarMulKernelBase(half) sync");
        ck(hipGetLastError(),      "scalarMulKernelBase(half) launch");

        std::vector<uint64_t> h_Gxh(half * 4), h_Gyh(half * 4);
        ck(hipMemcpy(h_Gxh.data(), d_Gxh, (size_t)half * 4 * sizeof(uint64_t), hipMemcpyDeviceToHost), "D2H Gx_half");
        ck(hipMemcpy(h_Gyh.data(), d_Gyh, (size_t)half * 4 * sizeof(uint64_t), hipMemcpyDeviceToHost), "D2H Gy_half");
        ck(hipMemcpyToSymbol(HIP_SYMBOL(c_Gx), h_Gxh.data(), (size_t)half * 4 * sizeof(uint64_t)), "ToSymbol c_Gx");
        ck(hipMemcpyToSymbol(HIP_SYMBOL(c_Gy), h_Gyh.data(), (size_t)half * 4 * sizeof(uint64_t)), "ToSymbol c_Gy");

        hipFree(d_sh); hipFree(d_Gxh); hipFree(d_Gyh);
    }

    // Precompute jump point J = G*B → constant memory c_Jx / c_Jy
    {
        std::vector<uint64_t> h_scB(4, 0); h_scB[0] = (uint64_t)B;
        uint64_t *d_scB=nullptr, *d_Jx=nullptr, *d_Jy=nullptr;
        ck(hipMalloc(&d_scB, 4 * sizeof(uint64_t)), "hipMalloc(d_scB)");
        ck(hipMalloc(&d_Jx,  4 * sizeof(uint64_t)), "hipMalloc(d_Jx)");
        ck(hipMalloc(&d_Jy,  4 * sizeof(uint64_t)), "hipMalloc(d_Jy)");
        ck(hipMemcpy(d_scB, h_scB.data(), 4 * sizeof(uint64_t), hipMemcpyHostToDevice), "cpy scB");

        scalarMulKernelBase<<<1, 1>>>(d_scB, d_Jx, d_Jy, 1);
        ck(hipDeviceSynchronize(), "scalarMulKernelBase(B) sync");
        ck(hipGetLastError(),      "scalarMulKernelBase(B) launch");

        uint64_t hJx[4], hJy[4];
        ck(hipMemcpy(hJx, d_Jx, 4 * sizeof(uint64_t), hipMemcpyDeviceToHost), "D2H Jx");
        ck(hipMemcpy(hJy, d_Jy, 4 * sizeof(uint64_t), hipMemcpyDeviceToHost), "D2H Jy");
        ck(hipMemcpyToSymbol(HIP_SYMBOL(c_Jx), hJx, 4 * sizeof(uint64_t)), "ToSymbol c_Jx");
        ck(hipMemcpyToSymbol(HIP_SYMBOL(c_Jy), hJy, 4 * sizeof(uint64_t)), "ToSymbol c_Jy");

        hipFree(d_scB); hipFree(d_Jx); hipFree(d_Jy);
    }

    // Print GPU info block
    {
        size_t freeB=0, totalB=0; hipMemGetInfo(&freeB, &totalB);
        double util = totalB ? (double)(totalB - freeB) * 100.0 / (double)totalB : 0.0;

        std::lock_guard<std::mutex> lk(g_print_mutex);
        std::cout << "======== GPU " << gpu_id << " : " << prop.name
                  << " (compute " << prop.major << "." << prop.minor << ") ========\n";
        std::cout << std::left << std::setw(20) << LBL_SM()              << " : " << prop.multiProcessorCount << "\n";
        std::cout << std::left << std::setw(20) << LBL_THREADS_BLOCK()   << " : " << threadsPerBlock << "\n";
        std::cout << std::left << std::setw(20) << LBL_BLOCKS()          << " : " << blocks << "\n";
        std::cout << std::left << std::setw(20) << LBL_TOTAL_THREADS()   << " : " << threadsTotal << "\n";
        std::cout << std::left << std::setw(20) << LBL_BATCH_SIZE()      << " : " << B << "\n";
        std::cout << std::left << std::setw(20) << LBL_BATCHES_SM()      << " : " << runtime_batches_per_sm << "\n";
        std::cout << std::left << std::setw(20) << LBL_BATCHES_LAUNCH()  << " : " << slices_per_launch << " (per thread)\n";
        std::cout << std::left << std::setw(20) << LBL_MEM_UTIL()        << " : "
                  << std::fixed << std::setprecision(1) << util << "% ("
                  << human_bytes((double)(totalB - freeB)) << " / " << human_bytes((double)totalB) << ")\n";
        std::cout << "------------------------------------------------------- \n";
        std::cout.flush();
    }

    // Signal init complete
    shared.init_done.fetch_add(1, std::memory_order_release);

    hipStream_t streamKernel;
    ck(hipStreamCreateWithFlags(&streamKernel, hipStreamNonBlocking), "create stream");
    (void)hipFuncSetCacheConfig(reinterpret_cast<const void*>(kernel_point_add_and_check_oneinv), hipFuncCachePreferL1);

    unsigned long long last_hashes_gpu = 0ull;
    bool stop_all    = false;
    bool completed_all = false;
    uint32_t last_vanity_count = 0;

    // Random mode: range for chunk selection and RNG
    uint64_t full_range_len[4];
    sub256(range_end, range_start, full_range_len);
    add256_u64(full_range_len, 1ull, full_range_len);
    // chunk_span = how many keys each random chunk covers (threadsTotal * per_thread_cnt[0])
    // per_thread_cnt[0] = B * slices in random mode, fits comfortably in uint64_t
    uint64_t chunk_span = (uint64_t)threadsTotal * per_thread_cnt[0];

    std::mt19937_64 rng_state(
        (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count()
        ^ ((uint64_t)gpu_id * 0x9e3779b97f4a7c15ULL)
    );

    // Fills chunk_start with a random position in [range_start, range_end - chunk_span]
    auto pick_random_start = [&](uint64_t chunk_start[4]) {
        // Effective range for chunk selection: range_len - chunk_span
        // Uses 128-bit arithmetic; safe for any range up to 2^128
        uint64_t rl_lo = full_range_len[0];
        uint64_t rl_hi = full_range_len[1];
        // Subtract chunk_span from 128-bit rl
        if (rl_lo < chunk_span) {
            if (rl_hi > 0) { --rl_hi; }  // borrow
            // rl_lo wraps: rl_lo = rl_lo + (2^64 - chunk_span) = rl_lo - chunk_span (mod 2^64)
        }
        rl_lo -= chunk_span;
        if (rl_hi == 0 && rl_lo == 0) { rl_lo = 1; }  // guard against empty range

        // Generate 128-bit random r
        uint64_t r_lo = rng_state();
        uint64_t r_hi = rng_state();

        // Compute off = r % rl  (128-bit modulo)
        uint64_t off_lo, off_hi;
        if (rl_hi == 0) {
            // Divisor fits in 64 bits
            uint64_t rem = 0;
#if defined(_MSC_VER) && !defined(__clang__)
            off_lo = _udiv128(r_hi, r_lo, rl_lo, &rem);
            off_hi = 0;
#else
            __uint128_t rr = ((__uint128_t)r_hi << 64) | r_lo;
            off_lo = (uint64_t)(rr % rl_lo);
            off_hi = 0;
#endif
        } else {
            // Divisor is 128-bit: binary long division (shift-subtract)
            uint64_t rm_lo = 0, rm_hi = 0;
            off_lo = 0; off_hi = 0;
            for (int _i = 0; _i < 128; ++_i) {
                // shift remainder left, bring in top bit of r_hi
                uint64_t top = (r_hi >> 63);
                rm_lo = (rm_lo << 1) | (rm_hi >> 63);
                rm_hi = (rm_hi << 1) | top;
                // shift quotient left
                off_lo = (off_lo << 1) | (off_hi >> 63);
                off_hi = (off_hi << 1);
                // bring in next bit of r
                r_hi = (r_hi << 1) | (r_lo >> 63);
                r_lo = (r_lo << 1);
                // if remainder >= divisor, subtract
                if (rm_hi > rl_hi || (rm_hi == rl_hi && rm_lo >= rl_lo)) {
                    // subtract rl from rm (128-bit borrow)
                    uint64_t diff = rm_lo - rl_lo;
                    uint64_t brw = (diff > rm_lo) ? 1ULL : 0ULL;
                    rm_lo = diff;
                    rm_hi = rm_hi - rl_hi - brw;
                    // set quotient bit
                    off_lo |= 1;
                }
            }
        }

        uint64_t offset[4] = {off_lo, off_hi, 0, 0};
        add256(range_start, offset, chunk_start);
    };

    // Refills h_start_scalars / h_counts256 and reinits EC points for a new chunk
    auto reinit_chunk = [&](const uint64_t chunk_start[4]) {
        uint64_t cur[4] = {chunk_start[0], chunk_start[1], chunk_start[2], chunk_start[3]};
        for (uint64_t i = 0; i < threadsTotal; ++i) {
            uint64_t Sc[4]; add256_u64(cur, (uint64_t)half, Sc);
            h_start_scalars[i*4+0] = Sc[0]; h_start_scalars[i*4+1] = Sc[1];
            h_start_scalars[i*4+2] = Sc[2]; h_start_scalars[i*4+3] = Sc[3];
            uint64_t next[4]; add256(cur, per_thread_cnt, next);
            cur[0]=next[0]; cur[1]=next[1]; cur[2]=next[2]; cur[3]=next[3];
        }
        for (uint64_t i = 0; i < threadsTotal; ++i) {
            h_counts256[i*4+0] = per_thread_cnt[0];
            h_counts256[i*4+1] = per_thread_cnt[1];
            h_counts256[i*4+2] = per_thread_cnt[2];
            h_counts256[i*4+3] = per_thread_cnt[3];
        }
        shared.cur_scalar_lo.store(h_start_scalars[0], std::memory_order_relaxed);
        shared.cur_scalar_hi.store(h_start_scalars[1], std::memory_order_relaxed);
        hipMemcpy(d_start_scalars, h_start_scalars.data(),
                   threadsTotal * 4 * sizeof(uint64_t), hipMemcpyHostToDevice);
        hipMemcpy(d_counts256,     h_counts256.data(),
                   threadsTotal * 4 * sizeof(uint64_t), hipMemcpyHostToDevice);
        int bs = (int)((threadsTotal + threadsPerBlock - 1) / threadsPerBlock);
        scalarMulKernelBase<<<bs, threadsPerBlock>>>(d_start_scalars, d_Px, d_Py, (int)threadsTotal);
        hipDeviceSynchronize();
    };
    shared.setup_done.store(1, std::memory_order_release);

    while (!stop_all) {
        if (shared.any_found.load(std::memory_order_relaxed)) break;
        if (g_sigint) break;

        // Random mode: always pick a new random position before each kernel launch
        if (random_mode) {
            uint64_t chunk_start[4];
            pick_random_start(chunk_start);
            reinit_chunk(chunk_start);
            shared.chunks_tried.fetch_add(1, std::memory_order_relaxed);
            if (shared.any_found.load(std::memory_order_relaxed)) break;
            if (g_sigint) break;
        }

        unsigned int zeroU = 0u;
        ck(hipMemcpyAsync(d_any_left, &zeroU, sizeof(unsigned int), hipMemcpyHostToDevice, streamKernel), "zero d_any_left");

        kernel_point_add_and_check_oneinv<<<blocks, threadsPerBlock, 0, streamKernel>>>(
            d_Px, d_Py, d_Rx, d_Ry,
            d_start_scalars, d_counts256,
            threadsTotal, B, slices_per_launch,
            d_found_flag, d_found_result,
            d_hashes_accum, d_any_left,
            d_vanity_count_gpu,
            d_vanity_buf,
            shared.vanity_max_results
        );
        hipError_t launchErr = hipGetLastError();
        if (launchErr != hipSuccess) {
            std::lock_guard<std::mutex> lk(g_print_mutex);
            fprintf(stderr, "\n[GPU %d] Kernel launch error: %s\n", gpu_id, hipGetErrorString(launchErr));
            stop_all = true;
            break;
        }

        // Poll until kernel finishes
        while (!stop_all) {
            if (shared.any_found.load(std::memory_order_relaxed)) {
                // Another GPU found the key — signal our kernel to exit early
                int ready = FOUND_READY;
                hipMemcpy(d_found_flag, &ready, sizeof(int), hipMemcpyHostToDevice);
                stop_all = true;
                break;
            }
            if (g_sigint) { stop_all = true; break; }

            // Accumulate hash count from this GPU into shared counter
            unsigned long long h_hashes = 0ull;
            hipMemcpy(&h_hashes, d_hashes_accum, sizeof(unsigned long long), hipMemcpyDeviceToHost);
            if (h_hashes > last_hashes_gpu) {
                shared.total_hashes.fetch_add(h_hashes - last_hashes_gpu, std::memory_order_relaxed);
                last_hashes_gpu = h_hashes;
            }

            int host_found = 0;
            hipMemcpy(&host_found, d_found_flag, sizeof(int), hipMemcpyDeviceToHost);
            if (host_found == FOUND_READY) {
                FoundResult res{};
                hipMemcpy(&res, d_found_result, sizeof(FoundResult), hipMemcpyDeviceToHost);
                {
                    std::lock_guard<std::mutex> lk(shared.result_mtx);
                    if (!shared.has_result) {
                        shared.best_result = res;
                        shared.has_result  = true;
                    }
                }
                shared.any_found.store(1, std::memory_order_release);
                stop_all = true;
                break;
            }

            hipError_t qs = hipStreamQuery(streamKernel);
            if (qs == hipSuccess)           break;
            if (qs != hipErrorNotReady) { hipGetLastError(); stop_all = true; break; }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        hipStreamSynchronize(streamKernel);

        // Final hash flush after sync (memory now fully visible)
        {
            unsigned long long h_hashes = 0ull;
            hipMemcpy(&h_hashes, d_hashes_accum, sizeof(unsigned long long), hipMemcpyDeviceToHost);
            if (h_hashes > last_hashes_gpu) {
                shared.total_hashes.fetch_add(h_hashes - last_hashes_gpu, std::memory_order_relaxed);
                last_hashes_gpu = h_hashes;
            }

            // Re-check found flag after sync — catches the race where hipStreamQuery
            // returned success before the polling loop read FOUND_READY
            if (!stop_all && !shared.any_found.load(std::memory_order_relaxed)) {
                int host_found = 0;
                hipMemcpy(&host_found, d_found_flag, sizeof(int), hipMemcpyDeviceToHost);
                if (host_found == FOUND_READY) {
                    FoundResult res{};
                    hipMemcpy(&res, d_found_result, sizeof(FoundResult), hipMemcpyDeviceToHost);
                    {
                        std::lock_guard<std::mutex> lk(shared.result_mtx);
                        if (!shared.has_result) {
                            shared.best_result = res;
                            shared.has_result  = true;
                        }
                    }
                    shared.any_found.store(1, std::memory_order_release);
                    stop_all = true;
                }
            }
        }

        // Read back vanity results from this launch
        if (shared.vanity_nibbles > 0 && d_vanity_count_gpu) {
            uint32_t current_count = 0;
            hipMemcpy(&current_count, d_vanity_count_gpu, sizeof(uint32_t), hipMemcpyDeviceToHost);
            uint32_t capped = std::min(current_count, shared.vanity_max_results);
            if (capped > last_vanity_count) {
                uint32_t n_new = capped - last_vanity_count;
                std::vector<VanityResult> results(n_new);
                hipMemcpy(results.data(),
                          (VanityResult*)d_vanity_buf + last_vanity_count,
                          (size_t)n_new * sizeof(VanityResult), hipMemcpyDeviceToHost);
                {
                    std::lock_guard<std::mutex> lk(shared.vanity_mtx);
                    shared.vanity_results.insert(shared.vanity_results.end(),
                                                  results.begin(), results.end());
                }
                shared.vanity_total.fetch_add(n_new, std::memory_order_relaxed);
            }
            last_vanity_count = capped;
        }

        if (stop_all || g_sigint) break;

        unsigned int h_any = 0u;
        hipMemcpy(&h_any, d_any_left, sizeof(unsigned int), hipMemcpyDeviceToHost);

        if (random_mode) {
            // Chunk done — loop back to pick a new random position (no swap)
        } else {
            std::swap(d_Px, d_Rx);
            std::swap(d_Py, d_Ry);
            if (h_any == 0u) { completed_all = true; break; }
        }
    }

    hipDeviceSynchronize();

    // Read back any remaining vanity results (from the last kernel launch)
    if (shared.vanity_nibbles > 0 && d_vanity_count_gpu) {
        uint32_t current_count = 0;
        hipMemcpy(&current_count, d_vanity_count_gpu, sizeof(uint32_t), hipMemcpyDeviceToHost);
        uint32_t capped = std::min(current_count, shared.vanity_max_results);
        if (capped > last_vanity_count) {
            uint32_t n_new = capped - last_vanity_count;
            std::vector<VanityResult> results(n_new);
            hipMemcpy(results.data(),
                      (VanityResult*)d_vanity_buf + last_vanity_count,
                      (size_t)n_new * sizeof(VanityResult), hipMemcpyDeviceToHost);
            {
                std::lock_guard<std::mutex> lk(shared.vanity_mtx);
                shared.vanity_results.insert(shared.vanity_results.end(),
                                              results.begin(), results.end());
            }
            shared.vanity_total.fetch_add(n_new, std::memory_order_relaxed);
        }
        hipFree(d_vanity_count_gpu);
        if (d_vanity_buf) hipFree(d_vanity_buf);
    }

    hipFree(d_start_scalars); hipFree(d_Px); hipFree(d_Py);
    hipFree(d_Rx); hipFree(d_Ry); hipFree(d_counts256);
    hipFree(d_found_flag); hipFree(d_found_result);
    hipFree(d_hashes_accum); hipFree(d_any_left);
    hipStreamDestroy(streamKernel);

    if (completed_all)
        shared.gpus_exhausted.fetch_add(1, std::memory_order_relaxed);
}


