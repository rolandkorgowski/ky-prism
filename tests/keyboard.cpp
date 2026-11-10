#define main prismApplicationMain
#include "../src/main.cpp"
#undef main
#include <QClipboard>
#include <QMessageBox>
#include <QSet>
#include <QtTest/QTest>
#include <source_location>
#include <stdexcept>
static void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
static void
waitFor(std::function<bool()> done,
        std::source_location where = std::source_location::current()) {
  QElapsedTimer timer;
  timer.start();
  while (!done() && timer.elapsed() < 6000) {
    QCoreApplication::processEvents();
    QThread::msleep(2);
  }
  if (!done()) {
    QString focus = QApplication::focusWidget()
                        ? QApplication::focusWidget()->objectName()
                        : "none";
    throw std::runtime_error("keyboard timeout at line " +
                             std::to_string(where.line()) +
                             ", focus=" + focus.toStdString());
  }
}
static void key(QWidget *target, Qt::Key code,
                Qt::KeyboardModifiers modifiers = {}) {
  QTest::keyClick(target, code, modifiers);
  QCoreApplication::processEvents();
}
static void choosePathWithKeys(QFileDialog *picker, const QString &name) {
  picker->activateWindow();
  QTest::qWait(10);
  auto field = picker->findChild<QLineEdit *>("fileNameEdit");
  check(field, "file chooser name field");
  for (int i = 0; i < 45 && !field->hasFocus(); ++i)
    key(QApplication::focusWidget() ? QApplication::focusWidget() : picker,
        Qt::Key_Tab);
  check(field->hasFocus(), "file chooser field is unreachable by Tab");
  key(field, Qt::Key_A, Qt::ControlModifier);
  QTest::keyClicks(field, name);
  key(field, Qt::Key_Return);
}
int main(int argc, char **argv) {
  QTemporaryDir isolated;
  qputenv("PRISM_THUMB_CACHE_DIR",(isolated.path()+"/thumbcache").toUtf8());
  qputenv("XDG_CONFIG_HOME", (isolated.path() + "/config").toUtf8());
  qputenv("XDG_CACHE_HOME", (isolated.path() + "/cache").toUtf8());
  QApplication app(argc, argv);
  app.setStyle("Fusion");
  app.setApplicationName("ky-prism");
  app.setApplicationVersion("0.12.1");
  if (VIPS_INIT(argv[0]))
    return 2;
  int result = 0;
  try {
    QTemporaryDir tmp;
    qputenv("XDG_DATA_HOME", (tmp.path() + "/data").toUtf8());
    QDir().mkpath(tmp.path() + "/images");
    QString photo = tmp.path() + "/images/keyboard.jpg",
            other = tmp.path() + "/images/z-next.jpg";
    auto image =
        vips::VImage::black(1800, 1200, vips::VImage::option()->set("bands", 3))
            .copy(vips::VImage::option()->set("interpretation",
                                              VIPS_INTERPRETATION_sRGB));
    image.jpegsave(photo.toUtf8().constData());
    image.jpegsave(other.toUtf8().constData());
    Window window({});
    window.show();
    window.activateWindow();
    QTest::qWait(30);
    window.open(photo);
    auto view = window.findChild<View *>("photoView");
    auto path = window.findChild<QLineEdit *>("folderPath");
    auto files = window.findChild<QTreeView *>("fileTree");
    auto heart = window.findChild<HeartButton *>();
    auto star = window.findChild<QPushButton *>("bookmarkStar");
    check(view && path && files && heart && star, "keyboard controls");
    auto active = [&] {
      window.activateWindow();
      QTest::qWait(10);
    };
    waitFor([&] {
      return !view->busy() && view->imageSize() == QSize(1800, 1200) &&
             !window.findChild<Favorites *>()->busy();
    });
    check(star->parentWidget() != window.findChild<QWidget *>("browser"),
          "star remained in sidebar");
    check(window.findChild<QHBoxLayout *>("photoToolbar")->indexOf(star) ==
              window.findChild<QHBoxLayout *>("photoToolbar")->indexOf(heart) +
                  1,
          "star is not adjacent to heart");
    active();
    key(&window, Qt::Key_L, Qt::ControlModifier);
    check(path->hasFocus(), "Ctrl+L did not focus directory");
    auto before = view->displayedImageRect();
    QString title = window.windowTitle();
    QTest::keyClicks(path, "f1 name");
    key(path, Qt::Key_Left);
    check(path->text() == "f1 name" && window.windowTitle() == title &&
              view->displayedImageRect() == before,
          "image shortcuts stole text editing keys");
    key(path, Qt::Key_A, Qt::ControlModifier);
    key(path, Qt::Key_C, Qt::ControlModifier);
    check(QApplication::clipboard()->text() == "f1 name" &&
              !window.findChild<FileDialog *>(),
          "Ctrl+C must copy text, not a file");
    key(path, Qt::Key_D, Qt::ControlModifier);
    check(star->isChecked() &&
              FolderBookmarks::read().contains(tmp.path() + "/images"),
          "keyboard bookmark toggle");
    key(path, Qt::Key_B, Qt::ControlModifier | Qt::ShiftModifier);
    auto bookmarks = window.findChild<BookmarksDialog *>();
    check(bookmarks && bookmarks->isVisible(), "keyboard bookmark browser");
    auto filter = bookmarks->findChild<QLineEdit *>("bookmarkFilter");
    auto table = bookmarks->findChild<QTableView *>("bookmarkTable");
    bookmarks->activateWindow();
    QTest::qWait(10);
    key(bookmarks, Qt::Key_F, Qt::ControlModifier);
    QTest::keyClicks(filter, "missing");
    check(table->model()->rowCount() == 0, "bookmark keyboard filter");
    key(filter, Qt::Key_A, Qt::ControlModifier);
    key(filter, Qt::Key_Backspace);
    key(bookmarks, Qt::Key_2, Qt::AltModifier);
    check(table->horizontalHeader()->sortIndicatorSection() == 1,
          "keyboard bookmark sort");
    key(filter, Qt::Key_Tab);
    check(table->hasFocus(), "Tab did not reach bookmark table");
    table->selectRow(0);
    key(table, Qt::Key_Return);
    check(!bookmarks->isVisible(), "Enter did not open bookmark");
    active();
    key(&window, Qt::Key_B, Qt::ControlModifier);
    check(!window.findChild<QWidget *>("browser")->isVisible(),
          "keyboard sidebar hide");
    key(&window, Qt::Key_L, Qt::ControlModifier);
    check(path->hasFocus() &&
              window.findChild<QWidget *>("browser")->isVisible(),
          "keyboard sidebar reveal");
    key(&window, Qt::Key_B, Qt::ControlModifier | Qt::ShiftModifier);
    bookmarks->activateWindow();
    QTest::qWait(10);
    key(filter, Qt::Key_Tab);
    table->selectRow(0);
    key(table, Qt::Key_Delete);
    check(FolderBookmarks::read().isEmpty() && !star->isChecked() &&
              QFileInfo(tmp.path() + "/images").isDir(),
          "keyboard bookmark removal affected folder or left star selected");
    key(bookmarks, Qt::Key_W, Qt::ControlModifier);
    active();
    QString nested = tmp.path() + "/images/folder";
    QDir().mkpath(nested);
    key(&window, Qt::Key_L, Qt::ControlModifier);
    QTest::keyClicks(path, nested);
    key(path, Qt::Key_Return);
    check(path->text() == nested, "keyboard directory path entry");
    key(path, Qt::Key_Up, Qt::AltModifier);
    check(path->text() == tmp.path() + "/images", "keyboard parent directory");
    key(path, Qt::Key_Left, Qt::AltModifier);
    check(path->text() == nested, "keyboard directory back");
    key(path, Qt::Key_Right, Qt::AltModifier);
    check(path->text() == tmp.path() + "/images", "keyboard directory forward");
    active();
    key(&window, Qt::Key_F1, Qt::ControlModifier);
    auto about = window.findChild<AboutDialog *>();
    check(about && about->isVisible() && about->currentPage() == 0 &&
              about->animating(),
          "keyboard About / animation");
    if (qEnvironmentVariableIsSet("PRISM_ABOUT_SNAPSHOT")) {
      QTest::qWait(700);
      about->grab().save(qEnvironmentVariable("PRISM_ABOUT_SNAPSHOT"));
    }
    check(about->findChild<QLabel *>("authorName")->text() ==
              "Krzysztof Kylen Lenartowicz",
          "copyright author");
    about->activateWindow();
    QTest::qWait(10);
    key(about, Qt::Key_2, Qt::AltModifier);
    check(about->currentPage() == 1 && !about->animating(),
          "hidden About animation did not stop");
    key(about, Qt::Key_W, Qt::ControlModifier);
    check(!about->isVisible() && !about->animating(), "keyboard About close");
    active();
    key(&window, Qt::Key_F1);
    check(about->isVisible() && about->currentPage() == 1, "F1 keyboard help");
    about->activateWindow();
    QTest::qWait(10);
    key(about, Qt::Key_Escape);
    check(!about->isVisible(), "Esc help close");
    active();
    auto aboutButton = window.findChild<QPushButton *>("about");
    aboutButton->setFocus();
    key(aboutButton, Qt::Key_Return);
    check(about->isVisible(), "Enter did not activate focused button");
    about->activateWindow();
    QTest::qWait(10);
    key(about, Qt::Key_Escape);
    active();
    view->setFocus();
    key(view, Qt::Key_1);
    check(std::abs(view->displayedImageRect().width() -
                   1800.0 / view->devicePixelRatioF()) < 0.01,
          "keyboard 1:1");
    before = view->displayedImageRect();
    key(view, Qt::Key_Right, Qt::ShiftModifier);
    check(view->displayedImageRect().left() == before.left() - 64,
          "keyboard image pan");
    key(view, Qt::Key_Right, Qt::ShiftModifier | Qt::ControlModifier);
    check(view->displayedImageRect().left() == before.left() - 320,
          "keyboard large pan");
    key(view, Qt::Key_F);
    check(view->displayedImageRect().width() != before.width(), "keyboard fit");
    waitFor([&] { return !view->busy(); });
    key(view, Qt::Key_Plus);
    waitFor([&] { return !view->busy(); });
    check(view->displayedImageRect().width() > 100, "keyboard zoom");
    auto enlarged = view->displayedImageRect().width();
    key(view, Qt::Key_Minus, Qt::ControlModifier);
    waitFor([&] { return !view->busy(); });
    check(view->displayedImageRect().width() < enlarged, "keyboard zoom out");
    key(view, Qt::Key_PageDown);
    waitFor([&] {
      return window.windowTitle().startsWith("z-next.jpg") && !view->busy();
    });
    key(view, Qt::Key_PageUp);
    waitFor([&] {
      return window.windowTitle().startsWith("keyboard.jpg") && !view->busy();
    });
    key(view, Qt::Key_I, Qt::ControlModifier);
    check(window.findChild<QPlainTextEdit *>()->isVisible(),
          "keyboard EXIF panel");
    key(view, Qt::Key_F6);
    check(files->hasFocus(), "F6 file list");
    key(files, Qt::Key_F6);
    check(path->hasFocus(), "F6 path");
    key(path, Qt::Key_F6, Qt::ShiftModifier);
    check(files->hasFocus(), "Shift+F6 backwards");
    auto proxy = static_cast<LibraryProxy *>(files->model());
    auto model = static_cast<QFileSystemModel *>(proxy->sourceModel());
    auto entry = proxy->mapFromSource(model->index(other));
    check(entry.isValid(), "file keyboard index");
    files->setCurrentIndex(entry);
    key(files, Qt::Key_Return);
    waitFor([&] {
      return window.windowTitle().startsWith("z-next.jpg") && !view->busy();
    });
    active();
    key(&window, Qt::Key_H, Qt::ControlModifier | Qt::ShiftModifier);
    auto favorites = window.findChild<Favorites *>();
    waitFor([&] { return !favorites->busy() && favorites->contains(other); });
    key(&window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    auto favoriteDialog = window.findChild<FavoritesDialog *>();
    check(favoriteDialog && favoriteDialog->isVisible(),
          "keyboard favorite browser");
    auto favoriteTable =
        favoriteDialog->findChild<QTableView *>("favoriteTable");
    favoriteDialog->activateWindow();
    QTest::qWait(10);
    key(favoriteDialog, Qt::Key_F, Qt::ControlModifier);
    auto favoriteFilter =
        favoriteDialog->findChild<QLineEdit *>("favoriteFilter");
    check(favoriteFilter->hasFocus(), "favorite Ctrl+F");
    key(favoriteDialog, Qt::Key_4, Qt::AltModifier);
    check(favoriteTable->horizontalHeader()->sortIndicatorSection() == 3,
          "keyboard favorite sort");
    key(favoriteFilter, Qt::Key_Tab);
    favoriteTable->selectRow(0);
    key(favoriteTable, Qt::Key_Return);
    check(!favoriteDialog->isVisible(), "Enter did not open favorite");
    active();
    key(&window, Qt::Key_H, Qt::ControlModifier | Qt::ShiftModifier);
    waitFor([&] { return !favorites->busy() && !favorites->contains(other); });
    check(!heart->isSelected() && !heart->animating(),
          "keyboard favorite removal / animation stop");
    active();
    key(&window, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
    auto copy = window.findChild<FileDialog *>();
    check(copy && copy->isVisible(), "keyboard copy dialog");
    copy->activateWindow();
    QTest::qWait(10);
    const QString copyTarget = tmp.path() + "/keyboard-copy.jpg";
    bool pickerOpened = false;
    QTimer pickerPoll;
    pickerPoll.setInterval(5);
    QObject::connect(&pickerPoll, &QTimer::timeout, [&] {
      for (auto widget : QApplication::topLevelWidgets())
        if (auto picker = qobject_cast<QFileDialog *>(widget);
            picker && picker->isVisible()) {
          pickerOpened = true;
          pickerPoll.stop();
          choosePathWithKeys(picker, copyTarget);
        }
    });
    pickerPoll.start();
    key(copy, Qt::Key_W, Qt::AltModifier);
    waitFor([&] { return pickerOpened; });
    copy->activateWindow();
    QTest::qWait(10);
    key(copy, Qt::Key_Z, Qt::AltModifier);
    waitFor([&] {
      return QFileInfo::exists(copyTarget) &&
             copy->findChild<QPushButton *>("fileAction")->isEnabled();
    });
    check(QFileInfo(copyTarget).size() == QFileInfo(other).size(),
          "keyboard file copy");
    key(copy, Qt::Key_W, Qt::ControlModifier);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(window.findChild<FileDialog *>() == nullptr,
          "copy keyboard cancellation");
    active();
    key(&window, Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier);
    auto duplicates = window.findChild<FileDialog *>();
    check(duplicates && duplicates->isVisible(), "keyboard duplicate dialog");
    QString duplicateRoot = tmp.path() + "/duplicates";
    QDir().mkpath(duplicateRoot + "/a");
    QDir().mkpath(duplicateRoot + "/b");
    QString duplicateA = duplicateRoot + "/a/same.jpg",
            duplicateB = duplicateRoot + "/b/same.jpg";
    check(QFile::copy(other, duplicateA) && QFile::copy(other, duplicateB),
          "duplicate keyboard fixtures");
    duplicates->activateWindow();
    QTest::qWait(10);
    bool directoryChosen = false;
    QTimer directoryPoll;
    directoryPoll.setInterval(5);
    QObject::connect(&directoryPoll, &QTimer::timeout, [&] {
      for (auto widget : QApplication::topLevelWidgets())
        if (auto picker = qobject_cast<QFileDialog *>(widget);
            picker && picker->isVisible()) {
          directoryChosen = true;
          directoryPoll.stop();
          choosePathWithKeys(picker, duplicateRoot);
        }
    });
    directoryPoll.start();
    key(duplicates, Qt::Key_W, Qt::AltModifier);
    waitFor([&] { return directoryChosen; });
    auto duplicateTree = duplicates->findChild<QTreeView *>();
    auto choose = duplicates->findChild<QPushButton *>("chooseDestination");
    waitFor([&] {
      return choose->isEnabled() && duplicateTree->model()->rowCount() == 1;
    });
    duplicates->activateWindow();
    QTest::qWait(10);
    for (int i = 0; i < 35 && !duplicateTree->hasFocus(); ++i)
      key(QApplication::focusWidget() ? QApplication::focusWidget()
                                      : duplicates,
          Qt::Key_Tab);
    check(duplicateTree->hasFocus(), "duplicate tree unreachable by Tab");
    key(duplicateTree, Qt::Key_Home);
    key(duplicateTree, Qt::Key_Right);
    key(duplicateTree, Qt::Key_Down);
    check(duplicateTree->currentIndex().parent().isValid(),
          "keyboard duplicate path selection");
    const QString selectedPath =
        duplicateTree->currentIndex().data().toString();
    bool cancelled = false;
    QTimer confirmationPoll;
    confirmationPoll.setInterval(5);
    QObject::connect(&confirmationPoll, &QTimer::timeout, [&] {
      for (auto widget : QApplication::topLevelWidgets())
        if (auto box = qobject_cast<QMessageBox *>(widget);
            box && box->isVisible()) {
          cancelled = true;
          confirmationPoll.stop();
          box->activateWindow();
          QTest::qWait(10);
          key(box, Qt::Key_Escape);
        }
    });
    confirmationPoll.start();
    key(duplicates, Qt::Key_K, Qt::AltModifier);
    waitFor([&] { return cancelled; });
    check(QFileInfo::exists(duplicateA) && QFileInfo::exists(duplicateB),
          "keyboard cancel removed a duplicate");
    duplicates->activateWindow();
    QTest::qWait(10);
    bool confirmed = false;
    QTimer yesPoll;
    yesPoll.setInterval(5);
    QObject::connect(&yesPoll, &QTimer::timeout, [&] {
      for (auto widget : QApplication::topLevelWidgets())
        if (auto box = qobject_cast<QMessageBox *>(widget);
            box && box->isVisible()) {
          yesPoll.stop();
          box->activateWindow();
          QTest::qWait(10);
          auto yes = box->button(QMessageBox::Yes);
          for (int i = 0; i < 12 && !yes->hasFocus(); ++i)
            key(QApplication::focusWidget() ? QApplication::focusWidget() : box,
                Qt::Key_Tab);
          check(yes->hasFocus(), "confirmation unreachable by Tab");
          confirmed = true;
          key(yes, Qt::Key_Return);
        }
    });
    yesPoll.start();
    key(duplicates, Qt::Key_K, Qt::AltModifier);
    waitFor([&] {
      return confirmed && choose->isEnabled() &&
             duplicateTree->model()->rowCount() == 0;
    });
    check(!QFileInfo::exists(selectedPath) &&
              (QFileInfo::exists(duplicateA) || QFileInfo::exists(duplicateB)),
          "keyboard trash confirmation");
    duplicates->activateWindow();
    QTest::qWait(10);
    key(duplicates, Qt::Key_Escape);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(!window.findChild<FileDialog *>(), "duplicate keyboard close");
    active();
    key(&window, Qt::Key_M, Qt::ControlModifier);
    check(window.isMaximized(), "keyboard maximize");
    key(&window, Qt::Key_M, Qt::ControlModifier);
    check(!window.isMaximized(), "keyboard restore");
    key(&window, Qt::Key_F11);
    check(window.isFullScreen(), "keyboard fullscreen");
    key(&window, Qt::Key_Escape);
    check(!window.isFullScreen(), "keyboard fullscreen escape");
    // Every visible main-window button must be in the Tab chain, including
    // custom heart.
    view->setFocus();
    QSet<QWidget *> reached;
    for (int i = 0; i < 180; ++i) {
      auto focus = QApplication::focusWidget();
      if (focus)
        reached.insert(focus);
      key(focus ? focus : &window, Qt::Key_Tab);
    }
    for (auto button : window.findChildren<QAbstractButton *>())
      if (button->window() == &window && button->isVisible() &&
          button->isEnabled())
        check(reached.contains(button),
              qPrintable("Unreachable button: " + button->text()));
    active();
    key(&window, Qt::Key_F1, Qt::ControlModifier);
    check(about->isVisible(), "about reopen before main close");
    active();
    key(&window, Qt::Key_W, Qt::ControlModifier);
    check(!about->isVisible() && !about->animating(),
          "closing main window left About active");
    check(!window.isVisible(), "keyboard application close");
    std::cout << "PASS: real key events, text editing isolation, "
                 "bookmark/favorite browsers, author animation, pan/zoom, file "
                 "dialogs, window controls, full Tab chain\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    result = 1;
  }
  vips_shutdown();
  return result;
}
