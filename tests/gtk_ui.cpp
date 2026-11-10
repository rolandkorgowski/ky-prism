// GTK front-end behaviour test. Drives the real window through its named
// actions on the session's display; exits 77 (skip) when no display exists.
//   prism-gtk-test                    assertions only
//   prism-gtk-test --screenshots DIR  additionally renders PNGs for review
//   prism-gtk-test --readme-shot PHOTOS OUT.png  main window for the README
#include "gtkbase.h"
#include "dialogs.h"
#include "document.h"
#include "language.h"
#include "library_pane.h"
#include "nvjpeg_backend.h"
#include "photo_view.h"
#include "settings.h"
#include "theme.h"
#include "window.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vips/vips8>

namespace {
QString shots;
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void pump(int milliseconds) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < milliseconds) {
    while (g_main_context_iteration(nullptr, FALSE)) {
    }
    g_usleep(2000);
  }
}
void waitFor(const std::function<bool()> &done, const char *what,
             int timeout = 15000) {
  QElapsedTimer timer;
  timer.start();
  while (!done() && timer.elapsed() < timeout) {
    while (g_main_context_iteration(nullptr, FALSE)) {
    }
    g_usleep(2000);
  }
  if (!done())
    throw std::runtime_error(std::string("timeout: ") + what);
}
// Composite a widget exactly as GTK draws it and return the pixels.
QImage render(GtkWidget *widget) {
  const int w = gtk_widget_get_width(widget), h = gtk_widget_get_height(widget);
  auto *native = gtk_widget_get_native(widget);
  if (w <= 0 || h <= 0 || !native)
    return {};
  g_autoptr(GdkPaintable) paintable = gtk_widget_paintable_new(widget);
  GtkSnapshot *snapshot = gtk_snapshot_new();
  gdk_paintable_snapshot(paintable, snapshot, w, h);
  GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
  if (!node)
    return {};
  graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, float(w), float(h));
  g_autoptr(GdkTexture) texture =
      gsk_renderer_render_texture(gtk_native_get_renderer(native), node, &bounds);
  gsk_render_node_unref(node);
  QImage image(gdk_texture_get_width(texture), gdk_texture_get_height(texture),
               QImage::Format_ARGB32_Premultiplied);
  gdk_texture_download(texture, image.bits(), gsize(image.bytesPerLine()));
  return image;
}
void shot(GtkWidget *widget, const QString &name) {
  if (shots.isEmpty())
    return;
  pump(250);
  const auto image = render(widget);
  if (!image.isNull())
    image.save(shots + "/" + name + ".png");
}
GtkWidget *content(GtkWindow *window) { return gtk_window_get_child(window); }
GtkWidget *findType(GtkWidget *root, GType type) {
  if (G_TYPE_CHECK_INSTANCE_TYPE(root, type))
    return root;
  for (auto *child = gtk_widget_get_first_child(root); child;
       child = gtk_widget_get_next_sibling(child))
    if (auto *found = findType(child, type))
      return found;
  return nullptr;
}
// Newest visible dialog window other than `main`.
GtkWindow *dialogOf(GtkWindow *main) {
  GtkWindow *found = nullptr;
  GList *all = gtk_window_list_toplevels();
  for (GList *l = all; l; l = l->next) {
    auto *w = GTK_WINDOW(l->data);
    if (w != main && gtk_widget_get_visible(GTK_WIDGET(w)) &&
        gtk_widget_has_css_class(GTK_WIDGET(w), "prism-dialog"))
      found = w;
  }
  g_list_free(all);
  return found;
}
void closeDialog(GtkWindow *dialog) {
  if (dialog)
    gtk_window_close(dialog);
  pump(120);
}
void solid(const QString &file, int w, int h, std::vector<double> rgb) {
  auto image = vips::VImage::black(w, h, vips::VImage::option()->set("bands", 3))
                   .new_from_image(rgb)
                   .copy(vips::VImage::option()->set(
                       "interpretation", VIPS_INTERPRETATION_sRGB))
                   .cast(VIPS_FORMAT_UCHAR);
  if (file.endsWith(".png"))
    image.pngsave(file.toUtf8().constData());
  else
    image.jpegsave(file.toUtf8().constData(),
                   vips::VImage::option()->set("Q", 95));
}
bool near(int a, int b, int tolerance) { return std::abs(a - b) <= tolerance; }

