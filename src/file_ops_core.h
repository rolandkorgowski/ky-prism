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
// One file of a copy into a folder (drag and drop), examined before anything
// is written.
struct CopyItem {
  enum State {
    Fresh,     // no file of that name yet: plain copy
    Exists,    // a different regular file of that name: overwrite only on request
    Identical, // a regular file with the same bytes: nothing to write
    SameFile,  // the source itself (same device and inode): nothing to write
    Blocked    // a link, folder or special file of that name, or a bad source
  };
  QString source, target, note;
  State state = Fresh;
  qint64 sourceSize = 0, sourceModified = 0, targetSize = 0,
         targetModified = 0; // modification times in ms since the epoch
  // Identity of the existing target when it was examined. An overwrite goes
  // ahead only while the target still has exactly this identity.
  quint64 targetDevice = 0, targetInode = 0;
  qint64 targetChangeSec = 0, targetChangeNsec = 0, targetModSec = 0,
         targetModNsec = 0;
};
struct CopyReport {
  int copied = 0, replaced = 0, unchanged = 0, skipped = 0;
  QString error, failedFile; // failedFile: name of the file that stopped it

  bool cancelled = false;
};
// One file the user asked to move to the trash (Delete), examined before the
// confirmation. Only regular files are ever moved; links, folders and special
// files stay where they are.
struct TrashItem {
  QString path, note; // note: why a blocked file stays where it is
  bool blocked = false;
  qint64 size = 0, modified = 0; // bytes; ms since the epoch
  // Identity when the file was examined. The file is moved only while it
  // still has exactly this identity.
  quint64 device = 0, inode = 0;
  qint64 changeSec = 0, changeNsec = 0, modSec = 0, modNsec = 0;
};
struct TrashReport {
  int trashed = 0, skipped = 0;  // skipped: changed or gone since the question
  QStringList trashedPaths;       // original paths, now in the trash
  QStringList skippedNotes;       // "name — reason" of the skipped files
  QString error, failedFile;      // failedFile: name of the file that stopped it
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
  // Classifies every source against `directory`; writes nothing. Fails for
  // an invalid request (no sources, two sources with one name, no folder).
  static QList<CopyItem> examineCopy(const QStringList &sources,
                                     const QString &directory,
                                     const std::shared_ptr<std::atomic_bool> &,
                                     const Progress & = {});
  // Copies the examined items. Fresh files are published without ever
  // replacing anything; Exists items are replaced only when `overwrite` is
  // true, atomically (complete copy on disk first, then rename) and only if
  // the target is still the file that was examined. Stops at the first
  // error; finished files stay.
  static CopyReport copyItems(const QList<CopyItem> &, bool overwrite,
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
  // Delete: classifies the files the user chose (absolute paths); changes
  // nothing. Fails for an invalid request (no files or more than 4096).
  static QList<TrashItem> examineTrash(const QStringList &paths,
                                       const std::shared_ptr<std::atomic_bool> &);
  // Moves the examined files to the system trash, called only after the
  // GUI's confirmation. Never deletes anything permanently and never copies:
  // the trash is reached by a rename (or a link) on the same disk, so each
  // file is at any moment either in its folder or in the trash. A file that
  // changed since it was examined stays where it is. Stops at the first error;
  // files moved before it stay in the trash, where they can be restored.
  static TrashReport trashItems(const QList<TrashItem> &,
                                const std::shared_ptr<std::atomic_bool> &,
                                const Progress & = {});
};
