#include "raster_source.h"
#include "document.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>
#include <stdexcept>
static void check(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
}
static QByteArray digest(QFile &f) {
  check(f.seek(0), "hash seek");
  QCryptographicHash h(QCryptographicHash::Sha256);
  check(h.addData(&f), "hash read");
  return h.result();
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (VIPS_INIT(argv[0]))
    return 2;
  vips_cache_set_max(0);
  int result = 0;
  try {
    QTemporaryDir tmp;
    auto token = std::make_shared<std::atomic_bool>(false);
    for (bool wide : {false, true}) {
      auto name = tmp.path() + (wide ? "/big.tif" : "/classic.fff");
      auto image =
          vips::VImage::black(80, 40, vips::VImage::option()->set("bands", 3))
              .new_from_image({220, 80, 30})
              .copy(vips::VImage::option()->set("interpretation",
                                                VIPS_INTERPRETATION_sRGB));
      image
          .icc_transform("srgb", vips::VImage::option()
                                     ->set("input_profile", "srgb")
                                     ->set("depth", 16))
          .tiffsave(name.toUtf8().constData(),
                    vips::VImage::option()->set("bigtiff", wide));
      QFile f(name);
      check(f.open(QIODevice::ReadWrite), "fixture open");
      auto bytes = f.readAll();
      check(bytes.startsWith("II"), "native TIFF endian");
      auto ifd = wide ? qFromLittleEndian<quint64>(bytes.constData() + 8)
                      : qFromLittleEndian<quint32>(bytes.constData() + 4);
      quint64 count = wide
                          ? qFromLittleEndian<quint64>(bytes.constData() + ifd)
                          : qFromLittleEndian<quint16>(bytes.constData() + ifd);
      int countBytes = wide ? 8 : 2, entryBytes = wide ? 20 : 12,
          nextBytes = wide ? 8 : 4;
      auto newOffset = quint64(bytes.size());
      QByteArray table(countBytes + (count + 1) * entryBytes + nextBytes, 0);
      if (wide)
        qToLittleEndian<quint64>(count + 1, table.data());
      else
        qToLittleEndian<quint16>(quint16(count + 1), table.data());
      memcpy(table.data() + countBytes, bytes.constData() + ifd + countBytes,
             size_t(count * entryBytes));
      auto e = table.data() + countBytes + count * entryBytes;
      qToLittleEndian<quint16>(37724, e);
      qToLittleEndian<quint16>(7, e + 2);
      quint64 payloadOffset = newOffset + table.size(),
              payloadSize = 64 * 1024 * 1024;
      if (wide) {
        qToLittleEndian<quint64>(payloadSize, e + 4);
        qToLittleEndian<quint64>(payloadOffset, e + 12);
        qToLittleEndian<quint64>(newOffset, bytes.data() + 8);
      } else {
        qToLittleEndian<quint32>(payloadSize, e + 4);
        qToLittleEndian<quint32>(payloadOffset, e + 8);
        qToLittleEndian<quint32>(newOffset, bytes.data() + 4);
      }
      check(f.seek(0) && f.write(bytes) == bytes.size() &&
                f.write(table) == table.size() &&
                f.resize(qint64(payloadOffset + payloadSize)),
            "sparse layer fixture");
      auto before = digest(f);
      {
        RasterSource source(name, token);
        check(source.skippedLayers && source.renderedTiff,
              "layer filter and RGB detection");
      }
      Document doc;
      auto info = doc.open(name, {}, token);
      check(info.size == QSize(80, 40) &&
                info.text.contains("profil osadzony") &&
                info.text.contains("warstw Photoshop"),
            "TIFF/renamed FFF metadata preservation");
      auto p = doc.tile(0, 0, 0).image.pixelColor(0, 0);
      check(std::abs(p.red() - 220) <= 1 && std::abs(p.green() - 80) <= 1,
            "flattened pixel and ICC fidelity");
      check(digest(f) == before, "original TIFF was modified");
      // Invalid byte range in the ignored tag must be rejected, not trusted.
      f.seek(qint64(newOffset + countBytes + count * entryBytes +
                    (wide ? 12 : 8)));
      QByteArray invalid(wide ? 8 : 4, char(0xff));
      f.write(invalid);
      f.flush();
      bool refused = false;
      try {
        RasterSource bad(name, token);
      } catch (const std::exception &) {
        refused = true;
      }
      check(refused, "invalid layer range");
    }
    // A reduced RGB IFD inside an unsupported RAW must never masquerade as
    // the full-resolution original. Thumbnails may use it, the photo view may
    // not.
    auto reducedName = tmp.path() + "/reduced.fff";
    vips::VImage::black(80, 40, vips::VImage::option()->set("bands", 3))
        .copy(vips::VImage::option()->set("interpretation",
                                          VIPS_INTERPRETATION_sRGB))
        .tiffsave(reducedName.toUtf8().constData());
    {
      QFile f(reducedName);
      check(f.open(QIODevice::ReadWrite), "reduced fixture");
      auto bytes = f.readAll();
      if (bytes.size() < 8)
        throw std::runtime_error("short fixture header");
      auto offset = qFromLittleEndian<quint32>(bytes.data() + 4);
      auto n = qFromLittleEndian<quint16>(bytes.data() + offset);
      QByteArray entries;
      for (int i = 0; i < n; ++i) {
        auto e = bytes.data() + offset + 2 + i * 12;
        if (qFromLittleEndian<quint16>(e) != 254)
          entries.append(e, 12);
      }
      QByteArray entry(12, 0);
      qToLittleEndian<quint16>(254, entry.data());
      qToLittleEndian<quint16>(4, entry.data() + 2);
      qToLittleEndian<quint32>(1, entry.data() + 4);
      qToLittleEndian<quint32>(1, entry.data() + 8);
      QByteArray table(2, 0);
      qToLittleEndian<quint16>(quint16(entries.size() / 12 + 1), table.data());
      table += entry + entries + QByteArray(4, 0);
      auto newOffset = bytes.size();
      qToLittleEndian<quint32>(quint32(newOffset), bytes.data() + 4);
      check(f.seek(0) && f.write(bytes) == bytes.size() &&
                f.write(table) == table.size(),
            "reduced root IFD");
    }
    {
      RasterSource input(reducedName, token);
      check(!input.renderedTiff && input.rgbPreview,
            "reduced RAW preview confused with full TIFF");
      Document d;
      bool rejected = false;
      try {
        d.open(reducedName, {}, token);
      } catch (const std::exception &e) {
        rejected = QString::fromUtf8(e.what()).contains("pomniejszony podgląd");
      }
      check(rejected, "unsupported RAW displayed thumbnail as original");
    }
    // Big-endian classic and BigTIFF metadata, no decoder allocation needed.
    for (bool wide : {false, true}) {
      QByteArray bytes(wide ? 72 : 38, 0);
      bytes[0] = 'M';
      bytes[1] = 'M';
      qToBigEndian<quint16>(wide ? 43 : 42, bytes.data() + 2);
      quint64 start = wide ? 16 : 8;
      if (wide) {
        qToBigEndian<quint16>(8, bytes.data() + 4);
        qToBigEndian<quint64>(start, bytes.data() + 8);
        qToBigEndian<quint64>(2, bytes.data() + 16);
      } else {
        qToBigEndian<quint32>(start, bytes.data() + 4);
        qToBigEndian<quint16>(2, bytes.data() + 8);
      }
      int countBytes = wide ? 8 : 2, entryBytes = wide ? 20 : 12;
      for (int i = 0; i < 2; ++i) {
        auto e = bytes.data() + start + countBytes + i * entryBytes;
        qToBigEndian<quint16>(i ? 277 : 262, e);
        qToBigEndian<quint16>(3, e + 2);
        if (wide)
          qToBigEndian<quint64>(1, e + 4);
        else
          qToBigEndian<quint32>(1, e + 4);
        qToBigEndian<quint16>(i ? 3 : 2, e + (wide ? 12 : 8));
      }
      auto name = tmp.path() + QString("/be-%1.tif").arg(wide);
      QFile f(name);
      check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(),
            "big-endian fixture");
      f.close();
      RasterSource input(name);
      check(input.renderedTiff && !input.skippedLayers,
            "big-endian inline metadata");
    }
    std::cout
        << "PASS: classic/BigTIFF/endian, sparse 64 MiB private layers, "
           "ICC/pixel preservation, immutable originals, invalid offsets\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    result = 1;
  }
  Document::clearSharedPreviews();
  vips_shutdown();
  return result;
}