void scenario(const QString &photos, const QString &config,
              const QString &work) {
  PrismWindow window;
  bool closedSeen = false;
  window.closed = [&] { closedSeen = true; };
  window.show();
  auto *gtkWindow = window.window();
  waitFor([&] { return gtk_widget_get_mapped(GTK_WIDGET(gtkWindow)); },
          "window mapped");
  check(window.welcomeVisible(), "welcome page at start");
  check(window.titleText() == "ky-prism • Zdjęcia", "initial window title");
  check(window.tabCount() == 1 && window.libraryVisible() &&
            !window.exifVisible() && !window.compareVisible(),
        "initial layout");
  check(g_get_prgname() && QString(g_get_prgname()) == "ky-prism",
        "program name drives the window class / app_id");
  const auto first = window.primaryView();
  waitFor([&] { return !first->gpu().isEmpty() || !first->hardwareReady(); },
          "renderer initialised", 5000);
  shot(content(gtkWindow), "01-welcome");

  // --- library: asynchronous listing, natural order, directories first ---
  window.browse(photos);
  auto *library = window.library();
  waitFor([&] { return !library->scanning() && library->count() == 7; },
          "directory listed");
  const QStringList expected{"Wakacje",      "IMG_2.jpg",  "IMG_10.jpg",
                             "blue.png",     "green.jpg",  "red.jpg",
                             "żółć [#].jpg"};
  const auto names = library->names();
  check(names.first() == "Wakacje", "directories lead the list");
  check(names.indexOf("IMG_2.jpg") < names.indexOf("IMG_10.jpg"),
        "natural sort: 2 before 10");
  check(!names.contains("notes.txt") && !names.contains(".hidden.jpg"),
        "only photographs, no hidden files");
  check(QSet<QString>(names.cbegin(), names.cend()) ==
            QSet<QString>(expected.cbegin(), expected.cend()),
        "listing content");
  // Click-to-open decides by what lies under the pointer.
  pump(200);
  {
    auto *list = library->focusWidget();
    const int width = gtk_widget_get_width(list),
              height = gtk_widget_get_height(list);
    check(width > 100 && height > 150, "library list is laid out");
    check(!library->entryAt(40, 8), "column header is not an entry");
    check(library->entryAt(40, 60), "first row is an entry");
    check(!library->entryAt(40, height - 4), "empty space is not an entry");
  }
  window.setSort(1, true); // size, descending: directories still lead
  check(library->names().first() == "Wakacje" &&
            library->names().at(1) == "IMG_10.jpg",
        "size sort, descending, directories first");
  window.setSort(0, false);
  check(window.currentDirectory() == photos, "current directory");

  // --- open: decode, 16-bit canvas, exact pixels ---
  const QString red = photos + "/red.jpg";
  window.open(red);
  check(!window.welcomeVisible(), "photo page after open");
  check(first->loading(), "loading indicator shown at open");
  waitFor([&] { return first->imageSize() == QSize(640, 480) && !first->busy(); },
          "photo decoded");
  waitFor([&] { return window.statusText().startsWith("Gotowe"); },
          "ready status");
  check(!first->loading() && !first->loadingAnimating(),
        "loading indicator stops after decode");
  check(window.titleText() == "red.jpg — ky-prism", "document window title");
  check(window.currentPhoto() == red, "current photo path");
  check(window.metadataText().contains("red.jpg"), "metadata delivered");
  check(window.zoomText().endsWith("%"), "zoom indicator");
  QImage reference;
  if (first->hardwareReady()) {
    pump(300);
    const auto frame = first->framebuffer();
    check(!frame.isNull(), "framebuffer readable");
    const QColor centre = frame.pixelColor(frame.width() / 2, frame.height() / 2);
    check(near(centre.red(), 200, 3) && near(centre.green(), 40, 3) &&
              near(centre.blue(), 30, 3) && centre.alpha() == 255,
          "canvas shows the photograph's own colour, fully opaque");
    reference = frame;
  }
  shot(content(gtkWindow), "02-photo");

  // --- zoom and pan ---
  check(window.trigger("actual"), "1:1 action");
  check(window.zoomText() == "100.0%", "1:1 means one image pixel per device pixel");
  check(std::abs(first->zoomFactor() - 1.0) < 1e-9, "physical zoom factor 1.0");
  window.trigger("zoomIn");
  waitFor([&] { return !first->busy(); }, "zoom settles");
  check(std::abs(first->zoomFactor() - 1.25) < 1e-6, "zoom in by 1.25");
  window.trigger("zoomOut");
  waitFor([&] { return !first->busy(); }, "zoom settles");
  check(std::abs(first->zoomFactor() - 1.0) < 1e-6, "zoom out returns to 1.0");
  const auto before = first->displayedImageRect();
  first->pan({64, -32});
  check(first->displayedImageRect().topLeft() ==
            before.topLeft() + QPointF(64, -32),
        "pan moves the image");
  window.trigger("fit");
  waitFor([&] { return !first->busy(); }, "fit settles");
  const auto fitted = first->displayedImageRect();
  const auto area = first->logicalSize();
  check(fitted.width() <= area.width() + 0.5 &&
            fitted.height() <= area.height() + 0.5 &&
            (std::abs(fitted.width() - area.width()) < 1.0 ||
             std::abs(fitted.height() - area.height()) < 1.0),
        "fit shows the whole photograph");

  // --- interface opacity never touches the photograph ---
  const int originalOpacity = Theme::opacity();
  auto *root = content(gtkWindow);
  auto chromeAlpha = [&] {
    const auto image = render(root);
    return image.isNull() ? -1 : qAlpha(image.pixel(image.width() / 2, 12));
  };
  auto photoPixel = [&] {
    const auto image = render(first->widget());
    return image.isNull() ? QColor()
                          : QColor::fromRgba(qUnpremultiply(image.pixel(
                                image.width() / 2, image.height() / 2)));
  };
  for (int i = 0; i < 20 && Theme::opacity() > 45; ++i)
    window.trigger("opacityDown");
  check(Theme::opacity() <= 45 &&
            Theme::opacity() >= PrismSettings::minimumSliderOpacity,
        "opacity lowered in steps, clamped at the slider minimum");
  pump(200);
  const int translucent = chromeAlpha();
  check(translucent > 0 && translucent < 150,
        "chrome becomes translucent at low opacity");
  const QColor through = photoPixel();
  check(through.alpha() == 255 && near(through.red(), 200, 3) &&
            near(through.green(), 40, 3) && near(through.blue(), 30, 3),
        "photograph stays opaque and unaltered under translucent chrome");
  if (first->hardwareReady()) {
    const auto frame = first->framebuffer();
    check(frame == reference || frame.pixel(frame.width() / 2, frame.height() / 2) ==
                                    reference.pixel(reference.width() / 2,
                                                    reference.height() / 2),
          "canvas pixels identical at every opacity");
  }
  shot(root, "03-opacity-low");
  for (int i = 0; i < 20 && Theme::opacity() < 100; ++i)
    window.trigger("opacityUp");
  check(Theme::opacity() == 100, "opacity raised to fully opaque");
  pump(200);
  check(chromeAlpha() == 255, "fully opaque chrome at 100%");
  check(photoPixel().alpha() == 255, "photograph opaque at 100%");
  waitFor(
      [&] {
        return PrismSettings::storedOpacity({}, config + "/ky-prism/appearance.json") == 100;
      },
      "opacity persisted to appearance.json", 3000);
  shot(root, "04-opacity-full");
  window.trigger("opacity");
  pump(300);
  GtkWidget *popover = findType(GTK_WIDGET(gtkWindow), GTK_TYPE_POPOVER);
  check(popover && gtk_widget_get_visible(popover),
        "opacity slider popover opens");
  shot(gtk_popover_get_child(GTK_POPOVER(popover)), "04b-opacity-popover");
  gtk_popover_popdown(GTK_POPOVER(popover));
  pump(150);

  // --- navigation between siblings ---
  window.open(photos + "/green.jpg");
  waitFor([&] { return window.currentPhoto().endsWith("green.jpg") && !first->busy(); },
          "second photo");
  window.trigger("next");
  waitFor([&] { return window.currentPhoto().endsWith("IMG_10.jpg"); },
          "next photo in name order");
  window.trigger("previous");
  waitFor([&] { return window.currentPhoto().endsWith("green.jpg"); },
          "previous photo");
  waitFor([&] { return !first->busy(); }, "idle after navigation");

  // --- panels ---
  window.trigger("exif");
  check(window.exifVisible(), "EXIF panel shown");
  shot(root, "05-exif");
  window.trigger("escape");
  check(!window.exifVisible(), "Esc hides the EXIF panel");
  window.trigger("toggleLibrary");
  check(!window.libraryVisible(), "library hidden");
  window.trigger("focusDirectory");
  check(window.libraryVisible(), "path focus reveals the library");
  window.setLibraryMode(true);
  pump(600);
  shot(root, "06-thumbnails");
  window.setLibraryMode(false);

  // --- tabs ---
  window.trigger("newTab");
  check(window.tabCount() == 2 && window.activeTab() == 1, "new tab is active");
  check(window.welcomeVisible() && window.currentPhoto().isEmpty(),
        "new tab starts empty");
  window.trigger("previousTab");
  check(window.activeTab() == 0 && window.currentPhoto().endsWith("green.jpg"),
        "first tab restores its photo");
  waitFor([&] { return !first->busy() && first->imageSize() == QSize(640, 480); },
          "tab photo reloaded");
  shot(root, "07-tabs");
  window.trigger("nextTab");
  window.trigger("closeTab");
  check(window.tabCount() == 1 && window.currentPhoto().endsWith("green.jpg"),
        "closing the active tab returns to the remaining one");
  for (int i = 0; i < 30; ++i)
    window.trigger("newTab");
  check(window.tabCount() == 24, "tab limit");
  check(window.statusText() == "Maksymalnie 24 karty.", "tab limit message");
  while (window.tabCount() > 1)
    window.trigger("closeTab");
  window.trigger("closeTab");
  check(window.tabCount() == 1, "closing the last tab leaves one empty tab");
  window.open(photos + "/green.jpg");
  waitFor([&] { return !first->busy() && first->imageSize() == QSize(640, 480); },
          "photo after tab churn");

  // --- comparison ---
  window.showComparison(photos + "/blue.png");
  auto *second = window.secondaryView();
  check(window.compareVisible(), "comparison pane shown");
  waitFor([&] { return second->imageSize() == QSize(320, 200) && !second->busy(); },
          "comparison decoded");
  check(window.comparedPhoto().endsWith("blue.png"), "compared photo path");
  shot(root, "08-compare");
  window.trigger("compare");
  check(!window.compareVisible() && window.comparedPhoto().isEmpty(),
        "comparison closed");
  library->selectRows({int(library->names().indexOf("red.jpg")),
                       int(library->names().indexOf("green.jpg"))});
  window.trigger("compare");
  check(window.compareVisible() && !window.currentPhoto().isEmpty() &&
            !window.comparedPhoto().isEmpty() &&
            window.currentPhoto() != window.comparedPhoto(),
        "two selected files open side by side");
  waitFor([&] { return !first->busy() && !second->busy(); }, "pair decoded");
  check(library->selectedFiles().size() == 2,
        "opening the pair keeps both files selected");
  window.trigger("compare");

  // --- bookmarks and favourites (isolated XDG_DATA_HOME) ---
  check(!window.bookmarkChecked(), "directory not bookmarked yet");
  window.trigger("star");
  check(window.bookmarkChecked(), "bookmark saved");
  check(window.statusText() == "Zapisano zakładkę katalogu.", "bookmark message");
  window.trigger("bookmarks");
  pump(300);
  auto *bookmarks = dialogOf(gtkWindow);
  check(bookmarks != nullptr, "bookmark browser opened");
  shot(content(bookmarks), "09-bookmarks");
  closeDialog(bookmarks);
  window.trigger("star");
  check(!window.bookmarkChecked(), "bookmark removed");
  waitFor([&] { return !first->busy(); }, "idle before favourites");
  window.trigger("heart");
  waitFor([&] { return window.heartSelected(); }, "favourite stored with MD5");
  window.trigger("favorites");
  pump(300);
  auto *favorites = dialogOf(gtkWindow);
  check(favorites != nullptr, "favourites browser opened");
  shot(content(favorites), "10-favorites");
  closeDialog(favorites);
  window.trigger("heart");
  waitFor([&] { return !window.heartSelected(); }, "favourite removed");

  // --- language ---
  window.trigger("language");
  waitFor([&] { return PrismLanguage::english(); }, "switched to English");
  pump(450);
  check(window.titleText().startsWith(QFileInfo(window.currentPhoto()).fileName()),
        "document title is never translated");
  shot(root, "11-english");
  window.trigger("language");
  waitFor([&] { return !PrismLanguage::english(); }, "switched back to Polish");
  pump(450);

  // --- dialogs ---
  window.trigger("about");
  pump(900);
  auto *about = dialogOf(gtkWindow);
  check(about != nullptr, "about dialog opened");
  shot(content(about), "12-about");
  closeDialog(about);
  window.trigger("help");
  pump(300);
  shot(content(dialogOf(gtkWindow)), "13-keyboard");
  closeDialog(dialogOf(gtkWindow));
  window.trigger("copy");
  pump(300);
  auto *copy = dialogOf(gtkWindow);
  check(copy != nullptr, "copy dialog opened");
  shot(content(copy), "14-copy");
  closeDialog(copy);
  pump(150);
  window.trigger("duplicates");
  pump(300);
  shot(content(dialogOf(gtkWindow)), "15-duplicates");
  closeDialog(dialogOf(gtkWindow));
  pump(150);
  window.trigger("open");
  pump(900);
  auto *chooser = dialogOf(gtkWindow);
  check(chooser != nullptr, "photo chooser opened");
  shot(content(chooser), "16-chooser");
  closeDialog(chooser);
  pump(300);

  // --- light palette (Omarchy colours.toml override) ---
  if (!shots.isEmpty()) {
    Theme::reload("background = \"#f4f1ea\"\nforeground = \"#2b2a27\"\naccent = "
                  "\"#b0562f\"\n");
    Theme::setOpacity(92, false);
    shot(root, "17-light");
    window.trigger("newTab");
    shot(root, "18-light-welcome");
    window.trigger("closeTab");
    Theme::reload();
  }
  Theme::setOpacity(originalOpacity, false);

  // --- fullscreen / maximise requests are accepted ---
  check(window.trigger("fullscreen"), "fullscreen action");
  pump(400);
  check(window.trigger("escape"), "escape leaves fullscreen");
  pump(400);
  check(!window.trigger("no-such-action"), "unknown action is rejected");

  // --- copy and duplicate search through the dialog's own worker ---
  {
    const QString source = photos + "/red.jpg", target = work + "/kopia-red.jpg";
    FileOpsDialog copy(source, false, photos, gtkWindow);
    copy.setSources({source});
    copy.setDestinationForTest(target);
    copy.runForTest();
    check(copy.active(), "copy worker started");
    waitFor([&] { return !copy.active(); }, "copy finished");
    QFile a(source), b(target);
    check(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly) &&
              a.readAll() == b.readAll(),
          "copy is byte-identical to the original");
    copy.setSources({source});
    copy.setDestinationForTest(target);
    check(copy.messageText().startsWith("Cel już istnieje"),
          "an existing target is never overwritten");
    QDir().mkpath(work + "/a");
    QDir().mkpath(work + "/b");
    check(QFile::copy(source, work + "/a/dup.jpg") &&
              QFile::copy(source, work + "/b/dup.jpg"),
          "duplicate fixtures");
    {
      FileOpsDialog empty({}, true, {}, gtkWindow);
      check(!empty.canStart(), "no folder: the start button waits");
    }
    FileOpsDialog duplicates({}, true, photos, gtkWindow);
    check(duplicates.searching(), "duplicate mode");
    check(duplicates.canStart(), "library folder preselected, start enabled");
    duplicates.selectFolderForTest(work);
    check(!duplicates.active() && duplicates.canStart(),
          "choosing a folder waits for the start button");
    duplicates.startForTest();
    check(duplicates.active() && !duplicates.canStart(),
          "start button runs the search and is disabled meanwhile");
    waitFor([&] { return !duplicates.active(); }, "duplicate scan finished");
    check(duplicates.messageText().startsWith("Znaleziono 1 grup") ||
              duplicates.messageText().startsWith("Wybrany plik: "),
          "one duplicate group found");
  }
  pump(100);

  // --- close while a decode is in flight ---
  window.open(photos + "/IMG_10.jpg");
  window.close();
  check(closedSeen && window.shuttingDown(), "close cancels work and reports");
  check(!window.trigger("open"), "no action runs after shutdown began");
}
// README picture: English UI, thumbnails of PHOTOS, the first photo open.
void readmeShot(const QString &photos, const QString &out) {
  PrismWindow window;
  gtk_window_set_default_size(window.window(), 1440, 900);
  window.show();
  waitFor([&] { return gtk_widget_get_mapped(GTK_WIDGET(window.window())); },
          "window mapped");
  window.browse(photos);
  auto *library = window.library();
  waitFor([&] { return !library->scanning() && library->count() > 0; },
          "directory listed");
  window.setLibraryMode(true);
  const auto names = library->names();
  window.open(photos + "/" + names.first());
  const auto view = window.primaryView();
  waitFor([&] { return !view->imageSize().isEmpty() && !view->busy(); },
          "photo decoded");
  pump(2500);
  shots = QFileInfo(out).absolutePath();
  QDir().mkpath(shots);
  shot(content(window.window()), QFileInfo(out).completeBaseName());
  window.close();
}
} // namespace

