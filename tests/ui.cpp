#define main prismApplicationMain
#include "../src/main.cpp"
#undef main
#include <QThread>
#include <cmath>
#include <stdexcept>
static void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  QTemporaryDir config;
  qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
  QApplication app(argc, argv);
  app.setStyle(new MenuGlassStyle);
  if (VIPS_INIT(argv[0]))
    return 2;
  int result = 0;
  try {
    QTemporaryDir userData;
    qputenv("PRISM_THUMB_CACHE_DIR",(userData.path()+"/thumbcache").toUtf8());
    qputenv("XDG_DATA_HOME", userData.path().toUtf8());
    Window window({});
    window.show();
    QCoreApplication::processEvents();
    // Open the actual toolbar action with the complete Window stylesheet.
    // A standalone chooser did not cover inherited application rules.
    QTemporaryDir chooserFolder;
    QDir(chooserFolder.path()).mkdir(QString::fromUtf8("Zdjęcia — zażółć gęślą"));
    window.browse(chooserFolder.path());
    bool chooserOk = false;
    QTimer::singleShot(80, [&] {
      if (auto *chooser = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
        chooserOk = chooser->testAttribute(Qt::WA_TranslucentBackground);
        const auto background = app.palette().color(QPalette::Window);
        for (auto *list : chooser->findChildren<QAbstractItemView *>())
          chooserOk = chooserOk && PrismDialogTheme::contrast(background,list->palette().color(QPalette::Text)) >= 4.5;
        const auto snapshots = qEnvironmentVariable("PRISM_DIALOG_SNAPSHOTS");
        if (!snapshots.isEmpty()) chooser->grab().save(snapshots+"/file-dialog-actual-app.png");
        chooser->reject();
      }
    });
    auto *loadPhoto=window.findChild<QPushButton *>("loadPhoto");
    check(loadPhoto && loadPhoto->text()=="Załaduj zdjęcie", "load button label changed");
    check(loadPhoto->property("prismSymbol").toString()=="addPhoto", "load button uses font fallback glyph");
    loadPhoto->click();
    check(chooserOk && !QApplication::activeModalWidget(), "actual file chooser palette or close failed");
    check(QWidget::mouseGrabber() == nullptr, "file chooser retained mouse grab");
    auto maximize = window.findChild<QPushButton *>("maximize");
    check(maximize, "maximize button missing");
    maximize->click();
    check(window.isMaximized(), "maximize failed");
    window.fullscreen();
    check(window.isFullScreen(), "fullscreen from maximize");
    window.fullscreen();
    check(window.isMaximized(), "restore maximized state");
    maximize->click();
    check(!window.isMaximized() && !window.isFullScreen(),
          "restore normal state");
    QTemporaryDir folders;
    QDir().mkpath(folders.path() + "/a");
    QDir().mkpath(folders.path() + "/b");
    QDir().mkpath(folders.path() + "/c");
    window.browse(folders.path() + "/a");
    window.browse(folders.path() + "/b");
    auto back = window.findChild<QPushButton *>("directoryBack"),
         forward = window.findChild<QPushButton *>("directoryForward");
    check(back && forward && back->isEnabled() && !forward->isEnabled(),
          "history actions");
    back->click();
    check(forward->isEnabled(), "directory back");
    forward->click();
    check(!forward->isEnabled(), "directory forward");
    back->click();
    window.browse(folders.path() + "/c");
    check(!forward->isEnabled(), "branch clears forward history");
    for (int i = 0; i < 100; ++i) {
      back->click();
      forward->click();
    }
    check(!forward->isEnabled(), "rapid history clicks");
    check(window.findChild<QPushButton *>("bookmarkStar") &&
              window.findChild<QPushButton *>("findDuplicates"),
          "bookmarks/file actions missing");
    auto welcome = window.findChild<QWidget *>("welcome");
    check(welcome && welcome->isVisible(), "welcome not visible");
    auto browser = window.findChild<QWidget *>("browser");
    check(browser && browser->isVisible(), "browser missing");
    QPushButton *files = nullptr;
    for (auto b : window.findChildren<QPushButton *>())
      if (b->objectName() == "toggleLibrary")
        files = b;
    check(files && files->isChecked(), "toggle missing");
    files->click();
    check(browser->isHidden() && !files->isChecked(), "hide failed");
    files->click();
    check(!browser->isHidden() && files->isChecked(), "show failed");
    QCoreApplication::processEvents();
    check(window.minimumSizeHint().width() < 1200, "toolbar too wide");
    if (qEnvironmentVariableIsSet("PRISM_UI_SNAPSHOT"))
      window.grab().save(qEnvironmentVariable("PRISM_UI_SNAPSHOT"));
    window.hide();
    QTemporaryDir tmp;
    auto source = tmp.path() + "/window.jpg";
    vips::VImage::black(1600, 900, vips::VImage::option()->set("bands", 3))
        .copy(vips::VImage::option()->set("interpretation",
                                          VIPS_INTERPRETATION_sRGB))
        .jpegsave(source.toUtf8().constData());
    View *view = nullptr;
    for (auto widget : window.findChildren<View *>())
      view = widget;
    check(view, "view missing");
    window.open(source);
    QElapsedTimer wait;
    wait.start();
    while ((view->imageSize().isEmpty() || view->busy()) &&
           wait.elapsed() < 5000) {
      QCoreApplication::processEvents();
      QThread::msleep(2);
    }
    check(view->imageSize() == QSize(1600, 900), "load test photo");
    QSize windowSize = window.size();
    double zoom = 0;
    QObject::connect(view, &View::zoomChanged, [&](double z) { zoom = z; });
    QPushButton *original = nullptr;
    for (auto b : window.findChildren<QPushButton *>())
      if (b->objectName() == "actualPhoto")
        original = b;
    check(original, "original action missing");
    original->click();
    check(window.size() == windowSize && std::abs(zoom - 1.0) < 0.0001,
          "original changed geometry or scale");
    auto favorites = window.findChild<Favorites *>();
    auto heart = window.findChild<HeartButton *>("favoriteHeart");
    check(favorites && heart, "favorites controls");
    auto waitFor = [&](auto done) {
      QElapsedTimer timer;
      timer.start();
      while (!done() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
      }
      check(done(), "favorite UI timeout");
    };
    waitFor([&] { return !favorites->busy(); });
    heart->click();
    waitFor([&] { return !favorites->busy() && favorites->contains(source); });
    check(heart->isSelected(), "heart not red after persistence");
    window.showFavorites();
    auto dialog = window.findChild<FavoritesDialog *>();
    check(dialog, "favorites dialog missing");
    auto table = dialog->findChild<QTableView *>();
    check(table && table->model()->rowCount() == 1, "favorites dialog list");
    dialog->hide();
    heart->click();
    waitFor([&] { return !favorites->busy() && !favorites->contains(source); });
    check(!heart->isSelected(), "favorite removal state");
    // Minimum layout sizes depend on Qt/style/font metrics. A legitimate
    // minimum above 1010 is not a fixed-size window (Qt clamps resize()).
    if (app.arguments().contains("--larger-minimum"))
      window.setMinimumWidth(std::max(1100, window.minimumWidth()));
    window.layout()->activate();
    const auto minimum = window.minimumSize().expandedTo(window.minimumSizeHint());
    const auto small = QSize(1010, 720).expandedTo(minimum);
    const auto large = small + QSize(83, 47);
    std::cout << "resize diagnostic: actual=" << window.width() << 'x' << window.height()
              << " minimum=" << window.minimumWidth() << 'x' << window.minimumHeight()
              << " hint=" << window.minimumSizeHint().width() << 'x' << window.minimumSizeHint().height()
              << " maximum=" << window.maximumWidth() << 'x' << window.maximumHeight() << '\n';
    check(window.maximumWidth() >= large.width() &&
              window.maximumHeight() >= large.height(), "window size range locked");
    window.resize(large);
    check(window.size() == large, "window cannot grow within layout constraints");
    window.resize(small);
    check(window.size() == small, "window cannot shrink within layout constraints");
    window.resize(large);
    check(window.size() == large, "window cannot resize again");
    window.close();
    std::cout << "PASS: welcome, sidebar toggle, toolbar width, original scale "
                 "without resize, window resizable, idle close\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    result = 1;
  }
  vips_shutdown();
  return result;
}
