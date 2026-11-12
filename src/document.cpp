#include "document.h"
#include "exif_orientation.h"
#include "raster_source.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStorageInfo>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <libraw/libraw.h>
#include <optional>
#include <stdexcept>
#include <unistd.h>
using vips::VImage;
namespace {
struct SharedPreviews {
  QMutex mutex;
  QCache<QString, PreviewCache> cache;
  SharedPreviews() {
    auto pages = sysconf(_SC_PHYS_PAGES), pageSize = sysconf(_SC_PAGESIZE);
    quint64 memory = pages > 0 && pageSize > 0
                         ? quint64(pages) * quint64(pageSize)
                         : 8ULL * 1024 * 1024 * 1024;
    auto budget =
        int(std::clamp<quint64>(memory / 32 / 1024, 128 * 1024, 1024 * 1024));
    bool ok = false;
    int requested =
        qEnvironmentVariableIntValue("PRISM_PREVIEW_CACHE_MIB", &ok);
    if (ok && requested >= 64 && requested <= 1024)
      budget = std::min(budget, requested * 1024);
    cache.setMaxCost(budget);
  }
};
SharedPreviews &shared() {
  static SharedPreviews value;
  return value;
}
std::optional<PreviewCache> cachedPreview(const QString &key) {
  auto &s = shared();
  QMutexLocker guard(&s.mutex);
  if (auto entry = s.cache.object(key))
    return *entry;
  return {};
}
void putPreview(const QString &key, PreviewCache value, int cost) {
  auto &s = shared();
  QMutexLocker guard(&s.mutex);
  s.cache.insert(key, new PreviewCache(std::move(value)), cost);
}
} // namespace
int Document::previewBucket(int edge) {
  return edge <= 1024 ? 1024 : edge <= 2048 ? 2048 : 4096;
}
int Document::sharedPreviewKiB() {
  auto &s = shared();
  QMutexLocker guard(&s.mutex);
  return s.cache.totalCost();
}
void Document::clearSharedPreviews() {
  auto &s = shared();
  QMutexLocker guard(&s.mutex);
  s.cache.clear();
}
static void fail(const QString &s) {
  throw std::runtime_error(s.toStdString());
}
static VImage displayImage(VImage source, const QString &profile,
                           QString &note) {
  if (source.format() == VIPS_FORMAT_FLOAT ||
      source.format() == VIPS_FORMAT_DOUBLE ||
      source.interpretation() == VIPS_INTERPRETATION_scRGB)
    fail("Obrazy HDR/zmiennoprzecinkowe wymagają osobnego toru HDR; nie są "
         "konwertowane po cichu.");
  const bool embedded = source.get_typeof(VIPS_META_ICC_NAME) != 0;
  // libvips without lcms2 (e.g. the OpenBSD package) has no icc_transform:
  // photos are then shown as sRGB and the panel says so.
  const bool icc = vips_icc_present();
  QString target = profile.isEmpty() ? "srgb" : profile;
  if (embedded && icc) {
    source = source.icc_transform(target.toUtf8().constData(),
                                  VImage::option()
                                      ->set("embedded", true)
                                      ->set("depth", 16)
                                      ->set("intent", VIPS_INTENT_RELATIVE));
    note.prepend("ICC: profil osadzony → " + target +
                 " (relative colorimetric, 16 bit).\n");
  } else {
    if (source.interpretation() == VIPS_INTERPRETATION_CMYK)
      fail(icc ? "CMYK bez profilu ICC: nie można zagwarantować poprawnych kolorów."
               : "CMYK wymaga zarządzania kolorem ICC, którego libvips na tym "
                 "systemie nie ma (bez lcms2).");
    if (source.format() == VIPS_FORMAT_UCHAR) {
      source = (source.colourspace(VIPS_INTERPRETATION_sRGB)
                    .cast(VIPS_FORMAT_USHORT) *
                257.0)
                   .cast(VIPS_FORMAT_USHORT)
                   .copy(VImage::option()->set("interpretation",
                                               VIPS_INTERPRETATION_RGB16));
    } else
      source = source.colourspace(VIPS_INTERPRETATION_RGB16);
    if (!profile.isEmpty() && icc)
      source = source.icc_transform(profile.toUtf8().constData(),
                                    VImage::option()
                                        ->set("input_profile", "srgb")
                                        ->set("depth", 16)
                                        ->set("intent", VIPS_INTENT_RELATIVE));
    note.prepend(embedded ? QString("ICC: libvips na tym systemie nie obsługuje "
                                    "profili (bez lcms2) — profil osadzony "
                                    "pominięto, przyjęto sRGB.\n")
                          : QString("Brak osadzonego ICC: przyjęto sRGB.\n"));
  }
  source = source.cast(VIPS_FORMAT_USHORT);
  if (source.bands() == 3)
    source = source.bandjoin(65535.0);
  if (source.bands() != 4)
    fail("Nieobsługiwana liczba kanałów po konwersji ICC.");
  return source;
}
// Stop a libvips evaluation of `image` as soon as `token` is set.
static void killOnCancel(VImage &image,
                         const std::shared_ptr<std::atomic_bool> &token) {
  if (token) {
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
  }
}
static QImage pixels(VImage image,
                     const std::shared_ptr<std::atomic_bool> &token = {}) {
  killOnCancel(image, token);
  size_t bytes = 0;
  void *data = image.write_to_memory(&bytes);
  // Transfer the allocation to Qt instead of copying every decoded image again.
  return QImage(
      static_cast<uchar *>(data), image.width(), image.height(),
      image.width() * 8, QImage::Format_RGBA64,
      [](void *buffer) { g_free(buffer); }, data);
}
static VImage imageFromPixels(const QImage &pixels) {
  auto owner = std::make_unique<QImage>(pixels);
  auto image = vips_image_new_from_memory(
      owner->constBits(), size_t(owner->sizeInBytes()), owner->width(),
      owner->height(), 4, VIPS_FORMAT_USHORT);
  if (!image)
    throw vips::VError();
  g_object_set_data_full(
      G_OBJECT(image), "prism-pixel-owner", owner.release(),
      +[](void *p) { delete static_cast<QImage *>(p); });
  return VImage(image).copy(
      VImage::option()->set("interpretation", VIPS_INTERPRETATION_RGB16));
}
QByteArray Document::encodePng(qint64 maxPixels, QSize *encoded) {
  if (!original.get_image())
    fail("Brak otwartego zdjęcia.");
  VImage image = original;
  const double count = double(image.width()) * double(image.height());
  if (maxPixels > 0 && count > double(maxPixels))
    image = image.resize(std::sqrt(double(maxPixels) / count),
                         VImage::option()->set("kernel", VIPS_KERNEL_LANCZOS3));
  // The canvas works in 16 bit; the clipboard's PNG is the common 8-bit sRGB.
  image = (image / 257.0).rint().cast(VIPS_FORMAT_UCHAR);
  if (!alpha)
    image = image.extract_band(0, VImage::option()->set("n", 3));
  image = image.copy(
      VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB));
  killOnCancel(image, cancellation);
  void *data = nullptr;
  size_t length = 0;
  image.write_to_buffer(".png", &data, &length,
                        VImage::option()
                            ->set("compression", 1)
                            ->set("keep", VIPS_FOREIGN_KEEP_NONE));
  if (cancellation && cancellation->load()) {
    g_free(data);
    fail("Anulowano");
  }
  QByteArray result(static_cast<const char *>(data), qsizetype(length));
  g_free(data);
  if (encoded)
    *encoded = {image.width(), image.height()};
  return result;
}
void Document::clear() {

  cache.clear();
  levels.clear();
  materializedBytes = 0;
  original = VImage();
}
Info Document::open(const QString &path, const QString &profile,
                    const std::shared_ptr<std::atomic_bool> &cancel,
                    const std::function<void(const Info &)> &earlyPreview,
                    int previewEdge, QSize viewportPixels) {
  previewEdge = previewBucket(previewEdge);
  if (path.isEmpty() || path.size() > 8192 || path.contains(QChar::Null) ||
      QFile::decodeName(QFile::encodeName(path)) != path)
    fail("Nieprawidłowa ścieżka pliku.");
  if (!cancel || cancel->load())
    fail("Anulowano");
  if (profile.size() > 8192 || profile.contains(QChar::Null))
    fail("Nieprawidłowa ścieżka profilu ICC.");
  cancellation = cancel;
  QFileInfo fi(path);
  jpeg = fi.suffix().compare("jpg", Qt::CaseInsensitive) == 0 ||
         fi.suffix().compare("jpeg", Qt::CaseInsensitive) == 0;
  identity = QString::fromLatin1(
      QCryptographicHash::hash(
          (path + QString::number(fi.size()) +
           QString::number(fi.lastModified().toMSecsSinceEpoch()) + profile)
              .toUtf8(),
          QCryptographicHash::Sha256)
          .toHex());
  if (!fi.isFile() || !fi.isReadable())
    fail("Nie można odczytać pliku.");
  const auto previewKey = identity + "/" + QString::number(previewEdge);
  auto neededEdge = [&](QSize source) {
    if (viewportPixels.isEmpty() || source.isEmpty())
      return previewEdge;
    double scale =
        std::min({1.0, double(viewportPixels.width()) / source.width(),
                  double(viewportPixels.height()) / source.height()});
    return std::max(
        1, std::min(previewEdge,
                    int(std::ceil(std::max(source.width(), source.height()) *
                                  scale))));
  };
  auto sufficient = [&](const PreviewCache &hit) {
    return std::max(hit.display.width(), hit.display.height()) >=
           std::min(neededEdge(hit.info.size),
                    std::max(hit.info.size.width(), hit.info.size.height()));
  };
  if (earlyPreview)
    if (auto hit = cachedPreview(previewKey);
        hit && !hit->info.size.isEmpty() && sufficient(*hit) &&
        !cancel->load()) {
      auto info = hit->info;
      info.text = "Podgląd z RAM • trwa przygotowanie pełnej rozdzielczości";
      earlyPreview(info);
    }
  // Deliver the sharp cached image before releasing a large previous TIFF/RAW.
  levels.clear();
  materializedBytes = 0;
  original = VImage();
  const QSet<QString> rawExt = {"raw", "cr2", "cr3", "nef", "nrw", "arw",
                                "dng", "raf", "rw2", "orf", "pef", "srw",
                                "3fr", "fff", "iiq", "kdc", "mos"};
  bool raw = rawExt.contains(fi.suffix().toLower());
  std::unique_ptr<LibRaw> rawDecoder;
  std::unique_ptr<RasterSource> raster;
  if (raw) {
    rawDecoder = std::make_unique<LibRaw>();
    []<typename D>(D &d) {
      if constexpr (requires { d.imgdata.rawparams.max_raw_memory_mb; })
        d.imgdata.rawparams.max_raw_memory_mb = 2048;
    }(*rawDecoder);
    rawDecoder->set_progress_handler(
        [](void *p, enum LibRaw_progress, int, int) {
          return static_cast<std::atomic_bool *>(p)->load() ? 1 : 0;
        },
        cancel.get());
    auto rc = rawDecoder->open_file(QFile::encodeName(path).constData());
    if (rc == LIBRAW_FILE_UNSUPPORTED) {
      raster = std::make_unique<RasterSource>(path, cancel);
      if (raster->renderedTiff) {
        raw = false;
        rawDecoder.reset();
      } else if (raster->rgbPreview)
        fail("LibRaw nie obsługuje tego wariantu RAW. Plik zawiera "
             "pomniejszony podgląd, który nie jest oryginałem. Wyeksportuj "
             "pełne zdjęcie do TIFF, np. w Hasselblad Phocus.");
      else
        fail(QString::fromUtf8(libraw_strerror(rc)));
    } else if (rc != LIBRAW_SUCCESS)
      fail(QString::fromUtf8(libraw_strerror(rc)));
  }
  VImage source;
  QString note;
  int orientation = 1;
  if (raw) {
    auto &decoder = *rawDecoder;
    []<typename Decoder>(Decoder &d) {
      if constexpr (requires { d.imgdata.rawparams.max_raw_memory_mb; })
        d.imgdata.rawparams.max_raw_memory_mb = 2048;
    }(decoder);
    decoder.set_progress_handler(
        [](void *p, enum LibRaw_progress, int, int) {
          return static_cast<std::atomic_bool *>(p)->load() ? 1 : 0;
        },
        cancel.get());
    auto check = [](int rc) {
      if (rc != LIBRAW_SUCCESS)
        fail(QString::fromUtf8(libraw_strerror(rc)));
    };

    if (quint64(decoder.imgdata.sizes.raw_width) *
            decoder.imgdata.sizes.raw_height >
        160000000ULL)
      fail("RAW przekracza limit 160 megapikseli.");
    decoder.imgdata.params.output_bps = 16;
    decoder.imgdata.params.output_color = 1; // sRGB primaries
    decoder.imgdata.params.gamm[0] = 1.0 / 2.4;
    decoder.imgdata.params.gamm[1] = 12.92;
    decoder.imgdata.params.use_camera_wb = 1;
    decoder.imgdata.params.no_auto_bright = 1;
    decoder.imgdata.params.user_qual = 3;
    check(decoder.unpack());
    check(decoder.dcraw_process());
    int rc = 0;
    std::unique_ptr<libraw_processed_image_t,
                    decltype(&LibRaw::dcraw_clear_mem)>
        mem(decoder.dcraw_make_mem_image(&rc), &LibRaw::dcraw_clear_mem);
    check(rc);
    if (!mem || mem->type != LIBRAW_IMAGE_BITMAP || mem->bits != 16 ||
        mem->colors != 3)
      fail("Nieobsługiwany wynik dekodera RAW.");
    VipsImage *image =
        vips_image_new_from_memory_copy(mem->data, mem->data_size, mem->width,
                                        mem->height, 3, VIPS_FORMAT_USHORT);
    if (!image)
      throw vips::VError();
    source = VImage(image).copy(
        VImage::option()->set("interpretation", VIPS_INTERPRETATION_RGB16));
    note = "RAW: wywołanie 16-bit sRGB, balans aparatu, AHD; bez "
           "automatycznego rozjaśniania.\n";
    note += QString("Aparat: %1 %2\nISO: %3  Czas: %4 s  f/%5  %6 mm\n")
                .arg(decoder.imgdata.idata.make, decoder.imgdata.idata.model)
                .arg(decoder.imgdata.other.iso_speed)
                .arg(decoder.imgdata.other.shutter)
                .arg(decoder.imgdata.other.aperture)
                .arg(decoder.imgdata.other.focal_len);
  } else {
    if (!raster)
      raster = std::make_unique<RasterSource>(path, cancel);
    const char *loader = vips_foreign_find_load_source(raster->get());
    if (!loader || (!strstr(loader, "LoadJpeg") &&
                    !strstr(loader, "LoadTiff") && !strstr(loader, "LoadPng")))
      fail("Obsługiwane: JPEG, TIFF, PNG i RAW z LibRaw.");
    jpeg = strstr(loader, "LoadJpeg") != nullptr;
    VipsImage *p = nullptr;
    // Calling loader directly avoids interpreting square brackets in filenames
    // as options.
    if (vips_call(loader, raster->get(), &p, "access", VIPS_ACCESS_RANDOM,
                  "fail_on", VIPS_FAIL_ON_ERROR, nullptr))
      throw vips::VError();
    source = VImage(p);
    if (raster->skippedLayers)
      note += "TIFF: pominięto prywatne dane warstw Photoshop; obraz złożony i "
              "profil ICC zachowane.\n";
    const quint64 pixels = quint64(source.width()) * source.height();
    if (pixels > 2500000000ULL || source.width() > 1000000 ||
        source.height() > 1000000 || source.bands() > 5)
      fail("Obraz przekracza limit geometrii (2,5 Gpx / 5 kanałów).");
    QStorageInfo storage(QDir::tempPath());
    quint64 scratch =
        pixels * quint64(source.bands()) * vips_format_sizeof(source.format());
    if (storage.isValid() &&
        quint64(storage.bytesAvailable()) < scratch + 512ULL * 1024 * 1024)
      fail("Za mało miejsca na dysku tymczasowym do bezpiecznego dekodowania.");
    char **fields = vips_image_get_fields(source.get_image());
    for (char **f = fields; f && *f; ++f) {
      if (note.size() > 24000)
        break;
      if (!strncmp(*f, "exif-", 5) || !strcmp(*f, "n-pages")) {
        char *value = nullptr;
        if (!vips_image_get_as_string(source.get_image(), *f, &value)) {
          note += QString::fromUtf8(*f) + ": " +
                  QString::fromUtf8(value).left(512) + "\n";
          g_free(value);
        }
      }
    }
    g_strfreev(fields);
    source = withExifOrientation(source);
    if (source.get_typeof(VIPS_META_ORIENTATION))
      orientation = source.get_int(VIPS_META_ORIENTATION);
    source = source.autorot();
  }
  if (cancel->load())
    fail("Anulowano");
  alpha = source.has_alpha();
  source = displayImage(source, profile, note);
  original = source;
  levels.emplace(0, original);
  note.prepend(QString("%1\n%2 × %3 px • %4 MiB\n")
                   .arg(fi.fileName())
                   .arg(original.width())
                   .arg(original.height())
                   .arg(fi.size() / 1048576.0, 0, 'f', 1));
  note +=
      "\nTIFF wielostronicowy: wyświetlana jest pierwsza strona.\nWyjście SDR; "
      "bez kalibracji monitora nie gwarantuje zgodności fizycznych barw.\n";
  if (auto cached = cachedPreview(previewKey); cached && sufficient(*cached)) {
    if (cached->level >= 0) {
      levels[cached->level] = imageFromPixels(cached->levelImage);
      materializedBytes = quint64(cached->levelImage.sizeInBytes());
    }
    return {{original.width(), original.height()}, note, cached->display};
  }
  previewEdge = neededEdge({original.width(), original.height()});
  // Embedded EXIF preview is explicitly provisional. No native GTK/Glycin path.
  // Decode only a bounded JPEG blob; malformed thumbnails fall back to the
  // source.
  if (jpeg && earlyPreview && original.get_typeof("jpeg-thumbnail-data") &&
      !cancel->load()) {
    try {
      const void *data = nullptr;
      size_t length = 0;
      if (!vips_image_get_blob(original.get_image(), "jpeg-thumbnail-data",
                               &data, &length) &&
          length > 0 && length <= 2 * 1024 * 1024) {
        VipsImage *image = nullptr;
        if (vips_jpegload_buffer(const_cast<void *>(data), length, &image,
                                 "access", VIPS_ACCESS_SEQUENTIAL, "fail_on",
                                 VIPS_FAIL_ON_ERROR, nullptr))
          throw vips::VError();
        VImage thumb(image);
        if (thumb.width() <= 4096 && thumb.height() <= 4096 &&
            std::max(thumb.width(), thumb.height()) >=
                std::min(previewEdge,
                         std::max(original.width(), original.height()))) {
          thumb = thumb.copy();
          thumb.set(VIPS_META_ORIENTATION, orientation);
          thumb = thumb.autorot();
          QString ignored;
          thumb = displayImage(thumb, profile, ignored);
          auto preview = pixels(thumb, cancel);
          if (!cancel->load())
            earlyPreview({{original.width(), original.height()},
                          "Podgląd EXIF • trwa odczyt pełnego zdjęcia",
                          preview});
        }
      }
    } catch (const vips::VError &) {
      vips_error_clear();
    } catch (const std::runtime_error &) {
      vips_error_clear();
    }
  }
  VImage preview;
  PreviewCache entry;

  if (!raw && jpeg) {
    int shrink = 1;
    while (shrink < 8 &&
           std::max(original.width(), original.height()) / (shrink * 2) >=
               previewEdge)
      shrink *= 2;
    preview = withExifOrientation(
                  VImage::jpegload(QFile::encodeName(path).constData(),
                                   VImage::option()
                                       ->set("shrink", shrink)
                                       ->set("access", VIPS_ACCESS_SEQUENTIAL)
                                       ->set("fail_on", VIPS_FAIL_ON_ERROR)))
                  .autorot();
    QString ignored;
    preview = displayImage(preview, profile, ignored);
    int previewLevel = int(std::log2(shrink));
    while (std::max(original.width(), original.height()) /
               std::ldexp(1.0, previewLevel) >
           previewEdge * 2.0)
      ++previewLevel;
    shrink = int(std::ldexp(1.0, previewLevel));
    int w = std::max(1, int(std::round(original.width() / double(shrink))));
    int h = std::max(1, int(std::round(original.height() / double(shrink))));
    if (preview.width() != w || preview.height() != h)
      preview = preview.resize(
          double(w) / preview.width(),
          VImage::option()->set("vscale", double(h) / preview.height()));
    if (quint64(preview.width()) * preview.height() * 8 <=
        256ULL * 1024 * 1024) {
      entry.levelImage = pixels(preview, cancel);
      entry.level = previewLevel;
      preview = imageFromPixels(entry.levelImage);
      levels[previewLevel] = preview;
      materializedBytes += quint64(entry.levelImage.sizeInBytes());
    }
  } else {
    int level = 0;
    while (std::max(original.width(), original.height()) /
               std::ldexp(1.0, level + 1) >=
           previewEdge)
      ++level;
    preview = level ? original.resize(
                          std::ldexp(1.0, -level),
                          VImage::option()->set("kernel", VIPS_KERNEL_LANCZOS3))
                    : original;
    entry.levelImage = pixels(preview, cancel);
    entry.level = level;
    preview = imageFromPixels(entry.levelImage);
    levels[level] = preview;
    materializedBytes += quint64(entry.levelImage.sizeInBytes());
  }
  double ratio =
      entry.level >= 0
          ? 1.0
          : std::min(1.0, double(previewEdge) /
                              std::max(preview.width(), preview.height()));
  if (ratio < 1.0)
    preview = preview.resize(
        ratio, VImage::option()->set("kernel", VIPS_KERNEL_LANCZOS3));
  if (cancel->load())
    fail("Anulowano");
  auto result = entry.level >= 0 ? entry.levelImage : pixels(preview, cancel);
  entry.display = result;
  entry.info = {{original.width(), original.height()}, note, result};
  // Display and reduced pyramid share the same allocation through QImage.
  quint64 ownedBytes =
      quint64(result.sizeInBytes()) + quint64(entry.info.text.size()) * 4 +
      quint64(previewKey.size()) * 2 + sizeof(PreviewCache) + 512;
  int cost = int(ownedBytes / 1024 + 1);
  putPreview(previewKey, std::move(entry), cost);
  return {{original.width(), original.height()}, note, result};
}
Tile Document::tile(int level, int x, int y) {
  if (!original.get_image() || level < 0 || level > 24 || x < 0 || y < 0)
    fail("Nieprawidłowy kafelek.");
  Tile out{level, x, y, {}};
  if (auto cached = cache.object(identity + out.key())) {
    out.image = *cached;
    return out;
  }
  if (!levels.contains(level)) {
    VImage image;
    auto nearest = levels.upper_bound(level);
    --nearest;
    // Reuse prepared dyadic levels for fit/overview. For finer detail use
    // the original loader once: decoding the same JPEG separately at 1/4,
    // 1/2 and 1/1 was slower on the real corpus (entropy decode dominates).
    int targetWidth =
        std::max(1, int(std::round(original.width() / std::ldexp(1.0, level))));
    int targetHeight = std::max(
        1, int(std::round(original.height() / std::ldexp(1.0, level))));
    // Anchor every mip size to the original: rounding a rounded parent can
    // otherwise grow odd-sized photographs by a pixel and misalign tiles.
    image = nearest->second.resize(
        double(targetWidth) / nearest->second.width(),
        VImage::option()
            ->set("vscale", double(targetHeight) / nearest->second.height())
            ->set("kernel", VIPS_KERNEL_LANCZOS3));
    quint64 bytes = quint64(image.width()) * image.height() * 8;
    if (level >= 3 && bytes <= 32ULL * 1024 * 1024 &&
        materializedBytes + bytes <= 384ULL * 1024 * 1024) {
      image = image.copy_memory();
      materializedBytes += bytes;
    }
    levels.emplace(level, image);
  }
  auto image = levels.at(level);
  int left = x * 512, top = y * 512;
  int w = std::min(512, image.width() - left),
      h = std::min(512, image.height() - top);
  if (w <= 0 || h <= 0)
    fail("Kafelek poza obrazem.");
  // 1-pixel gutters prevent seams under GPU bilinear filtering.
  int l = std::max(0, left - 1), t = std::max(0, top - 1);
  int r = std::min(image.width(), left + w + 1),
      b = std::min(image.height(), top + h + 1);
  auto crop = image.crop(l, t, r - l, b - t);
  vips_image_set_progress(crop.get_image(), true);
  g_signal_connect(crop.get_image(), "eval",
                   G_CALLBACK(+[](VipsImage *im, VipsProgress *, void *data) {
                     if (static_cast<std::atomic_bool *>(data)->load())
                       vips_image_set_kill(im, true);
                   }),
                   cancellation.get());
  size_t bytes = 0;
  std::unique_ptr<void, decltype(&g_free)> data(crop.write_to_memory(&bytes),
                                                &g_free);
  QImage q(static_cast<const uchar *>(data.get()), r - l, b - t, (r - l) * 8,
           QImage::Format_RGBA64);
  out.image = q.copy();
  cache.insert(identity + out.key(), new QImage(out.image),
               int(out.image.sizeInBytes() / 1024 + 1));
  return out;
}
