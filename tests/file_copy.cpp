// Copying into a folder (drag and drop): classification, the overwrite
// question's answers, and that no answer or failure ever damages a file.
#include "file_ops_core.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
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
struct stat info(const QString &path) {
  struct stat s{};
  check(!lstat(QFile::encodeName(path).constData(), &s), "lstat");
  return s;
}
bool noTemporaries(const QString &folder) {
  return QDir(folder)
      .entryList({".prism-copy-*"}, QDir::Files | QDir::Hidden | QDir::System)
      .isEmpty();
}
const CopyItem &find(const QList<CopyItem> &items, const QString &name) {
  for (const auto &item : items)
    if (QFileInfo(item.target).fileName() == name)
      return item;
  throw std::runtime_error("item missing");
}
} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    QTemporaryDir tmp;
    check(tmp.isValid(), "tempdir");
    const QString photos = tmp.path() + "/zdjęcia", folder = photos + "/wybrane";
    QDir().mkpath(folder);
    auto token = std::make_shared<std::atomic_bool>(false);
    const QByteArray big(3 * 1024 * 1024 + 17, 'n');
    write(photos + "/nowe.jpg", big);
    write(photos + "/inne.jpg", "nowa wersja");
    write(photos + "/takie-samo.jpg", "te same bajty");
    write(photos + "/dowiązanie.jpg", "źródło");
    write(photos + "/katalog.jpg", "źródło");
    write(photos + "/Zażółć [#] $(x).jpg", "polskie znaki");
    write(folder + "/inne.jpg", "stara wersja w folderze");
    write(folder + "/takie-samo.jpg", "te same bajty");
    write(tmp.path() + "/poza.jpg", "plik poza folderem");
    check(!::symlink(QFile::encodeName(tmp.path() + "/poza.jpg").constData(),
                     QFile::encodeName(folder + "/dowiązanie.jpg").constData()),
          "symlink fixture");
    QDir().mkpath(folder + "/katalog.jpg");
    check(!::chmod(QFile::encodeName(folder + "/inne.jpg").constData(), 0640),
          "chmod fixture");
    check(!::chmod(QFile::encodeName(photos + "/nowe.jpg").constData(), 0644),
          "chmod fixture");
    const QStringList sources{photos + "/nowe.jpg",       photos + "/inne.jpg",
                              photos + "/takie-samo.jpg", photos + "/dowiązanie.jpg",
                              photos + "/katalog.jpg",    photos + "/Zażółć [#] $(x).jpg"};

    // --- examination writes nothing and classifies every name ---
    auto items = FileOps::examineCopy(sources, folder, token);
    check(items.size() == sources.size(), "one item per source");
    check(find(items, "nowe.jpg").state == CopyItem::Fresh, "new name is fresh");
    check(find(items, "inne.jpg").state == CopyItem::Exists,
          "different file of that name exists");
    check(find(items, "takie-samo.jpg").state == CopyItem::Identical,
          "same bytes are identical");
    check(find(items, "dowiązanie.jpg").state == CopyItem::Blocked,
          "a link in the folder is never written through");
    check(find(items, "katalog.jpg").state == CopyItem::Blocked,
          "a folder of that name is never replaced");
    check(!QFileInfo::exists(folder + "/nowe.jpg"), "examination wrote nothing");

    // --- "Pomiń istniejące": only new names are written ---
    const auto oldInode = info(folder + "/inne.jpg").st_ino;
    auto report = FileOps::copyItems(items, false, token);
    check(report.error.isEmpty(), "copy without overwrite succeeded");
    check(report.copied == 2 && report.replaced == 0 && report.unchanged == 1 &&
              report.skipped == 3,
          "skip counts");
    check(read(folder + "/nowe.jpg") == big, "new copy is byte-identical");
    check(read(folder + "/Zażółć [#] $(x).jpg") == "polskie znaki",
          "Polish and shell characters in names");
    check(read(folder + "/inne.jpg") == "stara wersja w folderze" &&
              info(folder + "/inne.jpg").st_ino == oldInode,
          "existing file untouched without overwrite");
    check(read(tmp.path() + "/poza.jpg") == "plik poza folderem",
          "link target untouched");
    check((info(folder + "/nowe.jpg").st_mode & 0777) ==
              (0644 & ~[] { mode_t m = umask(022); umask(m); return m; }()),
          "new copy gets the source's permissions under the umask");
    check(info(folder + "/nowe.jpg").st_mtim.tv_sec ==
              info(photos + "/nowe.jpg").st_mtim.tv_sec,
          "new copy keeps the modification time");
    check(read(photos + "/inne.jpg") == "nowa wersja", "source untouched");

    // --- "Nadpisz": replaced atomically, identical files not rewritten ---
    items = FileOps::examineCopy(sources, folder, token);
    check(find(items, "nowe.jpg").state == CopyItem::Identical,
          "earlier copy is now identical");
    const auto identicalInode = info(folder + "/takie-samo.jpg").st_ino;
    report = FileOps::copyItems(items, true, token);
    check(report.error.isEmpty(), "overwrite succeeded");
    check(report.replaced == 1 && report.copied == 0 && report.unchanged == 3 &&
              report.skipped == 2,
          "overwrite counts");
    check(read(folder + "/inne.jpg") == "nowa wersja", "file replaced");
    check((info(folder + "/inne.jpg").st_mode & 0777) == 0640,
          "replacement keeps the replaced file's permissions");
    check(info(folder + "/takie-samo.jpg").st_ino == identicalInode,
          "identical file is not rewritten");
    check(QFileInfo(folder + "/dowiązanie.jpg").isSymLink() &&
              read(tmp.path() + "/poza.jpg") == "plik poza folderem",
          "link and its target survive an overwrite");
    check(QFileInfo(folder + "/katalog.jpg").isDir(), "folder survives");
    check(noTemporaries(folder), "no temporary files left");

    // --- the target changes after the question: never overwritten ---
    write(photos + "/inne.jpg", "trzecia wersja");
    items = FileOps::examineCopy({photos + "/inne.jpg"}, folder, token);
    check(items.value(0).state == CopyItem::Exists, "changed source differs");
    write(folder + "/inne.jpg", "edytowane po pytaniu");
    report = FileOps::copyItems(items, true, token);
    check(report.error.contains("zmienił się") &&
              report.failedFile == "inne.jpg",
          "a target edited after the question is refused");
    check(read(folder + "/inne.jpg") == "edytowane po pytaniu",
          "the edited target keeps its new content");
    check(noTemporaries(folder), "no temporary file after refusal");

    // --- the target becomes a link after the question ---
    items = FileOps::examineCopy({photos + "/inne.jpg"}, folder, token);
    QFile::remove(folder + "/inne.jpg");
    check(!::symlink(QFile::encodeName(tmp.path() + "/poza.jpg").constData(),
                     QFile::encodeName(folder + "/inne.jpg").constData()),
          "symlink swap");
    report = FileOps::copyItems(items, true, token);
    check(!report.error.isEmpty(), "a target swapped for a link is refused");
    check(QFileInfo(folder + "/inne.jpg").isSymLink() &&
              read(tmp.path() + "/poza.jpg") == "plik poza folderem",
          "nothing written through the link");
    QFile::remove(folder + "/inne.jpg");

    // --- a file appears under a fresh name meanwhile: never overwritten ---
    items = FileOps::examineCopy({photos + "/inne.jpg"}, folder, token);
    check(items.value(0).state == CopyItem::Fresh, "name free again");
    write(folder + "/inne.jpg", "pojawił się w międzyczasie");
    report = FileOps::copyItems(items, true, token);
    check(!report.error.isEmpty() &&
              read(folder + "/inne.jpg") == "pojawił się w międzyczasie",
          "a file that appeared meanwhile is kept");

    // --- the folder is the photos' own folder (a link to it) ---
    check(!::symlink(QFile::encodeName(photos).constData(),
                     QFile::encodeName(folder + "/tutaj").constData()),
          "folder link fixture");
    items = FileOps::examineCopy({photos + "/nowe.jpg"}, folder + "/tutaj", token);
    check(items.value(0).state == CopyItem::SameFile, "the source itself");
    report = FileOps::copyItems(items, true, token);
    check(report.error.isEmpty() && report.unchanged == 1 &&
              read(photos + "/nowe.jpg") == big,
          "copying a file onto itself changes nothing");

    // --- cancelling leaves no partial file and no damage ---
    const QString fresh = tmp.path() + "/pusty";
    QDir().mkpath(fresh);
    write(fresh + "/inne.jpg", "zostaje");
    items = FileOps::examineCopy({photos + "/nowe.jpg", photos + "/inne.jpg"},
                                 fresh, token);
    token->store(true);
    report = FileOps::copyItems(items, true, token);
    token->store(false);
    check(report.cancelled && !QFileInfo::exists(fresh + "/nowe.jpg") &&
              read(fresh + "/inne.jpg") == "zostaje" && noTemporaries(fresh),
          "cancel writes nothing");

    // --- invalid requests ---
    bool refused = false;
    try {
      FileOps::examineCopy({photos + "/nowe.jpg", folder + "/nowe.jpg"}, fresh,
                           token);
    } catch (const std::exception &) {
      refused = true;
    }
    check(refused, "two sources with one name are refused");
    refused = false;
    try {
      FileOps::examineCopy({photos + "/nowe.jpg"}, photos + "/nowe.jpg", token);
    } catch (const std::exception &) {
      refused = true;
    }
    check(refused, "a file is not a folder");
    std::cout << "file_copy: OK\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "file_copy: FAIL: " << e.what() << '\n';
    return 1;
  }
}
