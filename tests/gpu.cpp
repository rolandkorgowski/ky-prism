#include "view.h"
#include <QApplication>
#include <QFile>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
int main(int argc, char **argv) {
  if (qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen" ||
      (qEnvironmentVariableIsEmpty("DISPLAY") &&
       qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))) {
    std::cerr << "Test wymaga działającego pulpitu OpenGL.\n";
    return 77;
  }
  QSurfaceFormat f;
  f.setVersion(3, 3);
  f.setProfile(QSurfaceFormat::CoreProfile);
  QSurfaceFormat::setDefaultFormat(f);
  if (qEnvironmentVariable("QT_QPA_PLATFORMTHEME").startsWith("gtk"))
    qunsetenv("QT_QPA_PLATFORMTHEME");
  QCoreApplication::setApplicationName("ky-prism");
  QGuiApplication::setDesktopFileName("ky-prism");
  QApplication app(argc, argv);
  if (VIPS_INIT(argv[0]))
    return 2;
  vips_concurrency_set(4);
  vips_cache_set_max(0);
  int rc = 0;
  try {
    QTemporaryDir tmp;
    auto path = tmp.path() + "/gpu.tif";
    vips::VImage::black(256, 256, vips::VImage::option()->set("bands", 3))
        .new_from_image({210, 100, 30})
        .copy(vips::VImage::option()->set("interpretation",
                                          VIPS_INTERPRETATION_sRGB))
        .tiffsave(path.toUtf8().constData());
    View view;
    view.resize(700, 500);
    view.show();
    view.open(path);
    auto wait = [&] {
      QElapsedTimer timer;
      timer.start();
      while ((!view.hardwareReady() || view.busy() ||
              view.imageSize().isEmpty()) &&
             timer.elapsed() < 15000) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
      }
      if (!view.hardwareReady() || view.busy())
        throw std::runtime_error("GPU/context load timeout");
    };
    wait();
    for (int i = 0; i < 12; ++i) {
      view.resize(600 + i * 8, 400 + i * 5);
      view.actual();
      wait();
      auto frame = view.framebuffer();
      if (frame.isNull())
        throw std::runtime_error("Missing framebuffer");
      auto color = frame.pixelColor(frame.width() / 2, frame.height() / 2);
      if (std::abs(color.red() - 210) > 1 ||
          std::abs(color.green() - 100) > 1 || std::abs(color.blue() - 30) > 1)
        throw std::runtime_error("GPU color/alpha mismatch");
      view.open(path);
      wait();
    }
    auto checker = tmp.path() + "/native-checker.tif";
    auto stripe =
        vips::VImage::xyz(257, 129).extract_band(0).remainder_const({2}).cast(
            VIPS_FORMAT_UCHAR) *
        255;
    stripe.bandjoin({stripe, stripe})
        .cast(VIPS_FORMAT_UCHAR)
        .copy(vips::VImage::option()->set("interpretation",
                                          VIPS_INTERPRETATION_sRGB))
        .tiffsave(checker.toUtf8().constData());
    view.resize(700, 502);
    view.open(checker);
    wait();
    view.actual();
    wait();
    auto frame = view.framebuffer();
    auto origin =
        view.displayedImageRect().topLeft() * view.devicePixelRatioF();
    int x = int(std::round(origin.x())) + 64,
        y = int(std::round(origin.y())) + 32;
    if (x < 0 || y < 0 || x + 1 >= frame.width() || y >= frame.height() ||
        std::abs(frame.pixelColor(x, y).red() -
                 frame.pixelColor(x + 1, y).red()) < 250)
      throw std::runtime_error(
          "1:1 pixel boundaries blurred by subpixel placement");
    std::cout << "PASS Qt OpenGLWidget: " << view.gpu.toStdString()
              << "; 12 resize/reload, RGBA16, exact color, 1:1 checkerboard "
                 "contrast, idle\n";
    view.release();
    view.close();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    rc = 1;
  }
  vips_shutdown();
  return rc;
}
