#include "document.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sys/resource.h>
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if ((argc != 2 && argc != 4) || VIPS_INIT(argv[0]))
    return 2;
  vips_concurrency_set(4);
  vips_cache_set_max(0);
  int result = 0;
  try {
    int areaWidth = argc >= 4 ? QString::fromLocal8Bit(argv[2]).toInt() : 1600,
        areaHeight = argc >= 4 ? QString::fromLocal8Bit(argv[3]).toInt() : 900;
    if (areaWidth < 1 || areaHeight < 1 || areaWidth > 32768 ||
        areaHeight > 32768)
      throw std::runtime_error("invalid viewport size");
    Document d;
    auto token = std::make_shared<std::atomic_bool>(false);
    QElapsedTimer t;
    t.start();
#ifdef MODERN
    auto info = d.open(QString::fromLocal8Bit(argv[1]), {}, token, {},
                       Document::previewBucket(std::max(areaWidth, areaHeight)),
                       {areaWidth, areaHeight});
#else
    auto info = d.open(QString::fromLocal8Bit(argv[1]), {}, token);
#endif
    std::cout << "preview_ms=" << t.elapsed() << std::endl;
    // Common 1600x900 physical-pixel photo area on a Full HD monitor.
    double scale = std::min(double(areaWidth) / info.size.width(),
                            double(areaHeight) / info.size.height());
    int level = std::clamp(int(std::floor(std::log2(1.0 / scale))), 0, 24);
    d.tile(level, 0, 0);
    std::cout << "fit_ready_ms=" << t.elapsed() << " level=" << level
              << std::endl;
    struct rusage r{};
    getrusage(RUSAGE_SELF, &r);
    std::cout << "peak_rss_kib=" << r.ru_maxrss << std::endl;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    result = 1;
  }
  vips_shutdown();
  return result;
}
