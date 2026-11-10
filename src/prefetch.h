#pragma once
#include "document.h"
#include <QFutureWatcher>
#include <QObject>
#include <QThreadPool>
// Two adjacent JPEGs at most. One cancellable low-priority worker, no timer or
// background directory scan. Never competes with a foreground open.
class PreviewPrefetch final : public QObject {
  Q_OBJECT
  QThreadPool pool;
  QFutureWatcher<void> watcher;
  QStringList waiting;
  QString profile;
  int edge = 2048;
  QSize viewport;
  bool running = false;
  std::shared_ptr<std::atomic_bool> token =
      std::make_shared<std::atomic_bool>(false);
  void pump();

public:
  explicit PreviewPrefetch(QObject *parent = nullptr);
  ~PreviewPrefetch() override;
  void prepare(QStringList neighbors, QString displayProfile, int previewEdge,
               QSize viewportPixels = {});
  void cancel();
  bool busy() const { return running || !waiting.isEmpty(); }
};
