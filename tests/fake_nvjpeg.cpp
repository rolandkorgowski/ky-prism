// Test double for the module ABI only. It performs NO NVIDIA/GPU operations.
#include "nvjpeg_abi.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
namespace {
void *create(char *s, size_t n) {
  if (std::getenv("PRISM_FAKE_NVJPEG_SLOW_INIT"))
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  std::snprintf(s, n, "TEST DOUBLE, not CUDA");
  return new (std::nothrow) int(1);
}
void destroy(void *p) { delete static_cast<int *>(p); }
int decode(void *, const unsigned char *, size_t, int, int, unsigned char *out,
           int w, int h, int (*cancel)(void *), void *arg, char *error,
           size_t cap) {
  if (cancel(arg))
    return 4;
  if (std::getenv("PRISM_FAKE_NVJPEG_FAIL")) {
    std::snprintf(error, cap, "TEST DOUBLE injected decode failure");
    return 3;
  }
  for (int i = 0; i < w * h; ++i) {
    out[3 * i] = 200;
    out[3 * i + 1] = 70;
    out[3 * i + 2] = 25;
  }
  return 0;
}
} // namespace
extern "C" const PrismNvjpegApi *prism_nvjpeg_api() {
  static const PrismNvjpegApi good{1, create, destroy, decode},
      bad{9876, create, destroy, decode};
  return std::getenv("PRISM_FAKE_NVJPEG_BAD_ABI") ? &bad : &good;
}
