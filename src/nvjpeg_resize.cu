#include "nvjpeg_resize.h"
#include <cuda_runtime.h>
#include <math_constants.h>
namespace {
__device__ float sinc(float x) {
  x = fabsf(x);
  return x < 1.e-6f ? 1.f : sinf(CUDART_PI_F * x) / (CUDART_PI_F * x);
}
__global__ void plan(float *weights, int *starts, int in, int out, int taps) {
  const int p = blockIdx.x * blockDim.x + threadIdx.x;
  if (p >= out)
    return;
  const float scale = fmaxf(1.f, float(in) / out);
  const float center = (p + .5f) * float(in) / out - .5f;
  const int first = int(ceilf(center - 3.f * scale));
  starts[p] = first;
  float sum = 0;
  for (int k = 0; k < taps; ++k) {
    const int at = first + k;
    const float x = (at - center) / scale;
    const float w =
        at >= 0 && at < in && fabsf(x) < 3.f ? sinc(x) * sinc(x / 3.f) : 0.f;
    weights[p * taps + k] = w;
    sum += w;
  }
  for (int k = 0; k < taps; ++k)
    weights[p * taps + k] /= sum;
}
__global__ void horizontal(const unsigned char *src, size_t pitch, int iw,
                           int ih, float4 *temp, int ow, const float *weights,
                           const int *starts, int taps) {
  const size_t p = size_t(blockIdx.x) * blockDim.x + threadIdx.x;
  if (p >= size_t(ow) * ih)
    return;
  const int x = p % ow, y = p / ow, first = starts[x];
  float r = 0, g = 0, b = 0;
  for (int k = 0; k < taps; ++k) {
    const int sx = first + k;
    if (sx < 0 || sx >= iw)
      continue;
    const float w = weights[x * taps + k];
    const auto sample = src + size_t(y) * pitch + sx * 3;
    r = fmaf(w, float(sample[0]), r);
    g = fmaf(w, float(sample[1]), g);
    b = fmaf(w, float(sample[2]), b);
  }
  temp[p] = make_float4(r, g, b, 0);
}
__device__ unsigned char quantize(float f) {
  return static_cast<unsigned char>(fminf(255.f, fmaxf(0.f, roundf(f))));
}
__global__ void vertical(const float4 *src, int ow, int ih, unsigned char *out,
                         int oh, const float *weights, const int *starts,
                         int taps) {
  const int p = blockIdx.x * blockDim.x + threadIdx.x;
  if (p >= ow * oh)
    return;
  const int x = p % ow, y = p / ow, first = starts[y];
  float r = 0, g = 0, b = 0;
  for (int k = 0; k < taps; ++k) {
    const int sy = first + k;
    if (sy < 0 || sy >= ih)
      continue;
    const float w = weights[y * taps + k];
    const auto sample = src[size_t(sy) * ow + x];
    r = fmaf(w, sample.x, r);
    g = fmaf(w, sample.y, g);
    b = fmaf(w, sample.z, b);
  }
  out[p * 3] = quantize(r);
  out[p * 3 + 1] = quantize(g);
  out[p * 3 + 2] = quantize(b);
}
} // namespace
cudaError_t prismResizeRgb(const unsigned char *src, size_t pitch, int iw,
                           int ih, float4 *temp, float *xw, float *yw, int *xs,
                           int *ys, unsigned char *out, int ow, int oh, int xt,
                           int yt, cudaStream_t stream) {
  plan<<<(ow + 127) / 128, 128, 0, stream>>>(xw, xs, iw, ow, xt);
  plan<<<(oh + 127) / 128, 128, 0, stream>>>(yw, ys, ih, oh, yt);
  horizontal<<<(size_t(ow) * ih + 255) / 256, 256, 0, stream>>>(
      src, pitch, iw, ih, temp, ow, xw, xs, xt);
  vertical<<<(ow * oh + 255) / 256, 256, 0, stream>>>(temp, ow, ih, out, oh, yw,
                                                      ys, yt);
  return cudaGetLastError();
}
