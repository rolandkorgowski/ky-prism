#include "thumbnails.h"
#include "document.h"
#include "exif_fixture.h"
#include "prefetch.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <iostream>
static void check(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
}
static void waitFor(const std::function<bool()> &done) {
  QElapsedTimer t;
  t.start();
  while (!done() && t.elapsed() < 10000) {
    QCoreApplication::processEvents();
    QThread::msleep(2);
  }
  check(done(), "thumbnail worker timeout");
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QTemporaryDir isolatedCache;
  qputenv("PRISM_THUMB_CACHE_DIR", isolatedCache.path().toUtf8());
  if (VIPS_INIT(argv[0]))
    return 2;
  vips_concurrency_set(4);
  vips_cache_set_max(0);
  int result = 0;
  try {
    auto token = std::make_shared<std::atomic_bool>(false);
    if (argc == 2) {
      QDir dir(QString::fromLocal8Bit(argv[1]));
      int count = 0;
      for (const auto &fi : dir.entryInfoList(QDir::Files)) {
        auto ext = fi.suffix().toLower();
        if (!QStringList{"jpg", "jpeg", "tif", "tiff", "3fr", "fff"}.contains(
                ext))
          continue;
        QElapsedTimer t;
        t.start();
        auto im = decodeThumbnail(fi.absoluteFilePath(), 256, token);
        check(!im.isNull() && im.width() <= 256 && im.height() <= 256,
              "corpus thumbnail");
        std::cout << fi.fileName().toStdString()
                  << " thumbnail_ms=" << t.elapsed() << std::endl;
        ++count;
      }
      std::cout << "corpus_thumbnails=" << count << std::endl;
    } else {
      QTemporaryDir tmp;
      QStringList files;
      auto image = vips::VImage::black(2051, 1025,
                                       vips::VImage::option()->set("bands", 3))
                       .new_from_image({200, 70, 25})
                       .copy(vips::VImage::option()->set(
                           "interpretation", VIPS_INTERPRETATION_sRGB));
      for (int i = 0; i < 32; ++i) {
        auto name = tmp.path() + QString("/Zażółć [#] %1.jpg").arg(i);
        image.jpegsave(name.toUtf8().constData());
        files.append(name);
      }
      auto im = decodeThumbnail(files.first(), 160, token);
      check(im.width() == 160 && im.height() <= 81,
            "bounded thumbnail geometry");
      check(std::abs(im.pixelColor(10, 10).red() - 200) <= 2,
            "thumbnail color");
      auto tif = tmp.path() + "/rotated.tif";
      image.set(VIPS_META_ORIENTATION, 6);
      image.tiffsave(tif.toUtf8().constData());
      auto rotated = decodeThumbnail(tif, 256, token);
      check(rotated.height() == 256 && rotated.width() <= 129,
            "TIFF thumbnail orientation");
      QFile source(files.first());
      check(source.open(QIODevice::ReadOnly), "JPEG fixture");
      auto encoded = source.readAll();
      source.close();
      const auto smallPath = tmp.path() + "/camera-preview.jpg";
      auto unrotated = image.copy();
      unrotated.set(VIPS_META_ORIENTATION, 1);
      unrotated.thumbnail_image(160).jpegsave(smallPath.toUtf8().constData());
      QFile small(smallPath);
      check(small.open(QIODevice::ReadOnly), "camera preview fixture");
      const auto cameraPath = tmp.path() + "/camera.jpg";
      QFile cameraFile(cameraPath);
      check(cameraFile.open(QIODevice::WriteOnly), "embedded preview fixture");
      cameraFile.write(withExifThumbnail(encoded, small.readAll()));
      cameraFile.close();
      check(decodeThumbnail(cameraPath, 512, token, true).width() == 160,
            "quick embedded preview missing");
      check(decodeThumbnail(cameraPath, 512, token).width() == 512,
            "small camera preview replaced high-quality thumbnail");
      {
        ThumbnailCache camera;
        camera.setActive(true);
        camera.request(QFileInfo(cameraPath), 128);
        waitFor([&] { return !camera.busy(); });
        check(camera.cameraPreviews() == 1 && camera.sourceDecodes() == 1,
              "camera preview did not upgrade exactly once");
        check(camera.request(QFileInfo(cameraPath), 128).width() == 512,
              "provisional image remained after upgrade");
      }
      for (auto blob : {QByteArray("bad"), QByteArray::fromHex("ffd8ffffff")}) {
        auto name = tmp.path() + "/malformed-thumb.jpg";
        QFile f(name);
        check(f.open(QIODevice::WriteOnly), "malformed thumbnail fixture");
        f.write(withExifThumbnail(encoded, blob));
        f.close();
        check(!decodeThumbnail(name, 128, token).isNull(),
              "malformed EXIF thumbnail fallback");
      }
      auto corrupt = tmp.path() + "/broken.jpg";
      QFile f(corrupt);
      check(f.open(QIODevice::WriteOnly), "corrupt fixture");
      f.write("not a JPEG");
      f.close();
      bool failed = false;
      try {
        decodeThumbnail(corrupt, 128, token);
      } catch (const std::exception &) {
        failed = true;
        vips_error_clear();
      }
      check(failed, "corrupt thumbnail rejection");
      token->store(true);
      failed = false;
      try {
        decodeThumbnail(files.first(), 128, token);
      } catch (const std::exception &) {
        failed = true;
      }
      check(failed, "thumbnail cancellation");
      token->store(false);
      ThumbnailCache cache;
      cache.request(QFileInfo(files.first()), 128);
      check(!cache.busy(), "inactive cache did work");
      cache.setActive(true);
      cache.setPaused(true);
      for (auto &name : files)
        cache.request(QFileInfo(name), 128);
      check(!cache.busy(), "paused cache did work");
      cache.setPaused(false);
      for (auto &name : files)
        cache.request(QFileInfo(name), 128);
      check(cache.pendingCount() <= 192 + cache.workerCount(),
            "unbounded thumbnail queue");
      waitFor([&] { return !cache.busy(); });
      check(cache.cachedKiB() > 0 && cache.cachedKiB() <= 1024 * 1024,
            "thumbnail RAM limit");
      // Resizing the sidebar must reuse a decoded thumbnail immediately,
      // rather than exposing a placeholder and queueing a new JPEG per pixel.
      cache.request(QFileInfo(files.first()), 128);
      waitFor([&] { return !cache.busy(); });
      for (int edge = 64; edge <= 512; ++edge)
        check(!cache.request(QFileInfo(files.first()), edge).isNull(),
              "sidebar resize discarded an already loaded thumbnail");
      check(!cache.busy(), "sidebar resize scheduled redundant decodes");
      for (int cycle = 0; cycle < 12; ++cycle) {
        for (auto &name : files)
          cache.request(QFileInfo(name), 256);
        cache.discardRequests();
        cache.setActive(false);
        cache.setActive(true);
      }
      cache.setActive(false);
      waitFor([&] { return !cache.busy(); });
      check(cache.pendingCount() == 0, "cancelled thumbnail jobs retained");
      Document doc;
      auto sharp = doc.open(files.first(), {}, token, {}, 2048);
      check(sharp.preview.width() >= 2048, "sharp preview fit resolution");
      auto hidpi = doc.open(files.first(), {}, token, {}, 4096);
      check(hidpi.preview.size() == QSize(2051, 1025),
            "HiDPI preview native detail");
      Document another;
      int callbacks = 0;
      another.open(
          files.first(), {}, token,
          [&](const Info &cached) {
            check(cached.preview == sharp.preview, "shared sharp cache pixels");
            ++callbacks;
          },
          2048);
      check(callbacks == 1, "cross-document shared sharp cache");
      auto fit = doc.open(files[3], {}, token, {}, 2048, {1600, 500});
      check(fit.preview.width() >= 1001 && fit.preview.width() < 1600,
            "aspect-aware sharp preview");
      auto bigger = doc.open(files[3], {}, token, {}, 2048, {1600, 900});
      check(bigger.preview.width() >= 1600,
            "undersized cached preview reused after enlargement");
      PreviewPrefetch prefetch;
      prefetch.prepare({files[1], files[2], files[3]}, {}, 2048);
      waitFor([&] { return !prefetch.busy(); });
      callbacks = 0;
      another.open(
          files[1], {}, token, [&](const Info &) { ++callbacks; }, 2048);
      check(callbacks == 1, "adjacent sharp prefetch");
      prefetch.prepare(files, {}, 2048);
      prefetch.cancel();
      waitFor([&] { return !prefetch.busy(); });
      std::cout << "PASS: bounded thumbnails, EXIF orientation/corruption, "
                   "cancellation, active/paused lifecycle, shared sharp RAM "
                   "cache, adjacent prefetch\n";
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    result = 1;
  }
  Document::clearSharedPreviews();
  vips_shutdown();
  return result;
}
