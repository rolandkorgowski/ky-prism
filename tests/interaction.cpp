#include "exif_fixture.h"
#include "glass_widgets.h"
#include "view.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <iostream>
static void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
static void waitFor(const std::function<bool()> &done) {
  QElapsedTimer timer;
  timer.start();
  while (!done() && timer.elapsed() < 10000) {
    QCoreApplication::processEvents();
    QThread::msleep(2);
  }
  check(done(), "timeout");
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  if (VIPS_INIT(argv[0]))
    return 2;
  vips_cache_set_max(0);
  int result = 0;
  try {
    QTemporaryDir tmp;
    auto image =
        vips::VImage::black(1025, 513, vips::VImage::option()->set("bands", 3))
            .new_from_image({230, 90, 20})
            .copy(vips::VImage::option()->set("interpretation",
                                              VIPS_INTERPRETATION_sRGB));
    QString first = tmp.path() + "/first.jpg",
            last = tmp.path() + "/ostatni [#] żółć.jpg",
            bad = tmp.path() + "/bad.jpg";
    image.jpegsave(first.toUtf8().constData());
    image.jpegsave(last.toUtf8().constData());
    QFile file(bad);
    check(file.open(QIODevice::WriteOnly), "create malformed fixture");
    file.write("invalid JPEG");
    file.close();
    {
      View queued;
      queued.open(first);
      QThread::msleep(250);
      check(queued.busy(), "finished worker was reported idle before delivery");
      queued.open(last);
      waitFor([&] {
        return queued.imageSize() == QSize(1025, 513) && !queued.busy();
      });
    }
    View view;
    QString metadata;
    int received = 0;
    QObject::connect(&view, &View::metadata, [&](const QString &s) {
      metadata = s;
      ++received;
    });
    for (int i = 0; i < 100; ++i)
      view.open(i % 2 ? first : bad);
    view.open(last);
    auto loader = static_cast<LoadingGlass *>(
        view.findChild<QWidget *>("loadingHourglass"));
    check(loader && loader->isLoading(), "loading indicator missing at open");
    waitFor([&] { return metadata.contains("ostatni") && !view.busy(); });
    check(!loader->isLoading() && !loader->animating(),
          "loading indicator retained after decode");
    check(received == 1, "stale result overwrote latest request");
    double zoom = 0;
    int frames = 0;
    QObject::connect(&view, &View::zoomChanged, [&](double z) {
      check(std::isfinite(z) && z > 0, "invalid zoom");
      zoom = z;
      ++frames;
    });
    for (int i = 0; i < 100; ++i) {
      view.scale(i % 2 ? 1.05 : 1 / 1.05, QPointF(200, 180));
      QCoreApplication::processEvents();
    }
    view.scale(1.5, QPointF(200, 180));
    waitFor([&] { return !view.busy(); });
    check(zoom > 0 && frames > 1, "animated zoom did not progress");
    view.scale(2.0, QPointF(200, 180));
    view.actual();
    waitFor([&] { return !view.busy(); });
    check(std::abs(zoom - 1.0) < 0.0001, "1:1 during animated zoom");
    view.resize(802, 602);
    view.actual();
    view.pan({0.37, 0.61});
    auto aligned =
        view.displayedImageRect().topLeft() * view.devicePixelRatioF();
    check(std::abs(aligned.x() - std::round(aligned.x())) < 1e-7 &&
              std::abs(aligned.y() - std::round(aligned.y())) < 1e-7,
          "1:1 placed pixels on half-pixel boundaries");
    view.open(bad);
    waitFor([&] { return metadata.contains("Obsługiwane") && !view.busy(); });
    check(!loader->isLoading() && !loader->animating(),
          "error left spinner active");
    view.open(last);
    waitFor([&] { return metadata.contains("ostatni") && !view.busy(); });

    // The user may select Original while an embedded preview is displayed.
    QString noisy = tmp.path() + "/noise.jpg",
            embedded = tmp.path() + "/embedded-view.jpg";
    vips::VImage::gaussnoise(4096, 2048,
                             vips::VImage::option()
                                 ->set("mean", 128.0)
                                 ->set("sigma", 50.0)
                                 ->set("seed", 5))
        .cast(VIPS_FORMAT_UCHAR)
        .colourspace(VIPS_INTERPRETATION_sRGB)
        .jpegsave(noisy.toUtf8().constData(),
                  vips::VImage::option()->set("Q", 100));
    QFile mainJpeg(noisy), thumbJpeg(first);
    check(mainJpeg.open(QIODevice::ReadOnly) &&
              thumbJpeg.open(QIODevice::ReadOnly),
          "read embedded preview fixture");
    QFile combined(embedded);
    check(combined.open(QIODevice::WriteOnly),
          "create embedded preview fixture");
    combined.write(withExifThumbnail(mainJpeg.readAll(), thumbJpeg.readAll()));
    combined.close();
    bool early = false;
    QObject::connect(&view, &View::status, [&](const QString &status) {
      if (status.startsWith("Podgląd EXIF")) {
        early = true;
        QMetaObject::invokeMethod(
            &view, [&] { view.actual(); }, Qt::QueuedConnection);
      }
    });
    view.open(embedded);
    waitFor([&] { return metadata.contains("embedded-view") && !view.busy(); });
    check(early, "embedded preview was not delivered");
    check(std::abs(zoom - 1.0) < 0.0001, "full preview reset Original scale");
    {
      View closing;
      closing.open(first);
      closing.stop();
    }
    std::cout << "PASS: 100 rapid switches, latest-result-only, 100 zoom "
                 "requests, idle stop, corrupt file recovery, "
                 "Unicode/brackets, close during load\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    result = 1;
  }
  vips_shutdown();
  return result;
}
