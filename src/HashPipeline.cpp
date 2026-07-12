// HashPipeline.cpp — GPU hash pipeline (SHA-256 + RIPEMD-160)
//
// This file is the single-TU entry point for GPU hash code.
// It includes modular headers that compose the full pipeline:
//   gpu_sha256.hpp      — SHA-256 compression function primitives
//   gpu_ripemd160.hpp   — RIPEMD-160 compression function primitives
//   gpu_utils.hpp       — GPU utility functions (endian, compare, add)
//   gpu_hash_pipeline.hpp — High-level pipeline: SHA-256→RIPEMD-160→match
//
// Included directly by GPUWorker.cpp (single-TU style — no -fgpu-rdc needed).

#include "GpuPlatform.h"
#include "AMDHash.h"
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "gpu_sha256.hpp"
#include "gpu_ripemd160.hpp"
#include "gpu_utils.hpp"
#include "gpu_hash_pipeline.hpp"