int main(int argc, char **argv) {
  // Never touch the user's own preferences, favourites or caches.
  QTemporaryDir home;
  if (!home.isValid())
    return 2;
  for (const char *name : {"data", "config", "cache", "photos/Wakacje", "work"})
    QDir().mkpath(home.path() + "/" + name);
  qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
  qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
  qputenv("XDG_CACHE_HOME", (home.path() + "/cache").toUtf8());
  qputenv("PRISM_LANGUAGE", "pl");
  qunsetenv("PRISM_UI_OPACITY");
  if (qEnvironmentVariableIsEmpty("GSK_RENDERER"))
    qputenv("GSK_RENDERER", "gl");
  QString readmePhotos, readmeOut;
  for (int i = 1; i + 2 < argc; ++i)
    if (QString(argv[i]) == "--readme-shot") {
      readmePhotos = QString::fromLocal8Bit(argv[i + 1]);
      readmeOut = QString::fromLocal8Bit(argv[i + 2]);
      qputenv("PRISM_LANGUAGE", "en");
    }
  for (int i = 1; i + 1 < argc; ++i)
    if (QString(argv[i]) == "--screenshots") {
      shots = QString::fromLocal8Bit(argv[i + 1]);
      QDir().mkpath(shots);
    }
  g_set_prgname("ky-prism");
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName("ky-prism");
  if (!gtk_init_check()) {
    std::cout << "gtk ui: no display, skipped\n";
    return 77;
  }
  if (VIPS_INIT(argv[0]))
    return 2;
  vips_cache_set_max(0);
  int result = 0;
  try {
    if (!readmePhotos.isEmpty()) {
      PrismLanguage::initialize();
      Theme::install();
      readmeShot(readmePhotos, readmeOut);
      pump(200);
      Theme::shutdown();
      NvjpegBackend::shutdown();
      Document::clearSharedPreviews();
      vips_shutdown();
      return 0;
    }
    const QString photos = home.path() + "/photos";
    solid(photos + "/red.jpg", 640, 480, {200, 40, 30});
    solid(photos + "/green.jpg", 640, 480, {30, 160, 60});
    solid(photos + "/blue.png", 320, 200, {20, 60, 190});
    solid(photos + "/IMG_2.jpg", 800, 600, {120, 120, 120});
    solid(photos + "/IMG_10.jpg", 1600, 1200, {180, 150, 90});
    solid(photos + "/żółć [#].jpg", 300, 300, {240, 220, 40});
    solid(photos + "/.hidden.jpg", 64, 64, {0, 0, 0});
    solid(photos + "/Wakacje/plaża.jpg", 400, 300, {90, 170, 220});
    {
      QFile notes(photos + "/notes.txt");
      check(notes.open(QIODevice::WriteOnly), "fixture");
      notes.write("not a photograph");
    }
    PrismLanguage::initialize();
    Theme::install();
    scenario(photos, home.path() + "/config", home.path() + "/work");
    // Repeated construction/destruction: no worker or widget outlives a window.
    for (int i = 0; i < 3; ++i) {
      PrismWindow window;
      window.show();
      window.browse(photos);
      window.open(photos + (i % 2 ? "/red.jpg" : "/IMG_10.jpg"));
      pump(i * 60);
    }
    pump(100);
    check(Dialogs::openShells() == 0, "every dialog window was destroyed");
    Theme::shutdown();
    std::cout << "gtk ui: ok\n";
  } catch (const std::exception &e) {
    std::cerr << "gtk ui: " << e.what() << '\n';
    result = 1;
  }
  NvjpegBackend::shutdown();
  Document::clearSharedPreviews();
  vips_shutdown();
  return result;
}
