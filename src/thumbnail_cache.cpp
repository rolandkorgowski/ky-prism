// Toolkit-independent thumbnail decoder and cache (QtCore/QtGui only).
#include "thumbnail_cache.h"
#include "local_path.h"
#include "nvjpeg_backend.h"
#include "pixel_simd.h"
#include "exif_orientation.h"
#include "raster_source.h"
#include "thumbnail_disk.h"
#include <QDateTime>
#include <QFile>
#include <QFutureWatcher>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>
#include <cstring>
#include <libraw/libraw.h>
#include <stdexcept>
#include <unistd.h>
#include <vips/vips8>
using vips::VImage;
namespace {
// libvips renamed thumbnail's export_profile to output_profile (8.16). Ask the
// installed library which name it knows: Ubuntu 24.04 ships 8.15.
// Without lcms2 there is no colour management: returns nullptr, which ends
// the option list (callers pass the profile option last).
const char *thumbnailProfileOption() {
  static const char *const name = []() -> const char * {
    if (!vips_icc_present())
      return nullptr;
    const char *found = "output_profile";
    if (VipsOperation *op = vips_operation_new("thumbnail_source")) {
      GParamSpec *spec = nullptr;
      VipsArgumentClass *argumentClass = nullptr;
      VipsArgumentInstance *argument = nullptr;
      if (vips_object_get_argument(VIPS_OBJECT(op), "output_profile", &spec,
                                   &argumentClass, &argument)) {
        vips_error_clear();
        found = "export_profile";
      }
      g_object_unref(op);
    }
    return found;
  }();
  return name;
}
void cancelled(const std::shared_ptr<std::atomic_bool> &t) {
  if (t->load())
    throw std::runtime_error("Anulowano miniaturę");
}
QImage output(VImage image, const std::shared_ptr<std::atomic_bool> &token) {
  cancelled(token);
  image = image.colourspace(VIPS_INTERPRETATION_sRGB).cast(VIPS_FORMAT_UCHAR);
  const int bands = image.bands();
  if (bands != 3 && bands != 4)
    throw std::runtime_error("Nieobsługiwane kanały miniatury");
  vips_image_set_progress(image.get_image(), true);
  auto owner = new std::shared_ptr<std::atomic_bool>(token);
  g_signal_connect_data(
      image.get_image(), "eval",
      G_CALLBACK(+[](VipsImage *im, VipsProgress *, void *p) {
        if ((*static_cast<std::shared_ptr<std::atomic_bool> *>(p))->load())
          vips_image_set_kill(im, true);
      }),
      owner,
      +[](void *p, GClosure *) {
        delete static_cast<std::shared_ptr<std::atomic_bool> *>(p);
      },
      GConnectFlags(0));
  size_t bytes = 0;
  std::unique_ptr<void, decltype(&g_free)> allocation(
      image.write_to_memory(&bytes), &g_free);
  cancelled(token);
  if (bands == 3) {
    if (bytes != size_t(image.width()) * image.height() * 3)
      throw std::runtime_error("Nieprawidłowy bufor RGB miniatury");
    QImage rgba(image.width(), image.height(), QImage::Format_RGBA8888);
    if (rgba.isNull())
      throw std::bad_alloc();
    PixelSimd::rgbToRgba(static_cast<const unsigned char *>(allocation.get()),
                         rgba.bits(), size_t(image.width()) * image.height());
    return rgba;
  }
  auto data = allocation.release();
  return QImage(
      static_cast<uchar *>(data), image.width(), image.height(),
      image.width() * 4, QImage::Format_RGBA8888, [](void *p) { g_free(p); },
      data);
}
VImage memoryThumb(void *data, size_t length, int edge, int orientation = 1) {
  if (length < 2 || static_cast<unsigned char *>(data)[0] != 0xff ||
      static_cast<unsigned char *>(data)[1] != 0xd8)
    throw std::runtime_error("Nieprawidłowy JPEG miniatury");
  VipsImage *header = nullptr;
  if (vips_jpegload_buffer(data, length, &header, "fail_on", VIPS_FAIL_ON_ERROR,
                           nullptr))
    throw vips::VError();
  VImage check(header);
  if (quint64(check.width()) * check.height() > 32000000ULL)
    throw std::runtime_error("Osadzona miniatura przekracza limit");
  VipsImage *p = nullptr;
  if (vips_thumbnail_buffer(data, length, &p, edge, "height", edge, "size",
                            VIPS_SIZE_DOWN, "fail_on", VIPS_FAIL_ON_ERROR,
                            "no_rotate", true, thumbnailProfileOption(), "srgb",
                            nullptr))
    throw vips::VError();
  VImage result(p);
  result = result.copy();
  result.set(VIPS_META_ORIENTATION, orientation);
  return result.autorot();
}
} // namespace
QImage decodeThumbnail(const QString &path, int edge,
                       const std::shared_ptr<std::atomic_bool> &token,
                       bool embeddedOnly) {
  if (!validLocalPath(path) || edge < 16 || edge > 512 || !token)
    throw std::runtime_error("Nieprawidłowa miniatura");
  cancelled(token);
  if (embeddedOnly &&
      !QStringList{"jpg", "jpeg"}.contains(QFileInfo(path).suffix().toLower()))
    return {};
  const QSet<QString> raws = {"raw", "cr2", "cr3", "nef", "nrw", "arw",
                              "dng", "raf", "rw2", "orf", "pef", "srw",
                              "3fr", "fff", "iiq", "kdc", "mos"};
  QFileInfo fi(path);
  if (!fi.isFile() || !fi.isReadable())
    throw std::runtime_error("Nie można odczytać zdjęcia");
  std::unique_ptr<LibRaw> rawDecoder;
  std::unique_ptr<RasterSource> raster;
  if (raws.contains(fi.suffix().toLower())) {
    rawDecoder = std::make_unique<LibRaw>();
    []<typename D>(D &d) {
      if constexpr (requires { d.imgdata.rawparams.max_raw_memory_mb; })
        d.imgdata.rawparams.max_raw_memory_mb = 2048;
    }(*rawDecoder);
    rawDecoder->set_progress_handler(
        [](void *p, enum LibRaw_progress, int, int) {
          return static_cast<std::atomic_bool *>(p)->load() ? 1 : 0;
        },
        token.get());
    auto rc = rawDecoder->open_file(QFile::encodeName(path).constData());
    if (rc == LIBRAW_FILE_UNSUPPORTED) {
      raster = std::make_unique<RasterSource>(path, token);
      if (raster->rgbPreview)
        rawDecoder.reset();
      else
        throw std::runtime_error(libraw_strerror(rc));
    } else if (rc != LIBRAW_SUCCESS)
      throw std::runtime_error(libraw_strerror(rc));
  }
  if (rawDecoder) {
    auto &raw = *rawDecoder;
    raw.set_progress_handler(
        [](void *p, enum LibRaw_progress, int, int) {
          return static_cast<std::atomic_bool *>(p)->load() ? 1 : 0;
        },
        token.get());
    []<typename D>(D &d) {
      if constexpr (requires { d.imgdata.rawparams.max_raw_memory_mb; })
        d.imgdata.rawparams.max_raw_memory_mb = 2048;
    }(raw);
    auto check = [](int rc) {
      if (rc != LIBRAW_SUCCESS)
        throw std::runtime_error(libraw_strerror(rc));
    };

    if (quint64(raw.imgdata.sizes.raw_width) * raw.imgdata.sizes.raw_height >
        160000000ULL)
      throw std::runtime_error("RAW przekracza limit geometrii");
    if (raw.imgdata.thumbnail.tlength <= 32 * 1024 * 1024 &&
        raw.unpack_thumb() == LIBRAW_SUCCESS) {
      int rc = 0;
      std::unique_ptr<libraw_processed_image_t,
                      decltype(&LibRaw::dcraw_clear_mem)>
          mem(raw.dcraw_make_mem_thumb(&rc), &LibRaw::dcraw_clear_mem);
      if (rc == LIBRAW_SUCCESS && mem && mem->type == LIBRAW_IMAGE_JPEG &&
          mem->data_size <= 32 * 1024 * 1024)
        return output(memoryThumb(mem->data, mem->data_size, edge,
                                  raw.imgdata.sizes.flip == 3   ? 3
                                  : raw.imgdata.sizes.flip == 5 ? 8
                                  : raw.imgdata.sizes.flip == 6 ? 6
                                                                : 1),
                      token);
      if (rc == LIBRAW_SUCCESS && mem && mem->type == LIBRAW_IMAGE_BITMAP &&
          mem->bits == 8 && mem->colors == 3 &&
          quint64(mem->width) * mem->height <= 32000000ULL) {
        auto image =
            VImage::new_from_memory(mem->data, mem->data_size, mem->width,
                                    mem->height, 3, VIPS_FORMAT_UCHAR)
                .copy(VImage::option()->set("interpretation",
                                            VIPS_INTERPRETATION_sRGB));
        return output(
            image.thumbnail_image(edge, VImage::option()
                                            ->set("height", edge)
                                            ->set("size", VIPS_SIZE_DOWN)),
            token);
      }
    }
    cancelled(token);
    raw.imgdata.params.half_size = 1;
    raw.imgdata.params.output_bps = 8;
    raw.imgdata.params.output_color = 1;
    raw.imgdata.params.use_camera_wb = 1;
    raw.imgdata.params.no_auto_bright = 1;
    raw.imgdata.params.user_qual = 0;
    check(raw.unpack());
    check(raw.dcraw_process());
    int rc = 0;
    std::unique_ptr<libraw_processed_image_t,
                    decltype(&LibRaw::dcraw_clear_mem)>
        mem(raw.dcraw_make_mem_image(&rc), &LibRaw::dcraw_clear_mem);
    check(rc);
    if (!mem || mem->type != LIBRAW_IMAGE_BITMAP || mem->bits != 8 ||
        mem->colors != 3)
      throw std::runtime_error("Nieobsługiwany podgląd RAW");
    auto image = VImage::new_from_memory(mem->data, mem->data_size, mem->width,
                                         mem->height, 3, VIPS_FORMAT_UCHAR)
                     .copy(VImage::option()->set("interpretation",
                                                 VIPS_INTERPRETATION_sRGB));
    return output(
        image.thumbnail_image(
            edge,
            VImage::option()->set("height", edge)->set("size", VIPS_SIZE_DOWN)),
        token);
  }
  if (!raster)
    raster = std::make_unique<RasterSource>(path, token);
  const char *loader = vips_foreign_find_load_source(raster->get());
  if (!loader || (!strstr(loader, "LoadJpeg") && !strstr(loader, "LoadTiff") &&
                  !strstr(loader, "LoadPng")))
    throw std::runtime_error("Nieobsługiwany format miniatury");
  if (embeddedOnly && !strstr(loader, "LoadJpeg"))
    return {};
  VipsImage *header = nullptr;
  if (vips_call(loader, raster->get(), &header, "access",
                VIPS_ACCESS_SEQUENTIAL, "fail_on", VIPS_FAIL_ON_ERROR, nullptr))
    throw vips::VError();
  VImage image(header);
  if (quint64(image.width()) * image.height() > 2500000000ULL ||
      image.width() > 1000000 || image.height() > 1000000 || image.bands() > 5)
    throw std::runtime_error("Miniatura przekracza limit geometrii");
  if (!embeddedOnly && strstr(loader, "LoadJpeg")) {
    auto gpu = NvjpegBackend::thumbnail(path, image, edge, token);
    if (gpu.get_image())
      return output(gpu, token);
  }
  if (strstr(loader, "LoadJpeg") && image.get_typeof("jpeg-thumbnail-data")) {
    const void *data = nullptr;
    size_t n = 0;
    if (!vips_image_get_blob(image.get_image(), "jpeg-thumbnail-data", &data,
                             &n) &&
        n && n <= 2 * 1024 * 1024) {
      try {
        auto embedded = memoryThumb(const_cast<void *>(data), n, edge,
                                    exifOrientation(image));
        if (embeddedOnly ||
            std::max(embedded.width(), embedded.height()) >=
                std::min(edge, std::max(image.width(), image.height())))
          return output(embedded, token);
      } catch (const std::exception &) {
        vips_error_clear();
      }
    }
  }
  if (embeddedOnly)
    return {};
  // A source object keeps square brackets and shell metacharacters literal.
  auto source = raster->get();
  if (vips_source_rewind(source))
    throw vips::VError();
  VipsImage *thumb = nullptr;
  int rc = vips_thumbnail_source(source, &thumb, edge, "height", edge, "size",
                                 VIPS_SIZE_DOWN, "fail_on", VIPS_FAIL_ON_ERROR,
                                 thumbnailProfileOption(), "srgb", nullptr);

  if (rc)
    throw vips::VError();
  VImage result(thumb);
  // libvips without libexif cannot turn the thumbnail upright by itself.
  if (!image.get_typeof(VIPS_META_ORIENTATION)) {
    const int orientation = exifOrientation(image);
    if (orientation > 1) {
      result = result.copy();
      result.set(VIPS_META_ORIENTATION, orientation);
      result = result.autorot();
    }
  }
  return output(result, token);
}
ThumbnailCache::ThumbnailCache(QObject *parent)
    : QObject(parent), diskDirectory(ThumbnailDisk::root()) {
  const auto pages = sysconf(_SC_PHYS_PAGES), pageSize = sysconf(_SC_PAGESIZE);
  const quint64 memory = pages > 0 && pageSize > 0
                             ? quint64(pages) * quint64(pageSize)
                             : 8ULL * 1024 * 1024 * 1024;
  bool overridden = false;
  const int configured =
      qEnvironmentVariableIntValue("PRISM_THUMB_RAM_MIB", &overridden);
  cache.setMaxCost(overridden
                       ? std::clamp(configured, 128, 1024) * 1024
                       : int(std::clamp<quint64>(memory / 32 / 1024, 128 * 1024,
                                                 1024 * 1024)));
  workers = std::clamp(QThread::idealThreadCount() / 2, 1, 4);
  pool.setMaxThreadCount(workers);
  pool.setThreadPriority(QThread::LowPriority);
  pumpDelay.setSingleShot(true);
  changeDelay.setSingleShot(true);
  changeDelay.setInterval(16);
  connect(&pumpDelay, &QTimer::timeout, this, &ThumbnailCache::pump);
  connect(&changeDelay, &QTimer::timeout, this, &ThumbnailCache::changed);
}
ThumbnailCache::~ThumbnailCache() {
  clearRequests(true);
  for (auto watcher : findChildren<QFutureWatcherBase *>())
    watcher->disconnect(this);
  pool.waitForDone();
}
void ThumbnailCache::notify() {
  if (active && !changeDelay.isActive())
    changeDelay.start();
}
void ThumbnailCache::clearRequests(bool cancelRunning) {
  ++generation;
  pumpDelay.stop();
  queue.clear();
  queued.clear();
  for (auto it = running.begin(); it != running.end(); ++it) {
    queued.insert(it.key());
    if (cancelRunning)
      it.value()->store(true);
  }
}
void ThumbnailCache::discardRequests() { clearRequests(false); }
void ThumbnailCache::setActive(bool value) {
  if (active == value)
    return;
  active = value;
  if (!active) {
    clearRequests(true);
    changeDelay.stop();
  } else {
    notify();
    pump();
  }
}
void ThumbnailCache::setPaused(bool value) {
  if (paused == value)
    return;
  paused = value;
  if (value)
    clearRequests(true);
  else {
    notify();
    pump();
  }
}
QImage ThumbnailCache::request(const QFileInfo &file, int edge) {
  Q_UNUSED(edge);
  if (file.isDir())
    return {};
  return request(file.absoluteFilePath(), file.size(),
                 file.lastModified().toMSecsSinceEpoch());
}
QImage ThumbnailCache::request(const QString &absolutePath, qint64 size,
                               qint64 modifiedMs) {
  const QString key = absolutePath + "\n" + QString::number(size) + "/" +
                      QString::number(modifiedMs) + "/512-v4";
  auto hit = cache.object(key);
  const auto ready = hit ? hit->image : QImage{};
  if (hit && hit->complete)
    return ready;
  if (!active || paused || failed.contains(key) || queued.contains(key) ||
      queue.size() >= 192)
    return ready;
  queue.enqueue({key, absolutePath, 512, !hit, generation});
  queued.insert(key);
  if (!pumpDelay.isActive())
    pumpDelay.start(0);
  return ready;
}
void ThumbnailCache::pump() {
  while (active && !paused && running.size() < workers && !queue.isEmpty()) {
    const auto job = queue.dequeue();
    const auto disk = diskDirectory;
    auto token = std::make_shared<std::atomic_bool>(false);
    auto watcher = new QFutureWatcher<ThumbResult>(this);
    running.insert(job.key, token);
    connect(
        watcher, &QFutureWatcher<ThumbResult>::finished, this,
        [this, watcher, job, key = job.key] {
          auto result = watcher->result();
          if (result.decoded)
            ++sourceReads;
          running.remove(key);
          queued.remove(key);
          watcher->deleteLater();
          if (!result.cancelled && !result.image.isNull()) {
            if (result.needsFull)
              ++previews;
            cache.insert(
                key, new Entry{result.image, !result.needsFull},
                int((result.image.sizeInBytes() + key.size() * 2 + 512) / 1024 +
                    1));
            if (result.diskHit)
              ++hits;
          } else if (!result.cancelled && !result.needsFull)
            failed.insert(key, new bool(true));
          if (result.needsFull && !result.cancelled && active && !paused &&
              job.generation == generation) {
            auto detailed = job;
            detailed.previewOnly = false;
            queue.enqueue(detailed);
            queued.insert(key);
          }
          notify();
          pump();
        });
    ++starts;
    watcher->setFuture(QtConcurrent::run(&pool, [job, token, disk] {
      ThumbResult result;
      result.key = job.key;
      try {
        cancelled(token);
        if (job.previewOnly)
          result.image = ThumbnailDisk::read(disk, job.key);
        if (!result.image.isNull())
          result.diskHit = true;
        else if (job.previewOnly) {
          // Finish quick camera previews for visible entries before spending
          // worker slots decoding their high-resolution JPEG sources.
          result.needsFull = true;
          result.image = decodeThumbnail(job.path, job.edge, token, true);
          if (!result.image.isNull() &&
              std::max(result.image.width(), result.image.height()) >=
                  job.edge) {
            result.needsFull = false;
            ThumbnailDisk::write(disk, job.key, result.image);
          }
        } else {
          result.decoded = true;
          result.image = decodeThumbnail(job.path, job.edge, token);
          cancelled(token);
          ThumbnailDisk::write(disk, job.key, result.image);
        }
      } catch (const std::exception &e) {
        result.error = QString::fromUtf8(e.what()).left(200);
        vips_error_clear();
      }
      result.cancelled = token->load();
      return result;
    }));
  }
}
