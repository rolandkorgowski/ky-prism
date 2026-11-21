// Moving files to the trash (Delete): classification, the re-check right
// before the move, and that nothing is ever deleted permanently. HOME and
// XDG_DATA_HOME point into a temporary folder, so the user's own trash is
// never touched.
#include "file_ops_core.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace {
void check(bool ok, const char *what) {
  if (!ok)
    throw std::runtime_error(what);
}
void write(const QString &path, const QByteArray &data) {
  QFile file(path);
  check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "create fixture");
  check(file.write(data) == data.size(), "write fixture");
}
QByteArray read(const QString &path) {
  QFile file(path);
  check(file.open(QIODevice::ReadOnly), "read file");
  return file.readAll();
}
bool exists(const QString &path) {
  struct stat s{};
  return !lstat(QFile::encodeName(path).constData(), &s);
}
ino_t inode(const QString &path) {
  struct stat s{};
  check(!lstat(QFile::encodeName(path).constData(), &s), "lstat");
  return s.st_ino;
}
// Every regular file below `root` (links not followed). Moving to the trash
// keeps a file's inode, so a permanent deletion shows up as a missing inode;
// the trash's own information files only add new ones.
QSet<quint64> inventory(const QString &root) {
  QSet<quint64> result;
  QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString path = it.next();
    struct stat s{};
    if (!lstat(QFile::encodeName(path).constData(), &s) && S_ISREG(s.st_mode))
      result.insert(quint64(s.st_ino));
  }
  return result;
}
// No file of `before` was deleted permanently.
bool kept(const QSet<quint64> &before, const QString &root) {
  return inventory(root).contains(before);
}
const TrashItem &find(const QList<TrashItem> &items, const QString &name) {
  for (const auto &item : items)
    if (QFileInfo(item.path).fileName() == name)
      return item;
  throw std::runtime_error("item missing");
}
bool refuses(const std::function<void()> &run) {
  try {
    run();
  } catch (const std::exception &) {
    return true;
  }
  return false;
}
} // namespace

