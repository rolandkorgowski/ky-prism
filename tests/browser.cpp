#define main prismApplicationMain
#include "../src/main.cpp"
#undef main
#include <QtTest/QTest>
#include <source_location>
#include <stdexcept>
static void check(bool okay, const char *message) {
  if (!okay)
    throw std::runtime_error(message);
}
static void
waitFor(std::function<bool()> done,
        std::source_location loc = std::source_location::current()) {
  QElapsedTimer timer;
  timer.start();
  while (!done() && timer.elapsed() < 7000) {
    QCoreApplication::processEvents();
    QThread::msleep(2);
  }
  if (!done())
    throw std::runtime_error("browser timeout at " +
                             std::to_string(loc.line()));
}
int main(int argc, char **argv) {
  QTemporaryDir isolated;
  qputenv("PRISM_THUMB_CACHE_DIR",(isolated.path()+"/thumbcache").toUtf8());
  qputenv("XDG_CONFIG_HOME", (isolated.path() + "/config").toUtf8());
  qputenv("XDG_DATA_HOME", (isolated.path() + "/data").toUtf8());
  QApplication app(argc, argv);
  app.setStyle("Fusion");
  if (VIPS_INIT(argv[0]))
    return 2;
  vips_cache_set_max(0);
  int rc = 0;
  try {
    auto folder = isolated.path() + "/Zażółć gęślą [#] $(literal)";
    check(QDir().mkpath(folder), "Unicode folder");
    const auto first =
                   folder + "/Zdjęcie " + QString(80, QChar(0x17c)) + ".jpg",
               second = folder + "/zdjęcie 2.jpg";
    auto image =
        vips::VImage::black(1200, 800, vips::VImage::option()->set("bands", 3))
            .copy(vips::VImage::option()->set("interpretation",
                                              VIPS_INTERPRETATION_sRGB));
    image.jpegsave(first.toUtf8().constData());
    image.new_from_image({200, 100, 20}).jpegsave(second.toUtf8().constData());
    Window w({});
    w.show();
    w.activateWindow();
    w.open(first);
    auto view = w.findChild<View *>("photoView");
    auto right = w.findChild<View *>("compareView");
    auto tabs = w.findChild<QTabBar *>("photoTabs");
    auto files = w.findChild<QTreeView *>("fileTree");
    auto proxy = static_cast<LibraryProxy *>(files->model());
    auto model = static_cast<QFileSystemModel *>(proxy->sourceModel());
    waitFor([&] {
      return !view->busy() && view->imageSize() == QSize(1200, 800) &&
             files->model()->rowCount(files->rootIndex()) == 2;
    });
    check(w.windowTitle().startsWith(QFileInfo(first).fileName()),
          "Unicode title");
    check(w.findChild<QPushButton *>("copyFiles")->icon().isNull() == false,
          "copy icon");
    check(!tabs->isVisible(), "single tab strip wastes image area");
    check(files->isColumnHidden(2) && files->isColumnHidden(3),
          "replacing header restored hidden columns");
    check(files->header()->sectionViewportPosition(1) +
                  files->header()->sectionSize(1) <=
              files->viewport()->width() + 1,
          "size column is outside sidebar");
    QList<QPushButton *> sort;
    for (int i = 0; i < 4; ++i) {
      auto button = w.findChild<QPushButton *>("sortBy" + QString::number(i));
      check(button, "missing direct sort control");
      sort.append(button);
    }
    sort.at(2)->click();
    check(proxy->sortColumn() == 2, "direct sort action");
    check(w.findChildren<QMenu *>().isEmpty(), "unexpected popup menu");
    check(w.findChild<QPushButton *>("findDuplicates"),
          "direct duplicate button");
    auto grid = w.findChild<QListView *>("thumbnailGrid");
    auto cache = w.findChild<ThumbnailCache *>();
    check(grid && cache && !grid->isVisible() && !cache->busy(),
          "hidden thumbnails consume CPU");
    check(grid->selectionModel() == files->selectionModel(),
          "list/grid selection split");
    w.findChild<QPushButton *>("viewThumbnails")->click();
    waitFor([&] {
      return grid->isVisible() && !cache->busy() && cache->cachedKiB() > 0;
    });
    auto divider = w.findChild<QSplitter *>("librarySplitter");
    check(divider, "library splitter missing");
    check(!divider->opaqueResize(), "splitter resizes the GPU canvas per pixel");
    for (const auto &name : {first, second})
      cache->request(QFileInfo(name), 128);
    waitFor([&] { return !cache->busy(); });
    // Both files are already decoded; resizing must not restart the decoder.
    const int initialKiB = cache->cachedKiB();
    const auto originalGeometry = w.geometry();
    for (int i = 0; i < 180; ++i) {
      int width = 270 + (i % 90) * 5;
      divider->setSizes({width, 1200 - width});
      QCoreApplication::processEvents();
      check(!cache->request(QFileInfo(first), width - 22).isNull(),
            "splitter resize blinked a loaded thumbnail");
      check(!cache->busy(), "splitter resize queued a redundant decode");
    }
    QTest::qWait(80);
    const auto settledCell = grid->gridSize();
    QTest::qWait(80);
    check(grid->gridSize() == settledCell && cache->cachedKiB() == initialKiB,
          "thumbnail layout/cache did not settle after resize");
    check(w.geometry() == originalGeometry && !QWidget::mouseGrabber(),
          "splitter changed the window or retained a mouse grab");
    divider->setSizes({280, 920});
    QTest::qWait(30);
    grid->selectionModel()->clearSelection();
    for (const auto &name : {first, second})
      grid->selectionModel()->select(proxy->mapFromSource(model->index(name)),
                                     QItemSelectionModel::Select);
    check(w.selectedPhotos().size() == 2, "grid multiselection copy sources");
    if (qEnvironmentVariableIsSet("PRISM_LIST_SNAPSHOT")) {
      w.findChild<QPushButton *>("viewList")->click();
      QCoreApplication::processEvents();
      w.grab().save(qEnvironmentVariable("PRISM_LIST_SNAPSHOT"));
      w.findChild<QPushButton *>("viewThumbnails")->click();
    }
    for (int width : {180, 285, 420, 800}) {
      grid->resize(width, 500);
      QCoreApplication::processEvents();
      check(grid->gridSize().width() >= 100 && grid->gridSize().height() == 160,
            "thumbnail grid alignment");
    }
    w.findChild<QPushButton *>("viewList")->click();
    waitFor([&] { return !cache->busy(); });
    check(files->isVisible(), "list mode restore");
    for (int i = 0; i < 4; ++i) {
      sort.at(i)->click();
      QCoreApplication::processEvents();
      check(proxy->sortColumn() == i, "sort mode");
    }
    w.findChild<QPushButton *>("sortDirection")->click();
    check(proxy->sortOrder() == Qt::DescendingOrder, "descending sort");
    files->selectionModel()->clearSelection();
    for (const auto &name : {first, second})
      files->selectionModel()->select(proxy->mapFromSource(model->index(name)),
                                      QItemSelectionModel::Select |
                                          QItemSelectionModel::Rows);
    check(w.selectedPhotos().size() == 2, "multi selection");
    w.toggleCompare();
    waitFor(
        [&] { return right->isVisible() && !right->busy() && !view->busy(); });
    check(right->imageSize() == QSize(1200, 800), "comparison decode");
    check(std::min(view->width(), right->width()) > 350,
          "new comparison is not balanced");
    QCoreApplication::processEvents();
    w.activateWindow();
    waitFor([&] { return w.isActiveWindow(); });
    auto leftBefore = view->displayedImageRect();
    right->setFocus();
    QCoreApplication::processEvents();
    QPushButton *original = nullptr;
    for (auto b : w.findChildren<QPushButton *>())
      if (b->objectName() == "actualPhoto")
        original = b;
    check(original, "original toolbar action");
    original->setFocus();
    original->click();
    check(w.activeView() == right, "toolbar lost active comparison pane");
    check(std::abs(right->displayedImageRect().width() -
                   1200.0 / right->devicePixelRatioF()) < .01,
          "right pane Original scale");
    check(view->displayedImageRect() == leftBefore,
          "toolbar focus resized the other photo");
    right->fit();
    auto rect = right->displayedImageRect();
    right->actual();
    right->pan({-64, 0});
    check(right->displayedImageRect() != rect, "independent comparison pan");
    w.newTab();
    check(tabs->isVisible(), "multiple tabs strip hidden");
    check(tabs->count() == 2 && !right->isVisible(),
          "new tab retained active renderer");
    w.open(second);
    waitFor([&] { return !view->busy(); });
    tabs->setCurrentIndex(0);
    waitFor(
        [&] { return !view->busy() && !right->busy() && right->isVisible(); });
    check(w.windowTitle().startsWith(QFileInfo(first).fileName()),
          "tab state restore");
    w.toggleCompare();
    waitFor([&] { return !right->busy(); });
    check(!right->isVisible() && right->imageSize().isEmpty(),
          "comparison did not release memory");
    for (int i = 0; i < 60; ++i) {
      w.newTab();
      w.open(i % 2 ? first : second);
      w.closeTab(tabs->currentIndex());
    }
    waitFor([&] { return !view->busy(); });
    check(tabs->count() == 2, "rapid tabs leaked tabs");
    for (int i = 0; i < 40; ++i)
      w.newTab();
    check(tabs->count() == 24, "unbounded tab count");
    for (int i = 23; i >= 1; --i)
      w.closeTab(i);
    check(tabs->count() == 1 && !tabs->isVisible(), "single tab strip restore");
    w.open(first);
    w.showComparison(second);
    waitFor([&] { return !view->busy() && !right->busy(); });
    w.findChild<QPushButton *>("closePrimaryPhoto")->click();
    waitFor([&] { return !view->busy(); });
    check(!view->isVisible() && right->isVisible() &&
              view->imageSize().isEmpty() && w.activePhoto() == second,
          "independent close left pane");
    check(w.selectedPhotos().size() >= 1, "right-only copy source");
    w.findChild<QPushButton *>("closeComparisonPhoto")->click();
    waitFor([&] { return !right->busy(); });
    check(w.findChild<QWidget *>("welcome")->isVisible() &&
              right->imageSize().isEmpty(),
          "closing both panes retained image");
    w.open(first);
    w.showComparison(second);
    waitFor([&] { return !view->busy() && !right->busy(); });
    w.findChild<QPushButton *>("closeComparisonPhoto")->click();
    waitFor([&] { return !right->busy(); });
    check(view->isVisible() && !right->isVisible() &&
              view->imageSize() == QSize(1200, 800) && w.activePhoto() == first,
          "independent close right pane");
    waitFor([&] {
      return !w.findChild<QLabel *>("diskSpace")
                  ->text()
                  .contains("sprawdzanie");
    });
    check(w.findChild<QLabel *>("diskSpace")->text().contains("GiB"),
          "free disk capacity");
    check(!validLocalPath(QString("bad") + QChar::Null + ".jpg"),
          "NUL accepted");
    check(!validLocalPath(QString(8200, 'x')), "oversized path accepted");
    auto usb = usbEntries(
        R"({"blockdevices":[{"name":"/dev/nvme0n1","tran":"nvme","rm":false,"children":[{"name":"/dev/nvme0n1p1","fstype":"ext4","mountpoints":["/home"]}]},{"name":"/dev/sdb","tran":"usb","children":[{"name":"/dev/sdb1","fstype":"vfat","label":"Zażółć","mountpoints":["/media/Zażółć"]},{"name":"/dev/sdb2","fstype":"ext4","mountpoints":[null]}]}]})");
    check(usb.size() == 2 && usb.first().mount == "/media/Zażółć" &&
              usb.last().mount.isEmpty(),
          "USB filtering/mount detection");
    check(usbEntries("not json").isEmpty(), "malformed USB input");
    if (qEnvironmentVariableIsSet("PRISM_BROWSER_SNAPSHOT")) {
      w.open(first);
      w.showComparison(second);
      waitFor([&] { return !view->busy() && !right->busy(); });
      w.findChild<QPushButton *>("viewThumbnails")->click();
      waitFor([&] { return !cache->busy(); });
      QCoreApplication::processEvents();
      check(std::min(view->width(), right->width()) > 350,
            "reopened comparison collapsed a pane");
      w.grab().save(qEnvironmentVariable("PRISM_BROWSER_SNAPSHOT"));
    }
    if (qEnvironmentVariableIsSet("PRISM_DIALOG_SNAPSHOTS")) {
      auto output = qEnvironmentVariable("PRISM_DIALOG_SNAPSHOTS");
      for (bool duplicates : {false, true}) {
        auto dialog = new FileDialog(first, duplicates, folder, &w);
        if (!duplicates)
          dialog->setSources({first, second});
        dialog->show();
        QTest::qWait(230);
        check(dialog->grab().save(
                  output + (duplicates ? "/duplicates.png" : "/copy.png")),
              "save dialog capture");
        w.applyTheme("background = \"#18212e\"\nforeground = "
                     "\"#edf2ff\"\naccent = \"#94bcf4\"");
        QCoreApplication::processEvents();
        check(dialog->palette().color(QPalette::WindowText) ==
                  QColor("#edf2ff"),
              "open dialog theme did not update");
        check(dialog->grab().save(output + (duplicates ? "/duplicates-dark.png"
                                                       : "/copy-dark.png")),
              "save dark dialog capture");
        dialog->close();
        QCoreApplication::processEvents();
        w.applyTheme();
      }
      w.applyTheme("background = \"#18212e\"\nforeground = \"#edf2ff\"\naccent "
                   "= \"#94bcf4\"");
      QCoreApplication::processEvents();
      check(w.grab().save(output + "/browser-dark.png"),
            "save dark browser capture");
      auto loading = static_cast<LoadingGlass *>(
          view->findChild<QWidget *>("loadingHourglass"));
      loading->setLoading(true);
      QTest::qWait(240);
      check(w.grab().save(output + "/loading.png"), "save loader capture");
      loading->setLoading(false);
      w.applyTheme();
    }
    w.close();
    QCoreApplication::processEvents();
    std::cout << "PASS: Unicode/long names, sorting, multiselection, tabs "
                 "limit/rapid switching, comparison lifecycle, free space, USB "
                 "parsing, input validation\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    rc = 1;
  }
  vips_shutdown();
  return rc;
}
