// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Krzysztof Kylen Lenartowicz
#include "thumbnail_cache.h"
#include "nvjpeg_backend.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (VIPS_INIT(argv[0])) return 2;
  int code = 0;
  try {
    // This executable is deliberately placed away from either adapter lookup
    // directory. AUTO must try the missing module, then decode with libvips.
    if (NvjpegBackend::available()) throw std::runtime_error("Isolated fallback test found an adapter");
    QTemporaryDir tmp;
    auto path = tmp.path() + "/zdjęcie [auto].jpg";
    auto image = vips::VImage::black(1000, 500, vips::VImage::option()->set("bands", 3))
                     .new_from_image({120, 90, 60})
                     .copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB));
    image.jpegsave(path.toUtf8().constData());
    auto token = std::make_shared<std::atomic_bool>(false);
    qputenv("PRISM_JPEG_BACKEND", "auto");
    auto automatic = decodeThumbnail(path, 128, token);
    if (automatic.isNull() || automatic.width() != 128 || automatic.height() != 64 ||
        NvjpegBackend::decoded() || !NvjpegBackend::fallbacks())
      throw std::runtime_error("AUTO did not fall back to a valid CPU thumbnail");
    qputenv("PRISM_JPEG_BACKEND", "cpu");
    auto cpu = decodeThumbnail(path, 128, token);
    if (automatic != cpu) throw std::runtime_error("AUTO fallback differs from the CPU reference");
    std::cout << "PASS AUTO: unavailable nvJPEG adapter -> identical libvips CPU thumbnail\n";
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; code = 1; }
  vips_shutdown();
  return code;
}
