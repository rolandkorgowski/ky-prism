#pragma once
// Toolkit-independent thumbnail decoder and cache (QtCore/QtGui only).
#include <QCache>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <memory>
struct ThumbResult {
  QString key;
  QImage image;
  QString error;
  bool cancelled = false, diskHit = false, decoded = false, needsFull = false;
};
QImage decodeThumbnail(const QString &, int,
                       const std::shared_ptr<std::atomic_bool> &,
                       bool embeddedOnly = false);
class ThumbnailCache final : public QObject {
  Q_OBJECT
  struct Job {
    QString key, path;
    int edge = 128;
    bool previewOnly = true;
    quint64 generation = 0;
  };
  struct Entry {
    QImage image;
    bool complete = false;
  };
  QThreadPool pool;
  QCache<QString, Entry> cache{256 * 1024};
  QCache<QString, bool> failed{1024};
  QQueue<Job> queue;
  QSet<QString> queued;
  QHash<QString, std::shared_ptr<std::atomic_bool>> running;
  QTimer pumpDelay, changeDelay;
  QString diskDirectory;
  int workers = 1;
  quint64 starts = 0, hits = 0, sourceReads = 0, previews = 0;
  quint64 generation = 0;
  bool active = false, paused = false;
  void pump();
  void notify();
  void clearRequests(bool cancelRunning);

public:
  explicit ThumbnailCache(QObject *parent = nullptr);
  ~ThumbnailCache() override;
  QImage request(const QFileInfo &, int edge);
  // Same request for callers that already hold the directory entry's size
  // and modification time: no extra stat() on the interface thread.
  QImage request(const QString &absolutePath, qint64 size, qint64 modifiedMs);
  void setActive(bool);
  void setPaused(bool);
  void discardRequests();
  int cachedKiB() const { return cache.totalCost(); }
  int pendingCount() const { return queue.size() + running.size(); }
  int workerCount() const { return workers; }
  quint64 taskStarts() const { return starts; }
  quint64 diskHits() const { return hits; }
  quint64 sourceDecodes() const { return sourceReads; }
  quint64 cameraPreviews() const { return previews; }
  bool busy() const { return !running.isEmpty() || !queue.isEmpty(); }

signals:
  void changed();
};
