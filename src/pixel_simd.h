#pragma once
#include <cstddef>
namespace PixelSimd {
void rgbToRgba(const unsigned char *, unsigned char *, size_t pixels);
void scalarRgbToRgba(const unsigned char *, unsigned char *, size_t pixels);
const char *backend();
} // namespace PixelSimd
