#include "exif_orientation.h"
#include "nvjpeg_backend.h"
#include "thumbnail_cache.h"
#include <QColorSpace>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <chrono>
#include <iostream>
#include <thread>
static void check(bool ok, const char *text) {
  if (!ok)
    throw std::runtime_error(text);
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (VIPS_INIT(argv[0]))
    return 2;
  vips_cache_set_max(0);
  vips_concurrency_set(2);
  int result = 0;
  try {
    QTemporaryDir dir;
    const auto path = dir.path() + "/Zażółć [#] GPU.jpg";
    auto im =
        vips::VImage::black(1027, 683, vips::VImage::option()->set("bands", 3))
            .new_from_image({200, 70, 25});
    const auto icc = QColorSpace(QColorSpace::AdobeRgb).iccProfile();
    vips_image_set_blob_copy(im.get_image(), VIPS_META_ICC_NAME,
                             icc.constData(), icc.size());
    im.set(VIPS_META_ORIENTATION, 6);
    im.jpegsave(path.toUtf8().constData());
    // libvips without libexif (OpenBSD package) cannot write the orientation
    // tag: then the fixture is upright and so is its thumbnail.
    QFile saved(path);
    check(saved.open(QIODevice::ReadOnly), "fixture readable");
    const auto bytes = saved.readAll();
    const int stored = exifOrientation(
        vips::VImage::new_from_buffer(bytes.constData(), size_t(bytes.size()), ""));
    auto token = std::make_shared<std::atomic_bool>(false);
    qputenv("PRISM_JPEG_BACKEND", "cpu");
    const auto cpu = decodeThumbnail(path, 512, token);
    if (qEnvironmentVariableIsSet("PRISM_FAKE_NVJPEG_SLOW_INIT")) {
      std::thread init([] { NvjpegBackend::available(); });
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      QElapsedTimer timer;
      timer.start();
      NvjpegBackend::diagnostic();
      const auto elapsed = timer.elapsed();
      init.join();
      check(elapsed < 100,
            "diagnostics blocked GUI during CUDA initialization");
    }
    const bool badAbi = qEnvironmentVariableIsSet("PRISM_FAKE_NVJPEG_BAD_ABI");
    qputenv("PRISM_JPEG_BACKEND", "nvjpeg");
    if (badAbi) {
      bool rejected = false;
      try {
        decodeThumbnail(path, 512, token);
      } catch (const std::exception &) {
        rejected = true;
      }
      check(rejected && NvjpegBackend::decoded() == 0,
            "invalid module ABI accepted");
    } else {
      const auto decoded = decodeThumbnail(path, 512, token);
      check(decoded.size() == cpu.size() &&
                (stored < 5 || decoded.height() == 512),
            "EXIF orientation lost");
      const auto a = cpu.pixelColor(30, 30), b = decoded.pixelColor(30, 30);
      check(std::abs(a.red() - b.red()) <= 3 &&
                std::abs(a.green() - b.green()) <= 3 &&
                std::abs(a.blue() - b.blue()) <= 3,
            "ICC profile or RGBA conversion lost");
      check(NvjpegBackend::decoded() == 1, "successful module not counted");
    }
    qputenv("PRISM_FAKE_NVJPEG_FAIL", "1");
    qputenv("PRISM_JPEG_BACKEND", "auto");
    const auto fallback = decodeThumbnail(path, 512, token);
    check(fallback == cpu && NvjpegBackend::fallbacks() == 1,
          "failed module did not fall back safely");
    qputenv("PRISM_JPEG_BACKEND", "nvjpeg");
    bool failed = false;
    try {
      decodeThumbnail(path, 512, token);
    } catch (const std::exception &) {
      failed = true;
    }
    check(failed, "strict mode hid decode failure");
    token->store(true);
    failed = false;
    try {
      decodeThumbnail(path, 512, token);
    } catch (const std::exception &) {
      failed = true;
    }
    check(failed, "cancelled worker was not stopped");
    NvjpegBackend::shutdown();
    NvjpegBackend::shutdown();
    check(!NvjpegBackend::available(), "vendor context restarted after terminal cleanup");
    std::cout << "PASS: test-double ABI, ICC, EXIF, Unicode, strict error, "
                 "auto fallback, cancellation (NO GPU validation)\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    result = 1;
  }
  vips_shutdown();
  return result;
}
