#pragma once
#include "language.h"
#include "local_path.h"
#include <QCollator>
#include <QDirIterator>
#include <QFileSystemModel>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QStorageInfo>
#include <QThreadPool>
#include <QtConcurrent>
#include <atomic>
// QFileSystemModel enumerates asynchronously. Birth dates have a separate,
// bounded worker scan; sorting never stats every file on the GUI thread.
class LibraryProxy final : public QSortFilterProxyModel {
  QCollator collator;
  QThreadPool pool;
  QFutureWatcher<QHash<QString, qint64>> watcher;
  QHash<QString, qint64> born;
  QString directory, pending;
  bool scanning = false, stopped = false;
  std::shared_ptr<std::atomic_bool> cancel =
      std::make_shared<std::atomic_bool>(false);
  void scanBirthDates() {
    if (stopped) return;
    if (scanning) {
      cancel->store(true);
      pending = directory;
      return;
    }
    auto dir = directory;
    pending.clear();
    cancel = std::make_shared<std::atomic_bool>(false);
    auto token = cancel;
    scanning = true;
    watcher.setFuture(QtConcurrent::run(&pool, [dir, token] {
      QHash<QString, qint64> result;
      QDirIterator it(dir, QDir::Files | QDir::AllDirs | QDir::NoDotAndDotDot);
      int count = 0;
      while (it.hasNext() && !token->load() && count++ < 50000) {
        it.next();
        const auto f = it.fileInfo();
        const auto birth = f.birthTime();
        if (birth.isValid())
          result.insert(f.absoluteFilePath(), birth.toMSecsSinceEpoch());
      }
      return result;
    }));
  }

protected:
  bool lessThan(const QModelIndex &a, const QModelIndex &b) const override {
    auto model = static_cast<QFileSystemModel *>(sourceModel());
    auto x = model->fileInfo(a), y = model->fileInfo(b);
    if (x.isDir() != y.isDir())
      return sortOrder() == Qt::AscendingOrder ? x.isDir() : !x.isDir();
    if (a.column() == 1 && x.size() != y.size())
      return x.size() < y.size();
    if (a.column() == 2) {
      const auto l = born.value(x.absoluteFilePath(), -1),
                 r = born.value(y.absoluteFilePath(), -1);
      if (l != r)
        return l < r;
    }
    if (a.column() == 3 && x.lastModified() != y.lastModified())
      return x.lastModified() < y.lastModified();
    return collator.compare(x.fileName(), y.fileName()) < 0;
  }

public:
  explicit LibraryProxy(QObject *parent = nullptr)
      : QSortFilterProxyModel(parent) {
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    setDynamicSortFilter(true);
    pool.setMaxThreadCount(1);
    pool.setThreadPriority(QThread::LowPriority);
    connect(&watcher, &QFutureWatcher<QHash<QString, qint64>>::finished, this,
            [this] {
              scanning = false;
              if (!cancel->load()) {
                born = watcher.result();
                invalidate();
              }
              if (!pending.isEmpty())
                scanBirthDates();
            });
  }
  ~LibraryProxy() override {
    stopWork();
    pool.waitForDone();
  }
  void stopWork() {
    stopped = true;
    cancel->store(true);
    pending.clear();
    watcher.disconnect(this);
  }
  void setDirectory(const QString &dir) {
    if (directory == dir)
      return;
    directory = dir;
    born.clear();
    if (sortColumn() == 2)
      scanBirthDates();
    else {
      cancel->store(true);
      pending.clear();
    }
  }
  void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override {
    QSortFilterProxyModel::sort(column, order);
    if (column == 2 && !directory.isEmpty())
      scanBirthDates();
  }
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
      static const QStringList names = {"Nazwa", "Rozmiar", "Utworzono",
                                        "Zmieniono"};
      return PrismLanguage::text(names.value(section));
    }
    return QSortFilterProxyModel::headerData(section, orientation, role);
  }
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override {
    if (index.column() == 2 && role == Qt::DisplayRole) {
      auto model = static_cast<QFileSystemModel *>(sourceModel());
      auto t = born.value(model->filePath(mapToSource(index)), -1);
      return t < 0 ? QString("—")
                   : QDateTime::fromMSecsSinceEpoch(t).toString(
                         "yyyy-MM-dd HH:mm");
    }
    if (role == Qt::ToolTipRole) {
      auto model = static_cast<QFileSystemModel *>(sourceModel());
      return model->filePath(mapToSource(index));
    }
    if (index.column() == 1 && role == Qt::TextAlignmentRole)
      return int(Qt::AlignRight | Qt::AlignVCenter);
    return QSortFilterProxyModel::data(index, role);
  }
};
struct RemovableVolume {
  QString path, name, device;
};
inline bool usbDevice(const QByteArray &device,
                      const QString &sysRoot = "/sys") {
  QString block = QFileInfo(QString::fromLocal8Bit(device)).fileName();
  if (block.isEmpty())
    return false;
  QString sys =
      QFileInfo(sysRoot + "/class/block/" + block).canonicalFilePath();
  if (sys.isEmpty())
    return false;
  if (sys.contains(QRegularExpression("/usb[0-9]+/")))
    return true;
  for (int i = 0; i < 3 && !sys.isEmpty(); ++i) {
    QFile f(sys + "/removable");
    if (f.open(QIODevice::ReadOnly) && f.read(8).trimmed() == "1")
      return true;
    sys = QFileInfo(sys).absolutePath();
  }
  return false;
}
inline QList<RemovableVolume> mountedUsbVolumes() {
  QList<RemovableVolume> result;
  for (const auto &v : QStorageInfo::mountedVolumes())
    if (v.isValid() && v.isReady() && !v.isRoot() && usbDevice(v.device()))
      result.append(
          {v.rootPath(), v.displayName(), QString::fromLocal8Bit(v.device())});
  return result;
}
