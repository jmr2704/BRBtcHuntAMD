#ifndef GPU_PLATFORM_H
#define GPU_PLATFORM_H

#include <cstdlib>
#include <iostream>

// Default backend for the current production code. Future backends should set
// one of these macros from the build system before including this header.
#if !defined(BTC_GPU_BACKEND_HIP) && !defined(BTC_GPU_BACKEND_OPENCL)
#define BTC_GPU_BACKEND_HIP 1
#endif

#if defined(BTC_GPU_BACKEND_HIP)
#include <hip/hip_runtime.h>
#include <hip/hip_runtime_api.h>

#define BTC_GPU_BACKEND_NAME "HIP"
#define BTC_GPU_RUNTIME_NAME "HIP/ROCm"

// HIP versions differ on CUDA-compatible *_sync warp intrinsics,
// especially on Windows. Keep call sites backend-neutral.
#define BTC_SHFL_SYNC(mask, value, src_lane) __shfl((value), (src_lane))
#define BTC_SHFL_DOWN_SYNC(mask, value, delta) __shfl_down((value), (delta))
#define BTC_ANY_SYNC(mask, predicate) __any((predicate))
#define BTC_SYNCWARP(mask) ((void)0)

#define BTC_GPU_CHECK(ans) do { \
    hipError_t err__ = (ans); \
    if (err__ != hipSuccess) { \
        std::cerr << "HIP Error: " << hipGetErrorString(err__) \
                  << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(EXIT_FAILURE); \
    } \
} while (0)

#else
#error "Unsupported BTC GPU backend. Define BTC_GPU_BACKEND_HIP or add a backend implementation."
#endif

#define BTC_HOST_FAIL() std::exit(EXIT_FAILURE)

#endif // GPU_PLATFORM_H
