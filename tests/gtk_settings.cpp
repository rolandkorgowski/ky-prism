// Interface-opacity preference: parsing, defaults and atomic persistence.
#include "settings.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
static void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main() {
  try {
    QTemporaryDir tmp;
    check(tmp.isValid(), "temporary directory");
    const QString file = tmp.path() + "/nested/ky-prism/appearance.json";
    using namespace PrismSettings;
    check(storedOpacity({}, file) == -1, "missing file must not invent a value");
    check(storedOpacity("80", file) == 80, "environment override");
    check(storedOpacity("101", file) == -1 && storedOpacity("-1", file) == -1 &&
              storedOpacity("abc", file) == -1,
          "invalid environment values are ignored");
    check(defaultOpacity(false) > defaultOpacity(true),
          "without compositor blur the default must be more opaque");
    check(defaultOpacity(false) >= 90 && defaultOpacity(true) == 69,
          "defaults: readable without blur, unchanged 0.12 material with blur");
    check(minimumSliderOpacity >= 20 && minimumSliderOpacity < 60,
          "slider keeps the interface usable");
    check(saveOpacity(file, 73), "save creates the directory");
    check(storedOpacity({}, file) == 73, "saved value is read back");
    check(storedOpacity("55", file) == 55, "environment wins over the file");
    check(!saveOpacity(file, 101) && !saveOpacity(file, -5),
          "out-of-range values are rejected");
    check(storedOpacity({}, file) == 73, "rejected save left the file intact");
    // Unrelated keys written by another version survive.
    {
      QFile f(file);
      check(f.open(QIODevice::WriteOnly), "rewrite fixture");
      f.write("{\"ui_opacity_percent\":40,\"future_key\":\"kept\"}\n");
    }
    check(saveOpacity(file, 100), "save over existing file");
    {
      QFile f(file);
      check(f.open(QIODevice::ReadOnly), "read back");
      const auto root = QJsonDocument::fromJson(f.readAll()).object();
      check(root.value("ui_opacity_percent").toInt() == 100, "value updated");
      check(root.value("future_key").toString() == "kept",
            "unrelated key preserved");
      check(root.value("ui_opacity_percent").isDouble(),
            "stored as an integer number, as the installer validates");
    }
    // A symbolic link in place of the file is never followed.
    const QString target = tmp.path() + "/elsewhere.json", link = tmp.path() + "/link/appearance.json";
    check(QDir().mkpath(tmp.path() + "/link"), "link directory");
    {
      QFile f(target);
      check(f.open(QIODevice::WriteOnly), "link target");
      f.write("{}");
    }
    check(QFile::link(target, link), "create symlink");
    check(!saveOpacity(link, 50), "symlink is refused");
    {
      QFile f(target);
      check(f.open(QIODevice::ReadOnly) && f.readAll() == "{}",
            "symlink target untouched");
    }
    check(alphaFor(100) == 1.0 && alphaFor(0) == 0.0 && alphaFor(50) == 0.5 &&
              alphaFor(250) == 1.0,
          "alpha mapping");
    std::cout << "gtk settings: ok\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "gtk settings: " << e.what() << '\n';
    return 1;
  }
}
