#include "pixel_simd.h"
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif
void PixelSimd::scalarRgbToRgba(const unsigned char *s, unsigned char *d,
                                size_t n) {
  for (size_t i = 0; i < n; ++i) {
    d[4 * i] = s[3 * i];
    d[4 * i + 1] = s[3 * i + 1];
    d[4 * i + 2] = s[3 * i + 2];
    d[4 * i + 3] = 255;
  }
}
namespace {
#if defined(__x86_64__) || defined(__i386__)
__attribute__((target("avx2"))) void avx2(const unsigned char *s,
                                          unsigned char *d, size_t n) {
  const auto mask =
      _mm256_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, 9, 10, 11, -1, 0,
                       1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, 9, 10, 11, -1);
  const auto alpha = _mm256_set1_epi32(int(0xff000000U));
  while (n >= 8) {
    // Exactly 24 input bytes: the second load is at +8, never +12.
    const auto low = _mm_loadu_si128(reinterpret_cast<const __m128i *>(s));
    const auto high = _mm_srli_si128(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(s + 8)), 4);
    const auto rgb = _mm256_set_m128i(high, low);
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(d),
                        _mm256_or_si256(_mm256_shuffle_epi8(rgb, mask), alpha));
    s += 24;
    d += 32;
    n -= 8;
  }
  PixelSimd::scalarRgbToRgba(s, d, n);
}
const bool accelerated = __builtin_cpu_supports("avx2");
#else
const bool accelerated = false;
#endif
} // namespace
void PixelSimd::rgbToRgba(const unsigned char *s, unsigned char *d, size_t n) {
#if defined(__x86_64__) || defined(__i386__)
  if (accelerated) {
    avx2(s, d, n);
    return;
  }
#endif
  scalarRgbToRgba(s, d, n);
}
const char *PixelSimd::backend() { return accelerated ? "AVX2" : "scalar"; }
