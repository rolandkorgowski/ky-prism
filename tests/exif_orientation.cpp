// Raw-EXIF orientation fallback (libvips without libexif, e.g. OpenBSD).
#include "exif_orientation.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}
// "Exif\0\0" + TIFF header + IFD0 with one entry (tag, type, count, value).
std::vector<unsigned char> blob(bool little, unsigned tag, unsigned type,
                                unsigned count, unsigned value) {
  std::vector<unsigned char> d = {'E', 'x', 'i', 'f', 0, 0};
  auto u16 = [&](unsigned v) {
    if (little) { d.push_back(v & 255); d.push_back(v >> 8 & 255); }
    else { d.push_back(v >> 8 & 255); d.push_back(v & 255); }
  };
  auto u32 = [&](unsigned v) {
    if (little) for (int i = 0; i < 4; ++i) d.push_back(v >> (8 * i) & 255);
    else for (int i = 3; i >= 0; --i) d.push_back(v >> (8 * i) & 255);
  };
  d.push_back(little ? 'I' : 'M');
  d.push_back(little ? 'I' : 'M');
  u16(42);
  u32(8);
  u16(1);
  u16(tag);
  u16(type);
  u32(count);
  u16(value);
  u16(0);
  u32(0);
  return d;
}
} // namespace

int main(int, char **argv) {
  if (VIPS_INIT(argv[0]))
    return 2;
  auto le = blob(true, 0x0112, 3, 1, 6);
  check(exifOrientationFromBlob(le.data(), le.size()) == 6, "little-endian 6");
  auto be = blob(false, 0x0112, 3, 1, 8);
  check(exifOrientationFromBlob(be.data(), be.size()) == 8, "big-endian 8");
  check(exifOrientationFromBlob(be.data() + 6, be.size() - 6) == 8, "no Exif prefix");
  auto other = blob(true, 0x010f, 3, 1, 6);
  check(exifOrientationFromBlob(other.data(), other.size()) == 0, "other tag");
  auto type = blob(true, 0x0112, 4, 1, 6);
  check(exifOrientationFromBlob(type.data(), type.size()) == 0, "wrong type");
  auto range = blob(true, 0x0112, 3, 1, 9);
  check(exifOrientationFromBlob(range.data(), range.size()) == 0, "out of range");
  // Prefix 6 + TIFF header 8 + count 2 + entry 12: any shorter cut loses it.
  for (std::size_t cut = 0; cut < 28; ++cut)
    check(exifOrientationFromBlob(le.data(), cut) == 0, "truncated block");
  check(exifOrientationFromBlob(le.data(), 28) == 6, "entry complete");
  auto far = le;
  far[10] = 0xff; // IFD0 offset far beyond the block
  check(exifOrientationFromBlob(far.data(), far.size()) == 0, "IFD outside");
  check(exifOrientationFromBlob(nullptr, 0) == 0, "no data");

  // An image whose only orientation is in the raw block turns upright.
  auto image = vips::VImage::black(60, 30, vips::VImage::option()->set("bands", 3)).copy();
  vips_image_set_blob_copy(image.get_image(), VIPS_META_EXIF_NAME, le.data(), le.size());
  check(exifOrientation(image) == 6, "orientation from raw EXIF");
  auto upright = withExifOrientation(image).autorot();
  check(upright.width() == 30 && upright.height() == 60, "autorot uses it");
  // libvips' own value wins when present.
  image.set(VIPS_META_ORIENTATION, 3);
  check(exifOrientation(image) == 3, "libvips value preferred");
  vips_shutdown();
  if (failures)
    return 1;
  std::puts("exif orientation: ok");
  return 0;
}
