#pragma once
#include "localized_widgets.h"
#include <QCache>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QQueue>
#include <QSet>
#include <QThreadPool>
#include <atomic>
#include <memory>

// Separate, bounded chooser cache. The main gallery/renderer are unchanged.
class PhotoDialogCache final : public QObject {
  Q_OBJECT
  struct Job { QString key, path; quint64 generation; };
  QThreadPool pool;
  QCache<QString, QImage> images{48 * 1024}; // KiB; no full-size photographs.
  QSet<QString> pending;
  QCache<QString, bool> failed{256};
  QQueue<Job> queue;
  QHash<QString, std::shared_ptr<std::atomic_bool>> running;
  quint64 generation = 0;
  bool active = false;
  void pump();
public:
  explicit PhotoDialogCache(QObject *parent);
  ~PhotoDialogCache() override;
  QImage request(const QFileInfo &file);
  void setActive(bool enabled);
  void resetRequests();
  bool busy() const { return !running.isEmpty() || !queue.isEmpty(); }
  int cachedKiB() const { return images.totalCost(); }
Q_SIGNALS:
  void changed();
};

class PhotoDialogPreview;
class PhotoFileDialog final : public UiFileDialog {
  PhotoDialogCache cache;
  PhotoDialogPreview *preview;
  QString currentPath;
  void refreshPreview();
protected:
  void showEvent(QShowEvent *) override;
  void hideEvent(QHideEvent *) override;
public:
  explicit PhotoFileDialog(QWidget *parent, const QString &directory,
                           const QString &filter);
};
