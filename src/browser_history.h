#pragma once
#include <QStringList>
// Directory history is independent of photo navigation. Branching discards
// forward entries.
class BrowserHistory {
  QStringList entries;
  int cursor = -1;

public:
  QString current() const {
    return cursor < 0 ? QString() : entries.at(cursor);
  }
  bool backAvailable() const { return cursor > 0; }
  bool forwardAvailable() const { return cursor + 1 < entries.size(); }
  void visit(const QString &path) {
    if (path == current())
      return;
    while (entries.size() > cursor + 1)
      entries.removeLast();
    entries.append(path);
    if (entries.size() > 100)
      entries.removeFirst();
    cursor = entries.size() - 1;
  }
  QString move(int direction) {
    int next = cursor + direction;
    if (next < 0 || next >= entries.size())
      return {};
    cursor = next;
    return current();
  }
};

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <stdexcept>
class FolderBookmarks {
  static void fail() {
    throw std::runtime_error(
        "Nie można odczytać lub zapisać zakładek. Pliku nie nadpisano.");
  }

public:
  static QString filename() {
    return QStandardPaths::writableLocation(
               QStandardPaths::GenericDataLocation) +
           "/omarchy-prism/bookmarks.json";
  }
  static QStringList read() {
    QFile file(filename());
    if (!file.exists())
      return {};
    if (file.size() > 128 * 1024 || !file.open(QIODevice::ReadOnly))
      fail();
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray() ||
        doc.array().size() > 128)
      fail();
    QStringList paths;
    for (auto value : doc.array()) {
      auto path = value.toString();
      if (!QDir::isAbsolutePath(path) || path.contains(QChar(0)) ||
          path.size() > 8192)
        fail();
      if (!paths.contains(path))
        paths.append(path);
    }
    return paths;
  }
  static void change(const QString &directory, bool add) {
    if (!QDir::isAbsolutePath(directory) || directory.contains(QChar(0)) ||
        directory.size() > 8192)
      fail();
    auto file = filename();
    if (!QDir().mkpath(QFileInfo(file).absolutePath()) ||
        QFileInfo(file).isSymLink())
      fail();
    QLockFile lock(file + ".lock");
    if (!lock.tryLock(0))
      fail();
    auto paths = read();
    if (add) {
      if (paths.contains(directory))
        return;
      if (paths.size() >= 128)
        fail();
      paths.append(directory);
    } else
      paths.removeAll(directory);
    QJsonArray array;
    for (const auto &p : paths)
      array.append(p);
    auto bytes = QJsonDocument(array).toJson();
    if (bytes.size() > 128 * 1024)
      fail();
    QSaveFile output(file);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly))
      fail();
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (output.write(bytes) != bytes.size() || !output.commit())
      fail();
  }
};
