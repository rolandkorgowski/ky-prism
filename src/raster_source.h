#pragma once
#include <QString>
#include <atomic>
#include <memory>
#include <vips/vips8>
// Read-only TIFF view. Ignore oversized private Photoshop editing layers while
// preserving the flattened pixels, ICC, EXIF and all original file offsets.
// This keeps libtiff's normal allocation limits enabled.
class RasterSource {
  VipsSource *value = nullptr;

public:
  bool renderedTiff = false, rgbPreview = false, skippedLayers = false;
  explicit RasterSource(const QString &path,
                        const std::shared_ptr<std::atomic_bool> &cancel = {});
  ~RasterSource();
  RasterSource(const RasterSource &) = delete;
  RasterSource &operator=(const RasterSource &) = delete;
  VipsSource *get() const { return value; }
};
