#pragma once
// Toolkit-independent copy / duplicate / trash operations (QtCore only).
#include "favorites.h"
#include <QList>
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>
struct DuplicateGroup {
  QString name, md5;
  qint64 size = 0;
  QStringList paths;
};
struct FileResult {
  QList<DuplicateGroup> groups;
  QString error, message;
  bool cancelled = false;
};
class FileOps {
public:
  static void copy(const QString &source, const QString &destination,
                   const std::shared_ptr<std::atomic_bool> &,
                   const Progress & = {});
  static void copyBatch(const QStringList &, const QString &directory,
                        const std::shared_ptr<std::atomic_bool> &,
                        const Progress & = {});
  static QList<DuplicateGroup>
  duplicates(const QString &root, const std::shared_ptr<std::atomic_bool> &,
             const Progress & = {});
  // Explicitly called only after the GUI's confirmation. Never deletes
  // permanently.
  static void trash(const DuplicateGroup &, const QString &selected,
                    const std::shared_ptr<std::atomic_bool> &,
                    const Progress & = {});
};
