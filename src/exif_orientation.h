#pragma once
// EXIF orientation (1–8) for libvips images. libvips parses EXIF with libexif;
// builds without it (e.g. the OpenBSD package) still attach the raw APP1 block
// as "exif-data" but set no "orientation", so portrait photos would appear
// lying on their side. This reads only tag 0x0112 of IFD0, with bounds checks.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vips/vips8>

inline int exifOrientationFromBlob(const unsigned char *d, std::size_t n) {
  if (!d)
    return 0;
  if (n >= 6 && !std::memcmp(d, "Exif\0\0", 6)) {
    d += 6;
    n -= 6;
  }
  if (n < 8)
    return 0;
  bool little;
  if (d[0] == 'I' && d[1] == 'I')
    little = true;
  else if (d[0] == 'M' && d[1] == 'M')
    little = false;
  else
    return 0;
  auto u16 = [&](std::size_t o) -> unsigned {
    return little ? unsigned(d[o]) | unsigned(d[o + 1]) << 8
                  : unsigned(d[o]) << 8 | unsigned(d[o + 1]);
  };
  auto u32 = [&](std::size_t o) -> std::uint32_t {
    return little ? std::uint32_t(d[o]) | std::uint32_t(d[o + 1]) << 8 |
                        std::uint32_t(d[o + 2]) << 16 |
                        std::uint32_t(d[o + 3]) << 24
                  : std::uint32_t(d[o]) << 24 | std::uint32_t(d[o + 1]) << 16 |
                        std::uint32_t(d[o + 2]) << 8 | std::uint32_t(d[o + 3]);
  };
  if (u16(2) != 42)
    return 0;
  const std::size_t ifd = u32(4);
  if (ifd + 2 > n)
    return 0;
  const unsigned entries = u16(ifd);
  for (unsigned i = 0; i < entries; ++i) {
    const std::size_t entry = ifd + 2 + std::size_t(i) * 12;
    if (entry + 12 > n)
      break;
    if (u16(entry) != 0x0112)
      continue;
    // SHORT, count 1: the value sits in the first bytes of the value field.
    if (u16(entry + 2) != 3 || u32(entry + 4) != 1)
      return 0;
    const unsigned value = u16(entry + 8);
    return value >= 1 && value <= 8 ? int(value) : 0;
  }
  return 0;
}

// Orientation libvips knows, else the one read from the raw EXIF block; 1 when
// there is none.
inline int exifOrientation(const vips::VImage &image) {
  if (image.get_typeof(VIPS_META_ORIENTATION))
    return image.get_int(VIPS_META_ORIENTATION);
  if (!image.get_typeof(VIPS_META_EXIF_NAME))
    return 1;
  const void *data = nullptr;
  std::size_t length = 0;
  if (vips_image_get_blob(image.get_image(), VIPS_META_EXIF_NAME, &data,
                          &length) ||
      length > (1u << 20)) {
    vips_error_clear();
    return 1;
  }
  const int found =
      exifOrientationFromBlob(static_cast<const unsigned char *>(data), length);
  return found ? found : 1;
}

// Sets "orientation" when only the raw EXIF block carries it, so autorot()
// turns the photo upright on every libvips build.
inline vips::VImage withExifOrientation(vips::VImage image) {
  if (image.get_typeof(VIPS_META_ORIENTATION))
    return image;
  const int orientation = exifOrientation(image);
  if (orientation <= 1)
    return image;
  image = image.copy();
  image.set(VIPS_META_ORIENTATION, orientation);
  return image;
}
