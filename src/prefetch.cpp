#include "prefetch.h"
#include <QFileInfo>
#include <QtConcurrent>
PreviewPrefetch::PreviewPrefetch(QObject *parent) : QObject(parent) {
  pool.setMaxThreadCount(1);
  pool.setThreadPriority(QThread::LowPriority);
  connect(&watcher, &QFutureWatcher<void>::finished, this, [this] {
    running = false;
    pump();
  });
}
PreviewPrefetch::~PreviewPrefetch() {
  cancel();
  watcher.disconnect(this);
  pool.waitForDone();
}
void PreviewPrefetch::cancel() {
  waiting.clear();
  token->store(true);
}
void PreviewPrefetch::prepare(QStringList neighbors, QString displayProfile,
                              int previewEdge, QSize viewportPixels) {
  cancel();
  viewport = viewportPixels;
  profile = std::move(displayProfile);
  edge = Document::previewBucket(previewEdge);
  for (const auto &name : neighbors) {
    auto ext = QFileInfo(name).suffix().toLower();
    if ((ext == "jpg" || ext == "jpeg") && !waiting.contains(name))
      waiting.append(name);
    if (waiting.size() == 2)
      break;
  }
  pump();
}
void PreviewPrefetch::pump() {
  if (running || waiting.isEmpty())
    return;
  auto name = waiting.takeFirst(), outputProfile = profile;
  auto targetEdge = edge;
  auto viewportPixels = viewport;
  token = std::make_shared<std::atomic_bool>(false);
  auto cancelToken = token;
  running = true;
  watcher.setFuture(QtConcurrent::run(
      &pool, [name, outputProfile, targetEdge, cancelToken, viewportPixels] {
        try {
          Document doc;
          doc.open(name, outputProfile, cancelToken, {}, targetEdge,
                   viewportPixels);
        } catch (const std::exception &) {
          vips_error_clear();
        }
      }));
}
