#include "thumbnail_disk.h"
#include "thumbnails.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFileIconProvider>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <sys/resource.h>
#include <unistd.h>
#include <vips/vips8>
static void check(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
}
static void waitFor(const std::function<bool()> &done, int ms = 120000) {
  QElapsedTimer timer;
  timer.start();
  while (!done() && timer.elapsed() < ms) {
    QCoreApplication::processEvents();
    QThread::msleep(1);
  }
  check(done(), "gallery timeout");
}
int main(int argc, char **argv) {
  const bool gpu = QStringList(argv, argv + argc).contains("--gpu");
  const bool stress = QStringList(argv, argv + argc).contains("--stress");
  if (!gpu && qEnvironmentVariableIsEmpty("DISPLAY") &&
      qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  if (gpu && (qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen" ||
              (qEnvironmentVariableIsEmpty("DISPLAY") &&
               qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")))) {
    std::cerr << "GPU benchmark requires a desktop.\n";
    return 77;
  }
  if (qEnvironmentVariable("QT_QPA_PLATFORMTHEME").startsWith("gtk"))
    qunsetenv("QT_QPA_PLATFORMTHEME");
  QApplication app(argc, argv);
  if (VIPS_INIT(argv[0]))
    return 2;
  vips_concurrency_set(2);
  vips_cache_set_max(0);
  int rc = 0;
  try {
    QTemporaryDir fixture, disk;
    qputenv("PRISM_THUMB_CACHE_DIR", disk.path().toUtf8());
    qputenv("PRISM_THUMB_DISK_CACHE", "1");
    const bool synthetic =
        argc < 2 || QString::fromLocal8Bit(argv[1]).startsWith("--");
    QString directory;
    if (synthetic) {
      qputenv("PRISM_THUMB_RAM_MIB", "128");
      directory = fixture.path();
      auto im = vips::VImage::black(1024, 1024,
                                    vips::VImage::option()->set("bands", 3))
                    .new_from_image({200, 70, 25})
                    .copy(vips::VImage::option()->set(
                        "interpretation", VIPS_INTERPRETATION_sRGB));
      const auto source = directory + "/0000.jpg";
      im.jpegsave(source.toUtf8().constData());
      QFile file(source);
      check(file.open(QIODevice::ReadOnly), "fixture");
      auto bytes = file.readAll();
      for (int i = 1; i < 800; ++i) {
        QFile out(directory + QString("/%1.jpg").arg(i, 4, 10, QChar('0')));
        check(out.open(QIODevice::WriteOnly) &&
                  out.write(bytes) == bytes.size(),
              "800-file fixture");
      }
    } else
      directory = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    QElapsedTimer clock;
    clock.start();
    auto files = QDir(directory).entryInfoList(
        {"*.jpg", "*.jpeg", "*.JPG", "*.JPEG"}, QDir::Files, QDir::Name);
    const auto listingMs = clock.elapsed();
    check(files.size() >= 80, "At least 80 JPEG entries required");
    QJsonObject report{
        {"entries", files.size()},
        {"listing_ms", listingMs},
        {"visible_target", 80},
        {"synthetic", synthetic},
        {"method", "uncached thumbnail directory; OS file cache not dropped"}};
    QElapsedTimer heartbeatClock;
    heartbeatClock.start();
    qint64 longestGap = 0;
    QTimer heartbeat;
    heartbeat.setInterval(5);
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] {
      longestGap = std::max(longestGap, heartbeatClock.restart());
    });
    heartbeat.start();
    auto phase = [&](ThumbnailCache &cache) {
      cache.setActive(true);
      clock.restart();
      qint64 first = -1, majority = -1;
      waitFor([&] {
        int ready = 0;
        for (int i = 0; i < 80; ++i)
          if (!cache.request(files.at(i), 512).isNull())
            ++ready;
        if (ready && first < 0)
          first = clock.elapsed();
        if (ready >= 60 && majority < 0)
          majority = clock.elapsed();
        return ready == 80 && !cache.busy();
      });
      report[cache.diskHits() ? "disk_first_visible_ms" : "first_visible_ms"] =
          first;
      report[cache.diskHits() ? "disk_first_60_ms" : "first_60_ms"] = majority;
      return clock.elapsed();
    };
    {
      ThumbnailCache cold;
      report["uncached_80_ms"] = phase(cold);
      report["workers"] = cold.workerCount();
      report["source_decodes"] = double(cold.sourceDecodes());
      report["camera_previews"] = double(cold.cameraPreviews());
      report["ram_kib"] = cold.cachedKiB();
      check(cold.diskHits() == 0, "unexpected cache hit in new disk directory");
      const auto before = cold.taskStarts();
      for (int frame = 0; frame < 60; ++frame)
        for (int i = 0; i < 80; ++i)
          check(!cold.request(files.at(i), 64 + frame * 7).isNull(),
                "80-image cache eviction/flicker");
      check(cold.taskStarts() == before && !cold.busy(),
            "warm redraw restarted workers");
    }
    ThumbnailCache warm;
    report["disk_80_ms"] = phase(warm);
    report["disk_hits"] = double(warm.diskHits());
    check(warm.sourceDecodes() == 0, "warm folder reopened JPEG source");
    check(warm.diskHits() == 80,
          "restart did not reuse all persisted previews");
    if (synthetic) {
      // Ten pages, with scroll invalidations and off/on transitions, exercise
      // bounded parallel work and all 800 file identities, not just page one.
      for (int page = 1; page < 10; ++page) {
        warm.discardRequests();
        waitFor([&] {
          int ready = 0;
          for (int i = page * 80; i < (page + 1) * 80; ++i)
            ready += !warm.request(files.at(i), 512).isNull();
          return ready == 80 && !warm.busy();
        });
        warm.setActive(false);
        warm.setActive(true);
      }
      check(warm.cachedKiB() <= 1024 * 1024, "RAM limit after 800 entries");
      report["paged_entries"] = 800;
      report["all_pages_ram_kib"] = warm.cachedKiB();
    }
    if (stress && synthetic) {
      QJsonArray memory, descriptors;
      QList<qint64> samples;
      QList<int> fds;
      const auto decodes = warm.sourceDecodes();
      for (int round = 0; round < 6; ++round) {
        for (int page = 0; page < 10; ++page)
          waitFor([&] {
            int ready = 0;
            for (int i = page * 80; i < (page + 1) * 80; ++i)
              ready += !warm.request(files.at(i), 512).isNull();
            return ready == 80 && !warm.busy();
          });
        QFile statm("/proc/self/statm");
        check(statm.open(QIODevice::ReadOnly), "RSS unavailable");
        auto fields = statm.readAll().split(' ');
        check(fields.size() >= 2, "RSS schema");
        const auto rss = fields.at(1).toLongLong() * sysconf(_SC_PAGESIZE);
        samples.append(rss);
        memory.append(double(rss));
        const auto count =
            QDir("/proc/self/fd")
                .entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
                .size();
        fds.append(count);
        descriptors.append(count);
      }
      auto settled = samples.mid(2);
      const auto bounds = std::minmax_element(settled.begin(), settled.end());
      check(*bounds.second - *bounds.first < 32 * 1024 * 1024,
            "RSS did not stabilize after cache eviction");
      check(fds.last() <= fds.at(2) + 2, "thumbnail file descriptor growth");
      check(warm.sourceDecodes() == decodes, "stress reread JPEG sources");
      report["stress_rss_bytes"] = memory;
      report["stress_descriptors"] = descriptors;
      report["stress_visits"] = 4800;
      report["stress_settled_span_bytes"] = *bounds.second - *bounds.first;
    }
    if (gpu) {
      QWidget window;
      window.resize(3840, 2160);
      QHBoxLayout layout(&window);
      auto grid = new ThumbnailGrid;
      grid->setFixedSize(1440, 1280);
      grid->enableGpu();
      class Icons final : public QFileIconProvider {
        QIcon icon(const QFileInfo &) const override { return {}; }
        QIcon icon(IconType) const override { return {}; }
      } icons;
      QFileSystemModel model;
      model.setIconProvider(&icons);
      model.setReadOnly(true);
      model.setFilter(QDir::Files | QDir::NoDotAndDotDot);
      model.setRootPath(directory);
      LibraryProxy proxy;
      proxy.setSourceModel(&model);
      grid->setModel(&proxy);
      grid->setRootIndex(proxy.mapFromSource(model.index(directory)));
      grid->setItemDelegate(new ThumbnailDelegate(&warm, grid));
      QObject::connect(&warm, &ThumbnailCache::changed, grid,
                       [grid] { grid->viewport()->update(); });
      layout.addWidget(grid);
      layout.addStretch();
      window.show();
      clock.restart();
      waitFor(
          [&] {
            return grid->gpuReady() && grid->gpuUploads() >= 80 && !warm.busy();
          },
          15000);
      auto firstFrame = grid->gpuFrame();
      check(!firstFrame.isNull(), "missing GPU frame");
      if (synthetic) {
        const auto first = proxy.index(0, 0, grid->rootIndex());
        const auto cell = grid->visualRect(first);
        const auto point = QPoint(cell.center().x(), cell.top() + 60) *
                           grid->devicePixelRatioF();
        const auto pixel = firstFrame.pixelColor(point);
        check(std::abs(pixel.red() - 200) <= 3 &&
                  std::abs(pixel.green() - 70) <= 3 &&
                  std::abs(pixel.blue() - 25) <= 3,
              "GPU thumbnail pixels do not match decoded image");
      }
      report["gpu_ready_ms"] = clock.elapsed();
      report["gpu_frame_width"] = firstFrame.width();
      report["gpu_frame_height"] = firstFrame.height();
      report["renderer"] = grid->gpuRenderer();
      auto uploads = grid->gpuUploads();
      for (int i = 0; i < 20; ++i) {
        grid->viewport()->update();
        QCoreApplication::processEvents();
      }
      check(grid->gpuUploads() == uploads,
            "GPU textures uploaded again on redraw");
      for (int i = 0; i < 80; ++i) {
        grid->setFixedWidth(1440 + i % 3 * 7);
        QCoreApplication::processEvents();
      }
      check(grid->gpuReady(), "GPU viewport lost after resize");
      check(grid->gpuBytes() <= 512ULL * 1024 * 1024, "VRAM limit exceeded");
      report["gpu_uploads"] = double(uploads);
      report["vram_bytes"] = double(grid->gpuBytes());
      window.close();
    }
    // Corrupted disk entries are rejected without trusting allocation lengths.
    const QString key = "corrupt-test";
    QImage image(512, 512, QImage::Format_RGBA8888);
    image.fill(QColor(200, 70, 25));
    ThumbnailDisk::write(disk.path(), key, image);
    check(ThumbnailDisk::read(disk.path(), key) == image,
          "lossless disk round trip");
    const auto name =
        disk.path() + '/' +
        QString::fromLatin1(
            QCryptographicHash::hash(("ky-prism-thumb-v3/" + key).toUtf8(),
                                     QCryptographicHash::Sha256)
                .toHex()) +
        ".kthumb";
    QFile damaged(name);
    check(damaged.open(QIODevice::ReadWrite), "corruption fixture");
    auto valid = damaged.readAll();
    auto badHeader = valid;
    badHeader[8] = char(0x7f); // absurd width must not allocate an image
    damaged.seek(0);
    damaged.write(badHeader);
    damaged.flush();
    check(ThumbnailDisk::read(disk.path(), key).isNull(),
          "unbounded dimensions accepted");
    damaged.seek(0);
    valid[24] ^= 1; // checksum mismatch
    damaged.write(valid);
    damaged.flush();
    check(ThumbnailDisk::read(disk.path(), key).isNull(),
          "corrupted pixels accepted");
    damaged.resize(20);
    check(ThumbnailDisk::read(disk.path(), key).isNull(),
          "truncated cache accepted");
    damaged.close();
    struct rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    report["peak_rss_kib"] = double(usage.ru_maxrss);
    report["longest_event_loop_gap_ms"] = longestGap;
    std::cout
        << QJsonDocument(report).toJson(QJsonDocument::Indented).constData();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    rc = 1;
  }
  vips_shutdown();
  return rc;
}
