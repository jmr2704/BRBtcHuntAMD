// GPUWorker.h — Shared types and forward declarations for the GPU solver.
// Extracted from the original single-translation-unit structure.

#ifndef GPUWORKER_H
#define GPUWORKER_H

#include <cstdint>
#include <atomic>
#include <mutex>
#include <vector>

#include "GpuPlatform.h"

// ── Found-result descriptor ───────────────────────────────────────────
struct FoundResult {
    int      threadId;
    int      iter;
    uint64_t scalar[4];
    uint64_t Rx[4];
    uint64_t Ry[4];
};

// ── Vanity match result (GPU-side) ─────────────────────────────────────
struct VanityResult {
    uint64_t privkey[4];
    uint64_t pubkey_x[4];
    uint32_t hash160[5];
    uint8_t  prefix;  // 0x02 or 0x03
};

// ── Shared state between GPU threads and main loop ─────────────────────
struct GpuShared {
    std::atomic<int>                any_found{0};
    std::mutex                      result_mtx;
    FoundResult                     best_result{};
    bool                            has_result{false};
    std::atomic<unsigned long long> total_hashes{0};
    std::atomic<unsigned long long> chunks_tried{0};
    std::atomic<int>                gpus_exhausted{0};
    std::atomic<int>                init_done{0};
    std::atomic<uint64_t>           cur_scalar_lo{0};
    std::atomic<uint64_t>           cur_scalar_hi{0};
    std::atomic<int>                setup_done{0};
    long double                     total_keys_adjusted{0.0L};
    std::atomic<uint32_t>           vanity_total{0};

    // Vanity config (host-side)
    uint32_t                  vanity_nibbles{0};
    uint32_t                  vanity_max_results{65536};
    std::vector<VanityResult> vanity_results;
    std::mutex                vanity_mtx;
};



// ── Constants ──────────────────────────────────────────────────────────
#ifndef WARP_SIZE
#define WARP_SIZE 32
#endif

#define FOUND_NONE  0
#define FOUND_LOCK  1
#define FOUND_READY 2

#ifndef MAX_BATCH_SIZE
#define MAX_BATCH_SIZE 2048
#endif

// ── Worker entry point (defined in GPUWorker.cpp) ──────────────────────
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
);

#endif // GPUWORKER_H