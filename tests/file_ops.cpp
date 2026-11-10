#include "file_ops.h"
#include "browser_history.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <iostream>
#include <stdexcept>
void check(bool ok, const char *s) {
  if (!ok)
    throw std::runtime_error(s);
}
void write(const QString &path, const QByteArray &data) {
  QFile file(path);
  check(file.open(QIODevice::WriteOnly), "create fixture");
  check(file.write(data) == data.size(), "write fixture");
}
QByteArray read(const QString &path) {
  QFile file(path);
  check(file.open(QIODevice::ReadOnly), "read fixture");
  return file.readAll();
}
template <typename F> void refuses(F fn) {
  bool rejected = false;
  try {
    fn();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "unsafe operation accepted");
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  try {
    QTemporaryDir tmp;
    check(tmp.isValid(), "tempdir");
    qputenv("XDG_DATA_HOME", (tmp.path() + "/data").toUtf8());
    auto token = std::make_shared<std::atomic_bool>(false);
    auto source = tmp.path() + "/photo.jpg", dest = tmp.path() + "/copy.jpg";
    QByteArray bytes(3 * 1024 * 1024, 'q');
    write(source, bytes);
    FileOps::copy(source, dest, token);
    check(read(dest) == bytes && read(source) == bytes, "copy changed bytes");
    refuses([&] { FileOps::copy(source, dest, token); });
    auto batch = tmp.path() + "/batch";
    QDir().mkpath(batch);
    auto polish = tmp.path() + "/Zażółć [#] $(literal).jpg";
    write(polish, "polskie znaki");
    FileOps::copyBatch({source, polish}, batch, token);
    check(read(batch + "/photo.jpg") == bytes &&
              read(batch + "/Zażółć [#] $(literal).jpg") == "polskie znaki",
          "multi-file bytes/names");
    auto collision = tmp.path() + "/collision";
    QDir().mkpath(collision);
    write(collision + "/Zażółć [#] $(literal).jpg", "keep");
    refuses([&] { FileOps::copyBatch({source, polish}, collision, token); });
    check(!QFileInfo::exists(collision + "/photo.jpg"),
          "batch known collision published partial copies");
    refuses([&] {
      FileOps::copy(source + QChar::Null + "suffix", tmp.path() + "/nul.jpg",
                    token);
    });

    token->store(true);
    refuses([&] { FileOps::copy(source, tmp.path() + "/cancel.jpg", token); });
    check(!QFileInfo::exists(tmp.path() + "/cancel.jpg"),
          "cancel published destination");
    token->store(false);
    bool cancelled = false;
    refuses([&] {
      FileOps::copy(source, tmp.path() + "/mid-cancel.jpg", token,
                    [&](auto, auto) {
                      token->store(true);
                      cancelled = true;
                    });
    });
    check(cancelled && !QFileInfo::exists(tmp.path() + "/mid-cancel.jpg"),
          "cancel after write");
    token->store(false);
    auto changed = tmp.path() + "/changed.jpg";
    refuses([&] {
      FileOps::copy(source, changed, token,
                    [&](auto, auto) { write(source, "changed"); });
    });
    check(!QFileInfo::exists(changed), "mutated input published");
    write(source, bytes);
    auto raced = tmp.path() + "/race.jpg";
    refuses([&] {
      FileOps::copy(source, raced, token,
                    [&](auto, auto) { write(raced, "keep existing"); });
    });
    check(read(raced) == "keep existing", "destination race overwrote file");
    auto root = tmp.path() + "/duplicates";
    QDir().mkpath(root + "/a");
    QDir().mkpath(root + "/b");
    QDir().mkpath(root + "/c");
    QDir().mkpath(root + "/d");
    auto a = root + "/a/photo.jpg", b = root + "/b/photo.jpg",
         c = root + "/c/renamed.jpg", d = root + "/d/photo.jpg";
    write(a, "same");
    write(b, "same");
    write(c, "same");
    write(d, "diff");
    QFile::link(a, root + "/symlink.jpg");
    auto groups = FileOps::duplicates(root, token);
    check(groups.size() == 1 && groups[0].paths.size() == 2 &&
              groups[0].paths.contains(a) && groups[0].paths.contains(b),
          "name+size+MD5 grouping");
    write(b, "diff");
    refuses([&] { FileOps::trash(groups.first(), a, token); });
    check(QFileInfo::exists(a), "last original removed");
    write(b, "same");
    refuses([&] { FileOps::trash(groups.first(), c, token); });
    bool mutated = false;
    refuses([&] {
      FileOps::trash(groups.first(), a, token, [&](auto, auto) {
        if (!mutated) {
          write(a, "evil");
          mutated = true;
        }
      });
    });
    check(QFileInfo::exists(a), "changed selected file removed");
    write(a, "same");
    // Destructive path tested ONLY inside our disposable temporary directory.
    FileOps::trash(groups.first(), a, token);
    check(!QFileInfo::exists(a) && read(b) == "same",
          "trash selected copy failed");
    refuses([&] { FileOps::trash(groups.first(), b, token); });
    check(QFileInfo::exists(b), "last remaining copy removed");
    BrowserHistory history;
    history.visit("/a");
    history.visit("/b");
    history.visit("/b");
    history.visit("/c");
    check(history.move(-1) == "/b" && history.forwardAvailable(),
          "back history");
    history.visit("/d");
    check(!history.forwardAvailable() && history.move(-1) == "/b",
          "branch history");
    for (int i = 0; i < 150; ++i)
      history.visit(QString::number(i));
    int count = 1;
    while (history.backAvailable()) {
      history.move(-1);
      ++count;
    }
    check(count == 100, "history memory bound");
    FolderBookmarks::change(root, true);
    FolderBookmarks::change(root, true);
    check(FolderBookmarks::read() == QStringList{root}, "bookmark idempotent");
    FolderBookmarks::change(root, false);
    check(FolderBookmarks::read().isEmpty(), "bookmark removal");
    write(FolderBookmarks::filename(), "not json");
    refuses([&] { FolderBookmarks::change(root, true); });
    check(read(FolderBookmarks::filename()) == "not json",
          "corrupt bookmarks overwritten");
    // Real dialog events: choose destination, rapid repeated clicks, complete
    // copy, close.
    {
      auto dialog = new FileDialog(source, false, root);
      dialog->show();
      auto choose = dialog->findChild<QPushButton *>("chooseDestination");
      auto action = dialog->findChild<QPushButton *>("fileAction");
      check(choose && action && !action->isEnabled(),
            "copy dialog initial state");
      QString uiCopy = tmp.path() + "/UI-copy.jpg";
      QTimer::singleShot(0, [&] {
        for (auto widget : QApplication::topLevelWidgets())
          if (auto picker = qobject_cast<QFileDialog *>(widget)) {
            picker->selectFile(uiCopy);
            QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection);
          }
      });
      choose->click();
      check(action->isEnabled(), "copy destination selection");
      for (int i = 0; i < 100; ++i)
        action->click();
      QElapsedTimer wait;
      wait.start();
      while ((!QFileInfo::exists(uiCopy) || !action->isEnabled()) &&
             wait.elapsed() < 4000) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
      }
      check(QFileInfo::exists(uiCopy) && read(uiCopy) == bytes,
            "copy dialog worker completion");
      dialog->close();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    // Exercise duplicate tree indexes and both paths of the separate
    // confirmation dialog.
    QDir().mkpath(root + "/e");
    QDir().mkpath(root + "/f");
    QString e = root + "/e/photo.jpg", f = root + "/f/photo.jpg";
    write(e, "UI duplicate");
    write(f, "UI duplicate");
    {
      auto dialog = new FileDialog({}, true, root);
      dialog->show();
      auto choose = dialog->findChild<QPushButton *>("chooseDestination");
      auto action = dialog->findChild<QPushButton *>("fileAction");
      QTimer::singleShot(0, [&] {
        for (auto widget : QApplication::topLevelWidgets())
          if (auto picker = qobject_cast<QFileDialog *>(widget)) {
            picker->setDirectory(QFileInfo(root).absolutePath());
            picker->selectFile(root);
            QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection);
          }
      });
      choose->click();
      auto tree = dialog->findChild<QTreeView *>();
      check(tree, "duplicate tree");
      auto waitUntil = [&](auto predicate) {
        QElapsedTimer wait;
        wait.start();
        while (!predicate() && wait.elapsed() < 4000) {
          QCoreApplication::processEvents();
          QThread::msleep(2);
        }
        check(predicate(), "duplicate dialog timeout");
      };
      waitUntil([&] {
        return choose->isEnabled() && tree->model()->rowCount() == 1;
      });
      auto group = tree->model()->index(0, 0);
      check(tree->model()->rowCount(group) == 2, "duplicate model child rows");
      auto child = tree->model()->index(0, 0, group);
      check(child.parent() == group, "duplicate model parent");
      tree->selectionModel()->setCurrentIndex(
          child,
          QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
      check(action->isEnabled(), "duplicate selection action");
      QTimer::singleShot(0, [] {
        for (auto widget : QApplication::topLevelWidgets())
          if (auto box = qobject_cast<QMessageBox *>(widget)) {
            check(box->defaultButton() == box->button(QMessageBox::Cancel),
                  "confirmation must default to cancel");
            box->button(QMessageBox::Cancel)->click();
          }
      });
      action->click();
      check(QFileInfo::exists(e) && QFileInfo::exists(f),
            "cancel confirmation removed file");
      QTimer::singleShot(0, [] {
        for (auto widget : QApplication::topLevelWidgets())
          if (auto box = qobject_cast<QMessageBox *>(widget))
            box->button(QMessageBox::Yes)->click();
      });
      action->click();
      waitUntil([&] {
        return choose->isEnabled() && tree->model()->rowCount() == 0;
      });
      check(QFileInfo::exists(e) != QFileInfo::exists(f),
            "confirmation did not move exactly one duplicate to trash");
      dialog->close();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    // Open/close animations and queued callbacks: no accumulating dialog
    // objects.
    QWidget parent;
    for (int i = 0; i < 30; ++i) {
      auto dialog = new FileDialog(source, i % 2, root, &parent);
      dialog->show();
      QCoreApplication::processEvents();
      if (i == 0) {
        QElapsedTimer fade;
        fade.start();
        while (fade.elapsed() < 220) {
          QCoreApplication::processEvents();
          QThread::msleep(2);
        }
        check(dialog->graphicsEffect() == nullptr,
              "dialog fade did not finish");
      }
      dialog->close();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    check(parent.findChildren<FileDialog *>().isEmpty(),
          "dialog leak on close");
    std::cout
        << "PASS: atomic byte copy, cancel, mutation, destination race, "
           "duplicates, safe trash, history, bookmarks, dialog lifetime\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
