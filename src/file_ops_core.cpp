// Toolkit-independent copy / duplicate / trash operations (QtCore only).
#include "file_ops_core.h"
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTemporaryFile>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#ifdef __linux__
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace {
void fail(const QString &s) { throw std::runtime_error(s.toStdString()); }
void checkCancel(const std::shared_ptr<std::atomic_bool> &t) {
  if (t->load())
    fail("Anulowano.");
}
bool same(const struct stat &a, const struct stat &b) {
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino &&
         a.st_size == b.st_size && a.st_mtim.tv_sec == b.st_mtim.tv_sec &&
         a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
         a.st_ctim.tv_sec == b.st_ctim.tv_sec &&
         a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}
bool regular(const QString &p) {
  QFileInfo i(p);
  return i.isFile() && !i.isSymLink();
}
bool identicalBytes(const QString &a, const QString &b,
                    const std::shared_ptr<std::atomic_bool> &t,
                    const Progress &progress) {
  QFile x(a), y(b);
  if (!x.open(QIODevice::ReadOnly) || !y.open(QIODevice::ReadOnly))
    return false;
  struct stat xs{}, ys{}, xe{}, ye{}, xn{}, yn{};
  if (fstat(x.handle(), &xs) || fstat(y.handle(), &ys) ||
      xs.st_size != ys.st_size)
    return false;
  QByteArray xb(1024 * 1024, Qt::Uninitialized),
      yb(xb.size(), Qt::Uninitialized);
  QElapsedTimer clock;
  clock.start();
  qint64 compared = 0;
  while (true) {
    checkCancel(t);
    auto n = x.read(xb.data(), xb.size()), m = y.read(yb.data(), yb.size());
    if (n < 0 || n != m || std::memcmp(xb.data(), yb.data(), size_t(n)))
      return false;
    if (!n)
      break;
    compared += n;
    if (progress && clock.elapsed() >= 100) {
      progress(compared, -3);
      clock.restart();
    }
  }
  return !fstat(x.handle(), &xe) && !fstat(y.handle(), &ye) &&
         !lstat(QFile::encodeName(a).constData(), &xn) &&
         !lstat(QFile::encodeName(b).constData(), &yn) && same(xs, xe) &&
         same(ys, ye) && same(xe, xn) && same(ye, yn);
}
} // namespace
namespace {
// umask() can only be read by setting it, so it is read once while the
// program starts, before any worker thread exists.
const mode_t creationMask = [] {
  const mode_t mask = ::umask(022);
  ::umask(mask);
  return mask;
}();
void checkPath(const QString &path) {
  if (path.isEmpty() || path.contains(QChar::Null) || path.size() > 8192)
    fail("Nieprawidłowa ścieżka.");
}
// Copies the open source into `temp` (created in the target folder), checks
// that the source did not change meanwhile, then gives the copy its
// permission bits and the source's modification time and forces it to disk.
// Until the caller publishes it, the copy has a hidden temporary name, so an
// interruption at any point (cancel, crash, power cut) never leaves a
// half-written file under the target name.
void writeCopy(QFile &input, const struct stat &before, const QString &source,
               QTemporaryFile &temp, mode_t mode,
               const std::shared_ptr<std::atomic_bool> &token,
               const Progress &progress) {
  QByteArray buffer(1024 * 1024, Qt::Uninitialized);
  qint64 done = 0;
  QElapsedTimer clock;
  clock.start();
  while (true) {
    checkCancel(token);
    auto n = input.read(buffer.data(), buffer.size());
    if (n < 0)
      fail("Błąd odczytu źródła.");
    if (!n)
      break;
    if (temp.write(buffer.constData(), n) != n)
      fail("Błąd zapisu kopii (sprawdź miejsce na dysku).");
    done += n;
    if (progress && clock.elapsed() >= 100) {
      progress(done, before.st_size);
      clock.restart();
    }
  }
  if (progress)
    progress(done, before.st_size);
  struct stat after{}, named{};
  if (fstat(input.handle(), &after) ||
      lstat(QFile::encodeName(source).constData(), &named) ||
      !same(before, after) || !same(after, named) || done != before.st_size)
    fail("Źródło zmieniło się podczas kopiowania. Kopii nie zapisano.");
  checkCancel(token);
  if (!temp.flush())
    fail("Nie można utrwalić kopii na dysku.");
  // Best effort after the last write: FAT and exFAT media ignore or refuse
  // permissions and times, which must not fail the copy.
  (void)::fchmod(temp.handle(), mode);
  const struct timespec times[2] = {before.st_atim, before.st_mtim};
  (void)::futimens(temp.handle(), times);
  if (fsync(temp.handle()))
    fail("Nie można utrwalić kopii na dysku.");
  checkCancel(token);
}
bool syncDirectory(const QString &path) {
  int directory = ::open(QFile::encodeName(path).constData(),
                         O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory < 0)
    return false;
  const int result = fsync(directory);
  ::close(directory);
  return !result;
}
bool sameTarget(const struct stat &t, const CopyItem &item) {
  return S_ISREG(t.st_mode) && quint64(t.st_dev) == item.targetDevice &&
         quint64(t.st_ino) == item.targetInode &&
         qint64(t.st_size) == item.targetSize &&
         qint64(t.st_mtim.tv_sec) == item.targetModSec &&
         qint64(t.st_mtim.tv_nsec) == item.targetModNsec &&
         qint64(t.st_ctim.tv_sec) == item.targetChangeSec &&
         qint64(t.st_ctim.tv_nsec) == item.targetChangeNsec;
}
qint64 milliseconds(const struct timespec &t) {
  return qint64(t.tv_sec) * 1000 + qint64(t.tv_nsec) / 1000000;
}
// Replaces item.target with a copy of item.source. The old file stays
// untouched until the complete copy is on disk; rename() then swaps the
// names in one atomic step, so after a power cut the target is either the
// old file or the new one, never a mix.
void replaceFile(const CopyItem &item,
                 const std::shared_ptr<std::atomic_bool> &token,
                 const Progress &progress) {
  checkCancel(token);
  checkPath(item.source);
  checkPath(item.target);
  if (!regular(item.source))
    fail("Źródło musi być zwykłym plikiem, bez dowiązania.");
  QFile input(item.source);
  if (!input.open(QIODevice::ReadOnly))
    fail("Nie można odczytać źródła: " + input.errorString());
  struct stat before{}, existing{};
  if (fstat(input.handle(), &before) || !S_ISREG(before.st_mode))
    fail("Nieprawidłowe źródło.");
  const QFileInfo target(item.target);
  const auto targetName = QFile::encodeName(target.absoluteFilePath());
  const QString changed =
      QString("Plik w folderze zmienił się od pytania, więc go nie nadpisano: %1")
          .arg(target.fileName());
  if (lstat(targetName.constData(), &existing) || !sameTarget(existing, item))
    fail(changed);
  if (existing.st_dev == before.st_dev && existing.st_ino == before.st_ino)
    return; // the source itself: nothing to write
  QTemporaryFile temp(target.absolutePath() + "/.prism-copy-XXXXXX");
  if (!temp.open())
    fail("Nie można zapisać kopii w tym katalogu.");
  // The replacement keeps the permission bits of the file it replaces.
  writeCopy(input, before, item.source, temp, existing.st_mode & 0777, token,
            progress);
  struct stat now{};
  if (lstat(targetName.constData(), &now) || !sameTarget(now, item))
    fail(changed);
  if (::rename(QFile::encodeName(temp.fileName()).constData(),
               targetName.constData()))
    fail(QString("Nie można zastąpić pliku %1. Pozostał bez zmian.")
             .arg(target.fileName()));
  temp.setAutoRemove(false);
  if (!syncDirectory(target.absolutePath()))
    fail("Plik zastąpiono, ale system nie potwierdził utrwalenia katalogu.");
}
} // namespace
void FileOps::copy(const QString &source, const QString &destination,
                   const std::shared_ptr<std::atomic_bool> &token,
                   const Progress &progress) {
  checkCancel(token);
  checkPath(source);
  checkPath(destination);
  if (!regular(source))
    fail("Źródło musi być zwykłym plikiem, bez dowiązania.");
  QFile input(source);
  if (!input.open(QIODevice::ReadOnly))
    fail("Nie można odczytać źródła: " + input.errorString());
  struct stat before{};
  if (fstat(input.handle(), &before) || !S_ISREG(before.st_mode))
    fail("Nieprawidłowe źródło.");
  QFileInfo target(destination);
  if (target.exists() || target.isSymLink())
    fail("Plik docelowy już istnieje. Wybierz inną nazwę.");
  QTemporaryFile temp(target.absolutePath() + "/.prism-copy-XXXXXX");
  if (!temp.open())
    fail("Nie można zapisać kopii w tym katalogu.");
  // A new copy gets the source's read/write bits, limited by the umask.
  writeCopy(input, before, source, temp, before.st_mode & 0666 & ~creationMask,
            token, progress);
  // Publish without overwriting: renameat2 works on Linux filesystems including
  // removable media.
  auto temporaryName = QFile::encodeName(temp.fileName()),
       targetName = QFile::encodeName(target.absoluteFilePath());
#ifdef __linux__
  int published =
      int(::syscall(SYS_renameat2, AT_FDCWD, temporaryName.constData(),
                    AT_FDCWD, targetName.constData(), RENAME_NOREPLACE));
#else
  // BSD: link() is the portable no-overwrite publish (fails with EEXIST);
  // the temporary name is removed afterwards.
  int published = -1;
  errno = ENOSYS;
#endif
  bool linked = false;
  if (!published)
    temp.setAutoRemove(false);
  else if (errno == ENOSYS || errno == EINVAL || errno == EOPNOTSUPP) {
    published = ::link(temporaryName.constData(), targetName.constData());
    linked = !published;
  }
  if (published)
    fail("Nie zapisano kopii — cel istnieje lub system plików nie pozwala na "
         "bezpieczny zapis.");
  if (linked) {
    // The copy now has both names; drop the temporary one before the folder
    // is synced, so no hidden second name survives a power cut.
    temp.close();
    temp.remove();
  }
  if (!syncDirectory(target.absolutePath()))
    fail("Kopia powstała, ale system nie potwierdził utrwalenia katalogu.");
}
QList<CopyItem> FileOps::examineCopy(const QStringList &sources,
                                     const QString &directory,
                                     const std::shared_ptr<std::atomic_bool> &token,
                                     const Progress &progress) {
  if (sources.isEmpty() || sources.size() > 4096)
    fail("Wybierz 1–4096 plików i istniejący katalog docelowy.");
  checkPath(directory);
  const QFileInfo folder(directory);
  if (!folder.isDir())
    fail("Wybierz 1–4096 plików i istniejący katalog docelowy.");
  const QDir into(folder.absoluteFilePath());
  QSet<QString> names;
  QList<CopyItem> items;
  QElapsedTimer clock;
  clock.start();
  for (const auto &path : sources) {
    checkCancel(token);
    checkPath(path);
    const QFileInfo info(path);
    const QString name = info.fileName();
    if (name.isEmpty() || name == "." || name == "..")
      fail("Nieprawidłowe źródło.");
    if (names.contains(name))
      fail("Dwa źródła mają tę samą nazwę: " + name);
    names.insert(name);
    CopyItem item;
    item.source = info.absoluteFilePath();
    item.target = into.filePath(name);
    struct stat source{}, target{};
    if (lstat(QFile::encodeName(item.source).constData(), &source) ||
        !S_ISREG(source.st_mode)) {
      item.state = CopyItem::Blocked;
      item.note = "źródło jest dowiązaniem lub nie jest zwykłym plikiem";
      items.append(item);
      continue;
    }
    item.sourceSize = qint64(source.st_size);
    item.sourceModified = milliseconds(source.st_mtim);
    if (lstat(QFile::encodeName(item.target).constData(), &target)) {
      if (errno == ENOENT)
        item.state = CopyItem::Fresh;
      else {
        item.state = CopyItem::Blocked;
        item.note = "nie można sprawdzić pliku w folderze";
      }
    } else if (S_ISLNK(target.st_mode)) {
      item.state = CopyItem::Blocked;
      item.note = "w folderze jest dowiązanie o tej nazwie";
    } else if (S_ISDIR(target.st_mode)) {
      item.state = CopyItem::Blocked;
      item.note = "w folderze jest katalog o tej nazwie";
    } else if (!S_ISREG(target.st_mode)) {
      item.state = CopyItem::Blocked;
      item.note = "w folderze jest plik specjalny o tej nazwie";
    } else if (target.st_dev == source.st_dev &&
               target.st_ino == source.st_ino) {
      item.state = CopyItem::SameFile;
    } else {
      item.targetSize = qint64(target.st_size);
      item.targetModified = milliseconds(target.st_mtim);
      item.targetDevice = quint64(target.st_dev);
      item.targetInode = quint64(target.st_ino);
      item.targetModSec = qint64(target.st_mtim.tv_sec);
      item.targetModNsec = qint64(target.st_mtim.tv_nsec);
      item.targetChangeSec = qint64(target.st_ctim.tv_sec);
      item.targetChangeNsec = qint64(target.st_ctim.tv_nsec);
      item.state = item.targetSize == item.sourceSize &&
                           identicalBytes(item.source, item.target, token, {})
                       ? CopyItem::Identical
                       : CopyItem::Exists;
    }
    items.append(item);
    if (progress && clock.elapsed() >= 100) {
      progress(items.size(), sources.size());
      clock.restart();
    }
  }
  return items;
}
CopyReport FileOps::copyItems(const QList<CopyItem> &items, bool overwrite,
                              const std::shared_ptr<std::atomic_bool> &token,
                              const Progress &progress) {
  CopyReport report;
  qint64 total = 0, done = 0;
  QString folder;
  for (const auto &item : items) {
    const bool writes = item.state == CopyItem::Fresh ||
                        (overwrite && item.state == CopyItem::Exists);
    if (writes) {
      if (item.sourceSize < 0 ||
          item.sourceSize > std::numeric_limits<qint64>::max() - total) {
        report.error = "Nieprawidłowy łączny rozmiar.";
        return report;
      }
      total += item.sourceSize;
      folder = QFileInfo(item.target).absolutePath();
    }
  }
  if (!folder.isEmpty()) {
    // A replaced file frees its space only after the rename, so every copy
    // needs room of its own.
    QStorageInfo disk(folder);
    if (disk.isValid() && disk.isReady() && disk.bytesAvailable() < total) {
      report.error = "Za mało wolnego miejsca na kopie.";
      return report;
    }
  }
  QElapsedTimer clock;
  clock.start();
  QString current;
  try {
    for (const auto &item : items) {
      checkCancel(token);
      current = QFileInfo(item.target).fileName();
      auto step = [&](qint64 n, qint64) {
        if (progress && clock.elapsed() >= 100) {
          progress(done + n, total);
          clock.restart();
        }
      };
      switch (item.state) {
      case CopyItem::Fresh:
        copy(item.source, item.target, token, step);
        done += item.sourceSize;
        ++report.copied;
        break;
      case CopyItem::Exists:
        if (!overwrite) {
          ++report.skipped;
          break;
        }
        replaceFile(item, token, step);
        done += item.sourceSize;
        ++report.replaced;
        break;
      case CopyItem::Identical:
      case CopyItem::SameFile:
        ++report.unchanged;
        break;
      case CopyItem::Blocked:
        ++report.skipped;
        break;
      }
    }
  } catch (const std::exception &e) {
    report.cancelled = token->load();
    report.error = QString::fromUtf8(e.what());
    report.failedFile = current;
    return report;
  }
  if (progress)
    progress(total, total);
  return report;
}
QList<DuplicateGroup>
FileOps::duplicates(const QString &root,
                    const std::shared_ptr<std::atomic_bool> &token,
                    const Progress &progress) {
  if (!QFileInfo(root).isDir() || QFileInfo(root).isSymLink())
    fail("Wskaż zwykły katalog do przeszukania.");
  QMap<QString, QStringList> candidates;
  int entries = 0;
  QElapsedTimer clock;
  clock.start();
  QDirIterator it(root,
                  QDir::Files | QDir::Dirs | QDir::Hidden |
                      QDir::NoDotAndDotDot | QDir::NoSymLinks,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    checkCancel(token);
    QString path = it.next();
    if (++entries > 200000)
      fail("Limit 200 000 wpisów. Wybierz mniejszy katalog.");
    auto info = it.fileInfo();
    if ((info.isFile() || info.isDir()) && !info.isReadable())
      fail("Brak dostępu do: " + path);
    if (info.isFile())
      candidates[QString::number(info.size()) + "/" + info.fileName()].append(
          path);
    if (progress && clock.elapsed() >= 100) {
      progress(entries, -1);
      clock.restart();
    }
  }
  QList<DuplicateGroup> result;
  int hashed = 0;
  for (auto paths : candidates) {
    if (paths.size() < 2)
      continue;
    QMap<QString, QStringList> byHash;
    for (const auto &p : paths) {
      checkCancel(token);
      auto info = QFileInfo(p);
      qint64 size = 0;
      QString md5 = FavoriteStore::hash(
          p, token,
          [&](qint64, qint64) {
            if (progress && clock.elapsed() >= 100) {
              progress(hashed, -2);
              clock.restart();
            }
          },
          &size);
      if (size != info.size())
        fail("Plik zmienił się podczas skanowania. Powtórz wyszukiwanie.");
      byHash[md5].append(p);
      ++hashed;
      if (progress && clock.elapsed() >= 100) {
        progress(hashed, -2);
        clock.restart();
      }
    }
    for (auto group = byHash.cbegin(); group != byHash.cend(); ++group)
      if (group.value().size() > 1) {
        QFileInfo info(group.value().first());
        result.append(
            {info.fileName(), group.key(), info.size(), group.value()});
      }
  }
  checkCancel(token);
  return result;
}
void FileOps::trash(const DuplicateGroup &group, const QString &selected,
                    const std::shared_ptr<std::atomic_bool> &token,
                    const Progress &progress) {
  checkCancel(token);
  if (!group.paths.contains(selected) || !regular(selected) ||
      QFileInfo(selected).fileName() != group.name)
    fail("Wybrany plik nie należy już do tej grupy.");
  struct stat before{}, after{};
  if (lstat(QFile::encodeName(selected).constData(), &before))
    fail("Brak pliku.");
  auto report = [&](qint64 n, qint64) {
    if (progress)
      progress(n, -3);
  };
  qint64 size = 0;
  if (FavoriteStore::hash(selected, token, report, &size) != group.md5 ||
      size != group.size)
    fail("Plik zmienił się od skanowania. Nie przeniesiono go do kosza.");
  bool survivor = false;
  for (const auto &other : group.paths) {
    checkCancel(token);
    if (other == selected || !regular(other) ||
        QFileInfo(other).fileName() != group.name)
      continue;
    try {
      qint64 otherSize = 0;
      if (FavoriteStore::hash(other, token, report, &otherSize) == group.md5 &&
          otherSize == group.size &&
          identicalBytes(selected, other, token, report)) {
        survivor = true;
        break;
      }
    } catch (const std::exception &) {
      checkCancel(token);
    }
  }
  if (!survivor)
    fail("Nie potwierdzono drugiej identycznej kopii. Pozostawiono plik.");
  checkCancel(token);
  if (lstat(QFile::encodeName(selected).constData(), &after) ||
      !same(before, after))
    fail("Plik zmienił się przed przeniesieniem. Pozostawiono go.");
  if (!QDir().mkpath(QStandardPaths::writableLocation(
          QStandardPaths::GenericDataLocation)))
    fail("Nie można przygotować katalogu danych kosza.");
  QFile trashFile(selected);
  if (!trashFile.moveToTrash())
    fail("Nie można przenieść pliku do kosza: " + trashFile.errorString() +
         ". Pliku nie usunięto trwale.");
}
namespace {
bool sameItem(const struct stat &t, const TrashItem &item) {
  return S_ISREG(t.st_mode) && quint64(t.st_dev) == item.device &&
         quint64(t.st_ino) == item.inode && qint64(t.st_size) == item.size &&
         qint64(t.st_mtim.tv_sec) == item.modSec &&
         qint64(t.st_mtim.tv_nsec) == item.modNsec &&
         qint64(t.st_ctim.tv_sec) == item.changeSec &&
         qint64(t.st_ctim.tv_nsec) == item.changeNsec;
}
} // namespace
QList<TrashItem>
FileOps::examineTrash(const QStringList &paths,
                      const std::shared_ptr<std::atomic_bool> &token) {
  if (paths.isEmpty() || paths.size() > 4096)
    fail("Wybierz od 1 do 4096 plików.");
  QList<TrashItem> items;
  QSet<QString> seen;
  for (const auto &original : paths) {
    checkCancel(token);
    TrashItem item;
    item.path = original;
    item.blocked = true;
    if (original.isEmpty() || original.contains(QChar::Null) ||
        original.size() > 8192 || QDir::isRelativePath(original)) {
      item.note = "nieprawidłowa ścieżka";
      items.append(item);
      continue;
    }
    item.path = QDir::cleanPath(original);
    if (seen.contains(item.path))
      continue;
    seen.insert(item.path);
    struct stat st{};
    const QByteArray native = QFile::encodeName(item.path);
    const QString folder = QFileInfo(item.path).absolutePath();
    if (lstat(native.constData(), &st))
      item.note = errno == ENOENT ? "pliku już nie ma" : "nie można sprawdzić pliku";
    else if (S_ISLNK(st.st_mode))
      item.note = "to dowiązanie, nie plik";
    else if (S_ISDIR(st.st_mode))
      item.note = "to folder, a foldery nie trafiają do kosza";
    else if (!S_ISREG(st.st_mode))
      item.note = "to plik specjalny";
    // Moving a name out of a folder needs the right to change that folder.
    else if (::access(QFile::encodeName(folder).constData(), W_OK | X_OK))
      item.note = "brak uprawnień do zmiany folderu";
    else {
      item.blocked = false;
      item.size = qint64(st.st_size);
      item.modified = milliseconds(st.st_mtim);
      item.device = quint64(st.st_dev);
      item.inode = quint64(st.st_ino);
      item.modSec = qint64(st.st_mtim.tv_sec);
      item.modNsec = qint64(st.st_mtim.tv_nsec);
      item.changeSec = qint64(st.st_ctim.tv_sec);
      item.changeNsec = qint64(st.st_ctim.tv_nsec);
    }
    items.append(item);
  }
  return items;
}
TrashReport FileOps::trashItems(const QList<TrashItem> &items,
                                const std::shared_ptr<std::atomic_bool> &token,
                                const Progress &progress) {
  TrashReport r;
  qint64 total = 0, done = 0;
  for (const auto &item : items)
    total += !item.blocked;
  bool prepared = false;
  for (const auto &item : items) {
    if (item.blocked)
      continue;
    if (token->load()) {
      r.cancelled = true;
      break;
    }
    if (progress)
      progress(done, total);
    const QString name = QFileInfo(item.path).fileName();
    const QByteArray native = QFile::encodeName(item.path);
    // The very file that was shown in the question, or nothing.
    struct stat now{};
    if (lstat(native.constData(), &now)) {
      ++r.skipped;
      r.skippedNotes << QString("%1 — pliku już nie ma").arg(name);
      continue;
    }
    if (!sameItem(now, item)) {
      ++r.skipped;
      r.skippedNotes << QString("%1 — plik zmienił się od pytania").arg(name);
      continue;
    }
    // Qt needs the data folder of the home trash ($XDG_DATA_HOME).
    if (!prepared && !QDir().mkpath(QStandardPaths::writableLocation(
                         QStandardPaths::GenericDataLocation))) {
      r.error = "Nie można przygotować katalogu danych kosza.";
      r.failedFile = name;
      break;
    }
    prepared = true;
    // QFile::moveToTrash follows the freedesktop.org trash specification:
    // the information file is written first, then the file is renamed (or
    // linked and its old name removed) into the trash of the same disk.
    // It never copies, so it cannot leave a half-written file.
    QFile file(item.path);
    if (!file.moveToTrash()) {
      r.error = QString("Nie można przenieść pliku do kosza (%1). Plik pozostał "
                        "na miejscu.")
                    .arg(file.errorString());
      r.failedFile = name;
      break;
    }
    ++done;
    // Qt (6.7+) links the file into the trash, then removes its old name.
    // Without the right to change the folder that removal fails silently:
    // the file never left the folder.
    struct stat left{}, moved{};
    if (!lstat(native.constData(), &left) &&
        quint64(left.st_dev) == item.device &&
        quint64(left.st_ino) == item.inode) {
      // The trash entry is then only a second name of the very same file.
      // Removing that name (and its information file) keeps the trash from
      // listing a file that is still in its folder; the file is untouched.
      const QString extra = file.fileName();
      struct stat second{};
      if (extra != item.path &&
          !lstat(QFile::encodeName(extra).constData(), &second) &&
          second.st_dev == left.st_dev && second.st_ino == left.st_ino &&
          left.st_nlink >= 2 &&
          ::unlink(QFile::encodeName(extra).constData()) == 0) {
        const QFileInfo trashed(extra);
        const QString info = QDir::cleanPath(trashed.absolutePath() + "/../info/" +
                                             trashed.fileName() + ".trashinfo");
        if (QFileInfo(info).isFile() && !QFileInfo(info).isSymLink())
          QFile::remove(info);
      }
      r.error = "Plik pozostał w folderze (brak uprawnień do zmiany folderu).";
      r.failedFile = name;
      break;
    }
    ++r.trashed;
    r.trashedPaths << item.path;
    if (lstat(QFile::encodeName(file.fileName()).constData(), &moved) ||
        quint64(moved.st_ino) != item.inode) {
      r.error = "W koszu jest inny plik niż sprawdzony. Sprawdź kosz.";
      r.failedFile = name;
      break;
    }
  }
  if (progress)
    progress(done, total);
  return r;
}
// Virtual hierarchy: no thumbnail work or one-widget-per-file allocation on the
// GUI thread.
void FileOps::copyBatch(const QStringList &sources, const QString &directory,
                        const std::shared_ptr<std::atomic_bool> &token,
                        const Progress &progress) {
  if (sources.isEmpty() || sources.size() > 4096 ||
      directory.contains(QChar::Null) || !QFileInfo(directory).isDir())
    fail("Wybierz 1–4096 plików i istniejący katalog docelowy.");
  QSet<QString> names;
  qint64 total = 0, done = 0;
  // Check all names before publishing any file. A concurrent collision is also
  // rejected atomically by copy(), never overwritten.
  for (const auto &src : sources) {
    checkCancel(token);
    QFileInfo f(src);
    auto name = f.fileName();
    if (src.contains(QChar::Null) || !regular(src) || name.isEmpty())
      fail("Nieprawidłowe źródło.");
    if (names.contains(name))
      fail("Dwa źródła mają tę samą nazwę: " + name);
    names.insert(name);
    const auto dst = QDir(directory).filePath(name);
    if (QFileInfo::exists(dst) || QFileInfo(dst).isSymLink())
      fail("Cel już istnieje: " + dst);
    if (f.size() < 0 || f.size() > std::numeric_limits<qint64>::max() - total)
      fail("Nieprawidłowy łączny rozmiar.");
    total += f.size();
  }
  QStorageInfo disk(directory);
  if (disk.isValid() && disk.isReady() && disk.bytesAvailable() < total)
    fail("Za mało wolnego miejsca na kopie.");
  QElapsedTimer clock;
  clock.start();
  int completed = 0;
  try {
    for (const auto &src : sources) {
      checkCancel(token);
      auto size = QFileInfo(src).size();
      copy(src, QDir(directory).filePath(QFileInfo(src).fileName()), token,
           [&](qint64 n, qint64) {
             if (progress && clock.elapsed() >= 100) {
               progress(done + n, total);
               clock.restart();
             }
           });
      done += size;
      ++completed;
    }
  } catch (const std::exception &e) {
    fail(
        QString(
            "Zapisano %1 z %2 plików. Ukończone kopie pozostają w katalogu. %3")
            .arg(completed)
            .arg(sources.size())
            .arg(QString::fromUtf8(e.what())));
  }
  if (progress)
    progress(total, total);
}
