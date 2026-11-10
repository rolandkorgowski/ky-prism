#include "document.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <unistd.h>
static qint64 rss() {
  QFile f("/proc/self/statm");
  if (!f.open(QIODevice::ReadOnly))
    throw std::runtime_error("RSS unavailable");
  auto fields = f.readAll().split(' ');
  return fields.at(1).toLongLong() * sysconf(_SC_PAGESIZE);
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  qputenv("PRISM_PREVIEW_CACHE_MIB", "128");
  if (VIPS_INIT(argv[0]))
    return 2;
  vips_concurrency_set(4);
  vips_cache_set_max(0);
  try {
    QTemporaryDir tmp;
    QStringList files;
    for (int i = 0; i < 20; ++i) {
      QString name = tmp.path() + QString("/%1.jpg").arg(i);
      vips::VImage::black(1537, 1025, vips::VImage::option()->set("bands", 3))
          .new_from_image(
              {double(20 + i * 7), double(40 + i * 3), double(200 - i * 5)})
          .copy(vips::VImage::option()->set("interpretation",
                                            VIPS_INTERPRETATION_sRGB))
          .jpegsave(name.toUtf8().constData());
      files.append(name);
    }
    Document doc;
    auto token = std::make_shared<std::atomic_bool>(false);
    QList<qint64> samples;
    QList<int> descriptors;
    for (int round = 0; round < 8; ++round) {
      for (const auto &name : files) {
        doc.open(name, {}, token);
        doc.tile(0, 0, 0);
        doc.tile(2, 0, 0);
      }
      samples.append(rss());
      descriptors.append(QDir("/proc/self/fd")
                             .entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
                             .size());
      if (Document::sharedPreviewKiB() > 128 * 1024)
        throw std::runtime_error("sharp cache exceeded budget");
      std::cout << "round=" << round
                << " resident_mib=" << samples.last() / 1048576.0 << std::endl;
    }
    qint64 low = samples.at(3), high = low;
    for (int i = 3; i < samples.size(); ++i) {
      low = std::min(low, samples.at(i));
      high = std::max(high, samples.at(i));
    }
    if (high - low > 32LL * 1024 * 1024)
      throw std::runtime_error("resident growth beyond 32 MiB after warmup");
    if (descriptors.last() > descriptors.at(3) + 2)
      throw std::runtime_error("file descriptor growth after warmup");
    doc.clear();
    Document::clearSharedPreviews();
    vips_cache_drop_all();
    std::cout << "after_clear_mib=" << rss() / 1048576.0
              << " plateau_span_mib=" << (high - low) / 1048576.0
              << " opens=160 preview_cache_cap_mib=128 fd_growth="
              << descriptors.last() - descriptors.at(3)
              << " cache_eviction=true\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    vips_shutdown();
    return 1;
  }
  vips_shutdown();
  return 0;
}
