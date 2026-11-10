#pragma once
#include <cuda_runtime_api.h>
// Own CUDA kernels; no NVIDIA sample/source code is copied here.
cudaError_t prismResizeRgb(const unsigned char *, size_t, int, int, float4 *,
                           float *, float *, int *, int *, unsigned char *, int,
                           int, int, int, cudaStream_t);
