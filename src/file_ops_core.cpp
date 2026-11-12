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
void FileOps::copy(const QString &source, const QString &destination,
                   const std::shared_ptr<std::atomic_bool> &token,
                   const Progress &progress) {
  checkCancel(token);
  if (source.contains(QChar::Null) || destination.contains(QChar::Null) ||
      source.size() > 8192 || destination.size() > 8192)
    fail("Nieprawidłowa ścieżka.");
  if (!regular(source))
    fail("Źródło musi być zwykłym plikiem, bez dowiązania.");
  QFile input(source);
  if (!input.open(QIODevice::ReadOnly))
    fail("Nie można odczytać źródła: " + input.errorString());
  struct stat before{}, after{}, named{};
  if (fstat(input.handle(), &before) || !S_ISREG(before.st_mode))
    fail("Nieprawidłowe źródło.");
  QFileInfo target(destination);
  if (target.exists() || target.isSymLink())
    fail("Plik docelowy już istnieje. Wybierz inną nazwę.");
  QTemporaryFile temp(target.absolutePath() + "/.prism-copy-XXXXXX");
  if (!temp.open())
    fail("Nie można zapisać kopii w tym katalogu.");
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
  if (fstat(input.handle(), &after) ||
      lstat(QFile::encodeName(source).constData(), &named) ||
      !same(before, after) || !same(after, named) || done != before.st_size)
    fail("Źródło zmieniło się podczas kopiowania. Kopii nie zapisano.");
  checkCancel(token);
  if (!temp.flush() || fsync(temp.handle()))
    fail("Nie można utrwalić kopii na dysku.");
  checkCancel(token);
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
  if (!published)
    temp.setAutoRemove(false);
  else if (errno == ENOSYS || errno == EINVAL || errno == EOPNOTSUPP)
    published = ::link(temporaryName.constData(), targetName.constData());
  if (published)
    fail("Nie zapisano kopii — cel istnieje lub system plików nie pozwala na "
         "bezpieczny zapis.");
  int directory = ::open(QFile::encodeName(target.absolutePath()).constData(),
                         O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory < 0)
    fail("Kopia powstała, ale system nie potwierdził utrwalenia katalogu.");
  if (directory >= 0) {
    int result = fsync(directory);
    ::close(directory);
    if (result)
      fail("Kopia powstała, ale system nie potwierdził utrwalenia katalogu.");
  }
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
