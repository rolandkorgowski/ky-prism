#pragma once
#include <QCache>
#include <QImage>
#include <QSize>
#include <QString>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <vips/vips8>
struct Info {
  QSize size;
  QString text;
  QImage preview;
};
struct PreviewCache {
  QImage display, levelImage;
  int level = -1;
  Info info;
};
struct Tile {
  int level = 0, x = 0, y = 0;
  QImage image;
  QString key() const { return QString("%1/%2/%3").arg(level).arg(x).arg(y); }
};
class Document {
  vips::VImage original;
  QString identity;
  bool jpeg = false, alpha = false;
  quint64 materializedBytes = 0;
  std::shared_ptr<std::atomic_bool> cancellation;
  std::map<int, vips::VImage> levels;

  QCache<QString, QImage> cache{256 * 1024}; // KiB, worker-owned only
public:
  Info open(const QString &path, const QString &profile,
            const std::shared_ptr<std::atomic_bool> &cancel,
            const std::function<void(const Info &)> &earlyPreview = {},
            int previewEdge = 2048, QSize viewportPixels = {});
  Tile tile(int level, int x, int y);
  // The opened photograph (orientation applied, in the profile given to
  // open) as an 8-bit PNG, downscaled to at most `maxPixels` when larger.
  QByteArray encodePng(qint64 maxPixels, QSize *encoded = nullptr);
  void clear();
  static int previewBucket(int edge);
  static int sharedPreviewKiB();
  static void clearSharedPreviews();
};
