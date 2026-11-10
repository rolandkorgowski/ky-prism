#include "document.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <iostream>
#include <sys/resource.h>
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if ((argc != 2 && argc != 3) || VIPS_INIT(argv[0]))
    return 2;
  vips_concurrency_set(4);
  vips_cache_set_max(0);
  int rc = 0;
  try {
    Document d;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    QElapsedTimer t;
    t.start();
    auto info =
        d.open(QString::fromLocal8Bit(argv[1]), {}, cancel, [&](const Info &) {
          std::cout << "first_preview_ms=" << t.elapsed() << std::endl;
        });
    std::cout << "open_ms=" << t.restart() << " size=" << info.size.width()
              << "x" << info.size.height() << std::endl;
    if (argc == 2)
      for (int level : {3, 2, 1, 0})
        for (int x = 0; x < 2; ++x) {
          auto tile = d.tile(level, x, 0);
          std::cout << "level=" << level << " tile=" << x
                    << " ms=" << t.restart() << std::endl;
        }
    t.restart();
    d.open(QString::fromLocal8Bit(argv[1]), {}, cancel, [&](const Info &) {
      std::cout << "cached_preview_ms=" << t.elapsed() << std::endl;
    });
    std::cout << "reopen_ms=" << t.restart() << std::endl;
    if (argc == 2) {
      d.tile(3, 0, 0);
      std::cout << "reopen_tile_ms=" << t.elapsed() << std::endl;
    }
    struct rusage usage{};
    if (!getrusage(RUSAGE_SELF, &usage))
      std::cout << "peak_rss_kib=" << usage.ru_maxrss << std::endl;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    rc = 1;
  }
  vips_shutdown();
  return rc;
}
