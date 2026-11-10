#include "pixel_simd.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
int main() {
  try {
    std::mt19937 random(923475);
    for (size_t n = 0; n <= 2049; ++n) {
      // No readable padding past the input: ASan catches oversized SIMD loads.
      std::vector<unsigned char> src(n * 3), expected(n * 4),
          actual(n * 4 + 32, 19);
      for (auto &v : src)
        v = random() & 255;
      PixelSimd::scalarRgbToRgba(src.data(), expected.data(), n);
      PixelSimd::rgbToRgba(src.data(), actual.data() + 7, n);
      if (!std::equal(expected.begin(), expected.end(), actual.begin() + 7) ||
          !std::all_of(actual.begin(), actual.begin() + 7,
                       [](auto v) { return v == 19; }) ||
          !std::all_of(actual.begin() + 7 + n * 4, actual.end(),
                       [](auto v) { return v == 19; }))
        throw std::runtime_error("SIMD conversion mismatch/canary overwrite");
    }
    std::cout << "PASS: exact RGB/RGBA, odd sizes, unaligned output, bounds; "
              << PixelSimd::backend() << '\n';
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
