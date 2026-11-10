#pragma once
#include <stddef.h>
#include <stdint.h>
// Private versioned C ABI. The CPU application never links CUDA libraries.
// Status: 0 success, 1 unavailable, 2 unsupported, 3 failed, 4 cancelled.
struct PrismNvjpegApi {
  uint32_t version;
  void *(*create)(char *, size_t);
  void (*destroy)(void *);
  int (*decode)(void *, const unsigned char *, size_t, int, int,
                unsigned char *, int, int, int (*)(void *), void *, char *,
                size_t);
};
using PrismNvjpegEntry = const PrismNvjpegApi *(*)();