int main(int argc, char **argv) {
  QTemporaryDir tmp;
  if (!tmp.isValid())
    return 2;
  // Before anything reads them: the trash must be inside the test folder for
  // every Qt version (before 6.7 Qt chooses it by the disk of HOME).
  qputenv("HOME", tmp.path().toUtf8());
  qputenv("XDG_DATA_HOME", (tmp.path() + "/data").toUtf8());
  QCoreApplication app(argc, argv);
  try {
    const QString photos = tmp.path() + "/zdjęcia";
    const QString trash = tmp.path() + "/data/Trash";
    QDir().mkpath(photos + "/folder");
    auto token = std::make_shared<std::atomic_bool>(false);
    write(photos + "/a.jpg", "pierwsze");
    write(photos + "/b.jpg", QByteArray(2 * 1024 * 1024 + 5, 'b'));
    write(photos + "/c.jpg", "cel dowiązania");
    check(!symlink("c.jpg", QFile::encodeName(photos + "/link.jpg").constData()),
          "symlink");
    check(!mkfifo(QFile::encodeName(photos + "/fifo.jpg").constData(), 0600),
          "fifo");

    // --- examine: only regular files, nothing changes ---
    const auto before = inventory(tmp.path());
    const auto items = FileOps::examineTrash(
        {photos + "/a.jpg", photos + "/b.jpg", photos + "/link.jpg",
         photos + "/folder", photos + "/fifo.jpg", "a.jpg",
         photos + "/brak.jpg", photos + "/./a.jpg"},
        token);
    check(items.size() == 7, "the same file twice is examined once");
    check(!find(items, "a.jpg").blocked && find(items, "a.jpg").size == 8,
          "a regular file is ready");
    check(!find(items, "b.jpg").blocked &&
              find(items, "b.jpg").inode == quint64(inode(photos + "/b.jpg")),
          "identity recorded");
    check(find(items, "link.jpg").blocked &&
              find(items, "link.jpg").note == "to dowiązanie, nie plik",
          "a link is never moved");
    check(find(items, "folder").blocked &&
              find(items, "folder").note.startsWith("to folder"),
          "a folder is never moved");
    check(find(items, "fifo.jpg").blocked, "a special file is never moved");
    check(find(items, "brak.jpg").blocked &&
              find(items, "brak.jpg").note == "pliku już nie ma",
          "a missing file is reported");
    bool relative = false;
    for (const auto &item : items)
      relative |= item.path == "a.jpg" && item.blocked &&
                  item.note == "nieprawidłowa ścieżka";
    check(relative, "a relative path is refused");
    check(inventory(tmp.path()) == before && exists(photos + "/a.jpg"),
          "examining changes nothing");
    check(refuses([&] { FileOps::examineTrash({}, token); }), "no files");
    check(refuses([&] {
            FileOps::examineTrash(QStringList(4097, photos + "/a.jpg"), token);
          }),
          "too many files");

    // --- cancelled before the move: nothing moves ---
    auto cancelled = std::make_shared<std::atomic_bool>(true);
    auto report = FileOps::trashItems(items, cancelled);
    check(report.cancelled && report.trashed == 0 && exists(photos + "/a.jpg") &&
              inventory(tmp.path()) == before,
          "cancel moves nothing");

    // --- confirmed: regular files go to the trash, the rest stays ---
    const auto aInode = inode(photos + "/a.jpg"), bInode = inode(photos + "/b.jpg");
    qint64 lastDone = -1, lastTotal = -1;
    report = FileOps::trashItems(items, token, [&](qint64 done, qint64 total) {
      lastDone = done;
      lastTotal = total;
    });
    check(report.error.isEmpty() && !report.cancelled && report.trashed == 2 &&
              report.skipped == 0,
          "two files moved");
    check(report.trashedPaths ==
              QStringList({photos + "/a.jpg", photos + "/b.jpg"}),
          "moved paths reported");
    check(lastDone == 2 && lastTotal == 2, "progress counts files");
    check(!exists(photos + "/a.jpg") && !exists(photos + "/b.jpg"),
          "moved files left the folder");
    check(read(trash + "/files/a.jpg") == "pierwsze" &&
              inode(trash + "/files/a.jpg") == aInode &&
              inode(trash + "/files/b.jpg") == bInode,
          "the trash holds the very same files");
    const QByteArray info = read(trash + "/info/a.jpg.trashinfo");
    check(info.startsWith("[Trash Info]\n") && info.contains("Path=") &&
              info.contains("a.jpg") && info.contains("DeletionDate="),
          "restore information written");
    check(exists(photos + "/link.jpg") && read(photos + "/c.jpg") == "cel dowiązania" &&
              exists(photos + "/fifo.jpg") && QFileInfo(photos + "/folder").isDir(),
          "the link, its target, the folder and the special file stay");
    check(kept(before, tmp.path()), "nothing deleted permanently");

    // --- changed after the question: stays where it is ---
    write(photos + "/zmienione.jpg", "przed");
    write(photos + "/czas.jpg", "ten sam");
    write(photos + "/podmienione.jpg", "oryginał");
    const auto later = FileOps::examineTrash(
        {photos + "/zmienione.jpg", photos + "/czas.jpg",
         photos + "/podmienione.jpg"},
        token);
    write(photos + "/zmienione.jpg", "po pytaniu");
    const struct timespec times[2] = {{0, UTIME_OMIT}, {1000000000, 0}};
    check(!utimensat(AT_FDCWD, QFile::encodeName(photos + "/czas.jpg").constData(),
                     times, 0),
          "touch");
    write(photos + "/nowe.tmp", "oryginał");
    check(::rename(QFile::encodeName(photos + "/nowe.tmp").constData(),
                   QFile::encodeName(photos + "/podmienione.jpg").constData()) == 0,
          "swap");
    const auto changed = inventory(tmp.path());
    report = FileOps::trashItems(later, token);
    check(report.trashed == 0 && report.skipped == 3 && report.error.isEmpty() &&
              report.skippedNotes.size() == 3 &&
              report.skippedNotes.first() ==
                  "zmienione.jpg — plik zmienił się od pytania",
          "changed, touched and swapped files are skipped");
    check(read(photos + "/zmienione.jpg") == "po pytaniu" &&
              exists(photos + "/czas.jpg") && exists(photos + "/podmienione.jpg") &&
              kept(changed, tmp.path()),
          "skipped files stay untouched");
    QFile::remove(photos + "/zmienione.jpg");
    write(photos + "/zniknie.jpg", "x");
    const auto gone = FileOps::examineTrash({photos + "/zniknie.jpg"}, token);
    QFile::remove(photos + "/zniknie.jpg");
    report = FileOps::trashItems(gone, token);
    check(report.skipped == 1 &&
              report.skippedNotes == QStringList{"zniknie.jpg — pliku już nie ma"},
          "a file removed meanwhile is reported");

    // --- the same name twice: the first one in the trash is kept ---
    write(photos + "/e.jpg", "pierwszy");
    check(FileOps::trashItems(FileOps::examineTrash({photos + "/e.jpg"}, token),
                              token)
                  .trashed == 1,
          "first e.jpg");
    write(photos + "/e.jpg", "drugi");
    check(FileOps::trashItems(FileOps::examineTrash({photos + "/e.jpg"}, token),
                              token)
                  .trashed == 1,
          "second e.jpg");
    QSet<QByteArray> both;
    for (const auto &name :
         QDir(trash + "/files").entryList({"e.jpg*"}, QDir::Files))
      both.insert(read(trash + "/files/" + name));
    check(both == QSet<QByteArray>({"pierwszy", "drugi"}),
          "the trash never overwrites an earlier file");

    // --- no right to change the folder ---
    if (geteuid() != 0) {
      const QString locked = photos + "/zablokowany";
      QDir().mkpath(locked);
      write(locked + "/f.jpg", "chroniony");
      const auto ready = FileOps::examineTrash({locked + "/f.jpg"}, token);
      check(::chmod(QFile::encodeName(locked).constData(), 0555) == 0, "chmod");
      const auto blocked = FileOps::examineTrash({locked + "/f.jpg"}, token);
      check(blocked.first().blocked &&
                blocked.first().note == "brak uprawnień do zmiany folderu",
            "a read-only folder is reported before the question");
      // The folder became read-only after the question: the move fails and
      // the file stays. Qt 6.7+ first links the file into the trash; that
      // second name is removed again, so the trash lists nothing.
      const auto lockedBefore = inventory(tmp.path());
      report = FileOps::trashItems(ready, token);
      check(report.trashed == 0 && !report.error.isEmpty() &&
                report.failedFile == "f.jpg" &&
                read(locked + "/f.jpg") == "chroniony" &&
                kept(lockedBefore, tmp.path()),
            "a read-only folder keeps its file");
      struct stat s{};
      check(!lstat(QFile::encodeName(locked + "/f.jpg").constData(), &s) &&
                s.st_nlink == 1 &&
                QDir(trash + "/files").entryList({"f.jpg*"}, QDir::Files).isEmpty() &&
                QDir(trash + "/info").entryList({"f.jpg*"}, QDir::Files).isEmpty(),
            "the trash keeps no second name of a file that stayed");
      std::cout << "read-only folder: " << report.error.toStdString() << "\n";
      ::chmod(QFile::encodeName(locked).constData(), 0755);
    }

    // --- no usable trash: the file stays, nothing after it is tried ---
    const QString broken = tmp.path() + "/broken";
    QDir().mkpath(broken);
    write(broken + "/Trash", "to nie jest katalog");
    qputenv("XDG_DATA_HOME", broken.toUtf8());
    write(photos + "/g1.jpg", "g1");
    write(photos + "/g2.jpg", "g2");
    const auto pair =
        FileOps::examineTrash({photos + "/g1.jpg", photos + "/g2.jpg"}, token);
    const auto brokenBefore = inventory(tmp.path());
    report = FileOps::trashItems(pair, token);
    check(report.trashed == 0 && report.failedFile == "g1.jpg" &&
              report.error.startsWith("Nie można przenieść pliku do kosza (") &&
              report.error.endsWith("). Plik pozostał na miejscu."),
          "an unusable trash stops with an error");
    check(read(photos + "/g1.jpg") == "g1" && read(photos + "/g2.jpg") == "g2" &&
              kept(brokenBefore, tmp.path()),
          "an unusable trash leaves every file in place");
    qputenv("XDG_DATA_HOME", (tmp.path() + "/data").toUtf8());
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
  std::cout << "file_trash: examine, confirm, re-check, trash only\n";
  return 0;
}
