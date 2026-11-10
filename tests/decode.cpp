#include "document.h"
#include "exif_fixture.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
static void require(bool ok, const char *msg) {
  if (!ok)
    throw std::runtime_error(msg);
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (VIPS_INIT(argv[0]))
    return 1;
  vips_cache_set_max(0);
  int result = 0;
  try {
    QTemporaryDir tmp;
    Document doc;
    auto token = std::make_shared<std::atomic_bool>(false);
    auto image =
        vips::VImage::black(1025, 513, vips::VImage::option()->set("bands", 3))
            .cast(VIPS_FORMAT_USHORT)
            .new_from_image({65535, 12000, 0})
            .copy(vips::VImage::option()->set("interpretation",
                                              VIPS_INTERPRETATION_RGB16));
    auto path = tmp.path() + "/test[bracket].tif";
    image.tiffsave(path.toUtf8().constData());
    auto info = doc.open(path, {}, token);
    require(info.size == QSize(1025, 513), "dimensions");
    auto tile = doc.tile(0, 0, 0);
    require(tile.image.size() == QSize(513, 513), "gutter dimensions");
    auto pixel = tile.image.pixelColor(10, 10);
    require(pixel.red() > 250 && pixel.green() > 40 && pixel.green() < 50 &&
                pixel.blue() == 0,
            "16-bit exact channels");
    auto edge = doc.tile(0, 2, 1);
    require(edge.image.size() == QSize(2, 2), "edge gutter");
    require(doc.tile(0, 0, 0).image == tile.image, "RAM cache");
    require(doc.tile(1, 0, 0).image.width() == 513, "downsample");
    bool rejected = false;
    try {
      doc.tile(0, 900, 0);
    } catch (const std::exception &) {
      rejected = true;
    }
    require(rejected, "invalid tile");
    auto jpeg = tmp.path() + "/test.jpg";
    image.colourspace(VIPS_INTERPRETATION_sRGB)
        .jpegsave(jpeg.toUtf8().constData());
    require(doc.open(jpeg, {}, token).size == QSize(1025, 513), "JPEG");
    require(!doc.tile(0, 0, 0).image.isNull(), "JPEG decode");
    auto small = tmp.path() + "/rgb8.tif";
    auto rgb8 =
        vips::VImage::black(4, 4, vips::VImage::option()->set("bands", 3))
            .new_from_image({210, 100, 30})
            .copy(vips::VImage::option()->set("interpretation",
                                              VIPS_INTERPRETATION_sRGB));
    rgb8.tiffsave(small.toUtf8().constData());
    doc.open(small, {}, token);
    require(doc.tile(0, 0, 0).image.pixelColor(1, 1) == QColor(210, 100, 30),
            "8-to-16-bit exact expansion");
    auto icc = tmp.path() + "/icc.tif";
    rgb8.icc_transform("srgb", vips::VImage::option()
                                   ->set("input_profile", "srgb")
                                   ->set("depth", 16))
        .tiffsave(icc.toUtf8().constData());
    auto iccInfo = doc.open(icc, {}, token);
    require(iccInfo.text.contains("profil osadzony"), "embedded ICC");
    require(std::abs(doc.tile(0, 0, 0).image.pixelColor(1, 1).red() - 210) <= 1,
            "ICC sRGB preservation");
    auto rotated = tmp.path() + "/rotated.tif";
    image.set("orientation", 6);
    image.tiffsave(rotated.toUtf8().constData());
    require(doc.open(rotated, {}, token).size == QSize(513, 1025),
            "EXIF orientation");
    auto broken = tmp.path() + "/broken.jpg";
    QFile bad(broken);
    require(bad.open(QIODevice::WriteOnly), "create corrupt file");
    bad.write("not a JPEG");
    bad.close();
    rejected = false;
    try {
      doc.open(broken, {}, token);
    } catch (const std::exception &) {
      rejected = true;
    }
    require(rejected, "corrupt image rejection");
    for (int i = 0; i < 20; ++i) {
      doc.open(small, {}, token);
      require(!doc.tile(0, 0, 0).image.isNull(), "repeated open");
    }
    token->store(true);
    rejected = false;
    try {
      doc.open(path, {}, token);
    } catch (const std::exception &) {
      rejected = true;
    }
    require(rejected, "cancel");
    token->store(false);
    rejected = false;
    try {
      doc.open(tmp.path() + "/missing", {}, token);
    } catch (const std::exception &) {
      rejected = true;
    }
    require(rejected, "missing file");

    // Truncated entropy and malformed EXIF must never abort the process.
    QFile jpegInput(jpeg);
    require(jpegInput.open(QIODevice::ReadOnly), "read JPEG fixture");
    QByteArray encoded = jpegInput.readAll();
    for (int kind = 0; kind < 3; ++kind) {
      QByteArray payload;
      if (kind == 0)
        payload = encoded.left(encoded.size() / 2);
      else if (kind == 1)
        payload = QByteArray();
      else {
        QByteArray exif = QByteArray::fromHex("45786966000049492a00ffffff7f");
        QByteArray app1 = QByteArray::fromHex("ffe1");
        int n = exif.size() + 2;
        app1.append(char(n >> 8));
        app1.append(char(n));
        app1.append(exif);
        payload = encoded.left(2) + app1 + encoded.mid(2);
      }
      QString corrupt = tmp.path() + QString("/unusual-%1.jpg").arg(kind);
      QFile output(corrupt);
      require(output.open(QIODevice::WriteOnly), "create unusual fixture");
      output.write(payload);
      output.close();
      bool rejectedCorrupt = false;
      try {
        auto info = doc.open(corrupt, {}, token);
        require(!info.preview.isNull(), "usable preview if tolerated EXIF");
        doc.tile(0, 0, 0);
      } catch (const std::exception &) {
        rejectedCorrupt = true;
        vips_error_clear();
      }
      if (kind < 2)
        require(rejectedCorrupt, "truncated/empty image must fail");
    }

    // A real EXIF IFD1 thumbnail should be useful immediately, but remain
    // provisional.
    auto thumbPath = tmp.path() + "/thumb.jpg";
    rgb8.resize(32.0).jpegsave(thumbPath.toUtf8().constData());
    QFile thumbnailFile(thumbPath);
    require(thumbnailFile.open(QIODevice::ReadOnly), "read thumbnail");
    auto thumbBytes = thumbnailFile.readAll();
    for (bool malformed : {false, true}) {
      QByteArray thumbnail = malformed ? QByteArray("bad") : thumbBytes;
      QString test =
          tmp.path() + (malformed ? "/bad-thumb.jpg" : "/embedded-thumb.jpg");
      QFile output(test);
      require(output.open(QIODevice::WriteOnly),
              "create EXIF thumbnail fixture");
      output.write(withExifThumbnail(encoded, thumbnail));
      output.close();
      int early = 0;
      auto result = doc.open(test, {}, token, [&](const Info &preview) {
        ++early;
        require(preview.size == QSize(1025, 513), "preview source dimensions");
        require(!preview.preview.isNull(), "EXIF pixels");
      });
      require(early == 0,
              "tiny EXIF preview must not be stretched into a blurry photo");
      require(result.size == QSize(1025, 513), "full image after thumbnail");
    }
    // Scaled JPEG levels must survive random-order tile requests and cache
    // revisits.
    auto bigPath = tmp.path() + "/pyramid.jpg";
    vips::VImage::xyz(4099, 2051)
        .extract_band(0)
        .remainder_const({256})
        .cast(VIPS_FORMAT_UCHAR)
        .bandjoin({rgb8.extract_band(1).embed(
                       0, 0, 4099, 2051,
                       vips::VImage::option()->set("extend", VIPS_EXTEND_COPY)),
                   rgb8.extract_band(2).embed(0, 0, 4099, 2051,
                                              vips::VImage::option()->set(
                                                  "extend", VIPS_EXTEND_COPY))})
        .copy(vips::VImage::option()->set("interpretation",
                                          VIPS_INTERPRETATION_sRGB))
        .jpegsave(bigPath.toUtf8().constData(),
                  vips::VImage::option()->set("interlace", true));
    for (int pass = 0; pass < 2; ++pass) {
      auto sharp = doc.open(bigPath, {}, token);
      require(std::max(sharp.preview.width(), sharp.preview.height()) >= 2048,
              "sharp fit preview");
      for (int level : {3, 1, 2, 0, 1, 3}) {
        require(!doc.tile(level, 0, 0).image.isNull(),
                "random reduced JPEG tile");
        if (level < 3)
          require(!doc.tile(level, 1, 0).image.isNull(),
                  "second reduced JPEG tile");
        else
          require(doc.tile(level, 0, 0).image.size() == QSize(512, 256),
                  "odd-size mip geometry anchored to original");
      }
      doc.open(small, {}, token);
    }
    for (const auto &unsafe :
         QStringList{small + QChar::Null + "suffix", QString(8200, 'x')}) {
      bool refused = false;
      try {
        doc.open(unsafe, {}, token);
      } catch (const std::exception &) {
        refused = true;
      }
      require(refused, "invalid path validation");
    }
    for (const auto &unsafe :
         QStringList{small + QChar::Null + "suffix", QString(8200, 'x')}) {
      bool refused = false;
      try {
        doc.open(unsafe, {}, token);
      } catch (const std::exception &) {
        refused = true;
      }
      require(refused, "invalid path validation");
    }
    auto renamed = tmp.path() + "/actual-jpeg.tiff";
    require(QFile::copy(jpeg, renamed), "renamed fixture");
    require(doc.open(renamed, {}, token).size == QSize(1025, 513),
            "loader sniffing renamed JPEG");
    for (int n = 0; n < 64; ++n) {
      auto weird = tmp.path() + QString("/fuzz-%1.%2")
                                    .arg(n)
                                    .arg(n % 3 == 0   ? "raw"
                                         : n % 3 == 1 ? "jpg"
                                                      : "tif");
      QFile f(weird);
      require(f.open(QIODevice::WriteOnly), "fuzz fixture");
      QByteArray payload(32 + n * 3, Qt::Uninitialized);
      for (int j = 0; j < payload.size(); ++j)
        payload[j] = char((j * 73 + n * 17) & 255);
      if (n % 4 == 0)
        payload.replace(0, 4, QByteArray::fromHex("49492a00"));
      f.write(payload);
      f.close();
      try {
        doc.open(weird, {}, token);
      } catch (const std::exception &) {
        vips_error_clear();
      }
    }
    doc.clear();
    std::cout << "PASS: 16-bit TIFF, JPEG, dimensions, gutter edges, cache, "
                 "zoom level, invalid request, cancellation, missing file\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    result = 1;
  }
  vips_shutdown();
  return result;
}
