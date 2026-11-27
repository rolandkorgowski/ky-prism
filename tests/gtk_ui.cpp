// GTK front-end behaviour test. Drives the real window through its named
// actions on the session's display; exits 77 (skip) when no display exists.
//   prism-gtk-test                    assertions only
//   prism-gtk-test --screenshots DIR  additionally renders PNGs for review
//   prism-gtk-test --readme-shot PHOTOS OUT.png  main window for the README
//   prism-gtk-test --stress ROUNDS  memory growth and main-loop stalls while
//                    copying a 24 Mpx photo to the clipboard and exporting it
//   prism-gtk-test --xdotool-drag   real pointer drags from the library on an
//                    X11 display, driven by xdotool. It moves the display's
//                    pointer, so it is not part of ctest: run it only on a
//                    throwaway display (Xvfb in a test machine).
#include "gtkbase.h"
#include "dialogs.h"
#include "document.h"
#include "language.h"
#include "library_pane.h"
#include "nvjpeg_backend.h"
#include "photo_view.h"
#include "print_layout.h"
#include "settings.h"
#include "theme.h"
#include "window.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QVersionNumber>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <csignal>
#include <dlfcn.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include <vips/vips8>
#include <zlib.h>

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
  graphene_rect_t bounds = Gx::rect(0, 0, float(w), float(h));
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
// Clicks the button whose caption is `caption` inside `area`.
bool pressIn(GtkWidget *area, const QString &caption) {
  std::function<GtkWidget *(GtkWidget *)> find = [&](GtkWidget *w) -> GtkWidget * {
    if (GTK_IS_BUTTON(w))
      if (auto *label = findType(w, GTK_TYPE_LABEL);
          label && QString::fromUtf8(gtk_label_get_text(GTK_LABEL(label))) == caption)
        return w;
    for (auto *c = gtk_widget_get_first_child(w); c; c = gtk_widget_get_next_sibling(c))
      if (auto *found = find(c))
        return found;
    return nullptr;
  };
  auto *button = area ? find(area) : nullptr;
  if (button)
    g_signal_emit_by_name(button, "clicked");
  pump(150);
  return button;
}
// Clicks the dialog button whose caption is `caption`.
bool press(GtkWindow *dialog, const QString &caption) {
  return pressIn(dialog ? GTK_WIDGET(dialog) : nullptr, caption);
}
GtkEventController *controllerOf(GtkWidget *widget, GType type) {
  GListModel *list = gtk_widget_observe_controllers(widget);
  GtkEventController *found = nullptr;
  for (guint i = 0; i < g_list_model_get_n_items(list) && !found; ++i) {
    auto *controller = GTK_EVENT_CONTROLLER(g_list_model_get_item(list, i));
    if (G_TYPE_CHECK_INSTANCE_TYPE(controller, type))
      found = controller;
    g_object_unref(controller);
  }
  g_object_unref(list);
  return found;
}
// The click gesture of `widget` that listens to mouse button `button` only.
GtkGesture *buttonGesture(GtkWidget *widget, guint button) {
  GListModel *list = gtk_widget_observe_controllers(widget);
  GtkGesture *found = nullptr;
  for (guint i = 0; i < g_list_model_get_n_items(list) && !found; ++i) {
    auto *controller = GTK_EVENT_CONTROLLER(g_list_model_get_item(list, i));
    if (GTK_IS_GESTURE_CLICK(controller) &&
        gtk_gesture_single_get_button(GTK_GESTURE_SINGLE(controller)) == button)
      found = GTK_GESTURE(controller);
    g_object_unref(controller);
  }
  g_object_unref(list);
  return found;
}
QStringList filesOf(GdkContentProvider *provider) {
  QStringList paths;
  GValue value = G_VALUE_INIT;
  g_value_init(&value, GDK_TYPE_FILE_LIST);
  if (provider && gdk_content_provider_get_value(provider, &value, nullptr)) {
    GSList *files =
        gdk_file_list_get_files(static_cast<GdkFileList *>(g_value_get_boxed(&value)));
    for (auto *l = files; l; l = l->next) {
      g_autofree char *path = g_file_get_path(G_FILE(l->data));
      if (path)
        paths << QFile::decodeName(path);
    }
    g_slist_free(files);
  }
  g_value_unset(&value);
  return paths;
}
// Centre of the library row/thumbnail of `path`, in the window content's
// coordinates (the drop target's).
bool dropPoint(PrismWindow &window, const QString &path, double *x, double *y) {
  double vx = 0, vy = 0;
  if (!window.library()->pointOf(path, &vx, &vy))
    return false;
  graphene_point_t in = Gx::point(float(vx), float(vy)), out;
  if (!gtk_widget_compute_point(window.library()->focusWidget(),
                                content(window.window()), &in, &out))
    return false;
  *x = out.x;
  *y = out.y;
  return true;
}
QByteArray bytesOf(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
ino_t inodeOf(const QString &path) {
  struct stat s{};
  return lstat(QFile::encodeName(path).constData(), &s) ? 0 : s.st_ino;
}
// Every label of a dialog, one per line.
QString textOf(GtkWidget *widget) {
  QString out;
  if (GTK_IS_LABEL(widget))
    out += QString::fromUtf8(gtk_label_get_text(GTK_LABEL(widget))) + "\n";
  for (auto *c = gtk_widget_get_first_child(widget); c; c = gtk_widget_get_next_sibling(c))
    out += textOf(c);
  return out;
}
// The unmodified `key` shortcut that `widget` itself handles: activated when
// `activate`, otherwise only looked for.
bool keyShortcut(GtkWidget *widget, guint key, bool activate) {
  GListModel *controllers = gtk_widget_observe_controllers(widget);
  bool found = false;
  for (guint i = 0; i < g_list_model_get_n_items(controllers) && !found; ++i) {
    auto *controller = g_list_model_get_item(controllers, i);
    if (GTK_IS_SHORTCUT_CONTROLLER(controller)) {
      auto *shortcuts = G_LIST_MODEL(controller);
      for (guint j = 0; j < g_list_model_get_n_items(shortcuts) && !found; ++j) {
        auto *shortcut = GTK_SHORTCUT(g_list_model_get_item(shortcuts, j));
        auto *trigger = gtk_shortcut_get_trigger(shortcut);
        if (GTK_IS_KEYVAL_TRIGGER(trigger) &&
            gtk_keyval_trigger_get_keyval(GTK_KEYVAL_TRIGGER(trigger)) == key &&
            gtk_keyval_trigger_get_modifiers(GTK_KEYVAL_TRIGGER(trigger)) == 0)
          found = !activate ||
                  gtk_shortcut_action_activate(gtk_shortcut_get_action(shortcut),
                                               GTK_SHORTCUT_ACTION_EXCLUSIVE,
                                               widget, nullptr);
        g_object_unref(shortcut);
      }
    }
    g_object_unref(controller);
  }
  g_object_unref(controllers);
  return found;
}
// What GTK runs when `key` (with `modifiers`) reaches `widget` unhandled:
// its own bindings, class ones included, with their arguments.
bool boundKey(GtkWidget *widget, guint key,
              GdkModifierType modifiers = GdkModifierType(0)) {
  std::function<bool(GtkShortcutTrigger *)> matches = [&](GtkShortcutTrigger *t) {
    if (GTK_IS_ALTERNATIVE_TRIGGER(t))
      return matches(gtk_alternative_trigger_get_first(GTK_ALTERNATIVE_TRIGGER(t))) ||
             matches(gtk_alternative_trigger_get_second(GTK_ALTERNATIVE_TRIGGER(t)));
    return GTK_IS_KEYVAL_TRIGGER(t) &&
           gtk_keyval_trigger_get_keyval(GTK_KEYVAL_TRIGGER(t)) == key &&
           gtk_keyval_trigger_get_modifiers(GTK_KEYVAL_TRIGGER(t)) == modifiers;
  };
  GListModel *controllers = gtk_widget_observe_controllers(widget);
  bool found = false;
  for (guint i = 0; i < g_list_model_get_n_items(controllers) && !found; ++i) {
    auto *controller = g_list_model_get_item(controllers, i);
    if (GTK_IS_SHORTCUT_CONTROLLER(controller)) {
      auto *shortcuts = G_LIST_MODEL(controller);
      for (guint j = 0; j < g_list_model_get_n_items(shortcuts) && !found; ++j) {
        auto *shortcut = GTK_SHORTCUT(g_list_model_get_item(shortcuts, j));
        if (matches(gtk_shortcut_get_trigger(shortcut)))
          found = gtk_shortcut_action_activate(gtk_shortcut_get_action(shortcut),
                                               GTK_SHORTCUT_ACTION_EXCLUSIVE,
                                               widget,
                                               gtk_shortcut_get_arguments(shortcut));
        g_object_unref(shortcut);
      }
    }
    g_object_unref(controller);
  }
  g_object_unref(controllers);
  pump(30);
  return found;
}
// The window's own binding for `key`: arrows and Tab emit "move-focus".
bool windowKey(GtkWindow *window, guint key,
               GdkModifierType modifiers = GdkModifierType(0)) {
  return boundKey(GTK_WIDGET(window), key, modifiers);
}
// The visible "Załaduj zdjęcie" button of the welcome page.
GtkWidget *welcomeButtonOf(GtkWidget *root) {
  if (GTK_IS_BUTTON(root) && gtk_widget_has_css_class(root, "suggested") &&
      gtk_widget_is_visible(root))
    return root;
  for (auto *c = gtk_widget_get_first_child(root); c; c = gtk_widget_get_next_sibling(c))
    if (auto *found = welcomeButtonOf(c))
      return found;
  return nullptr;
}
// The list or grid inside a library view (the column view holds a list).
GtkWidget *listBaseOf(GtkWidget *view) {
  return GTK_IS_LIST_BASE(view) ? view : findType(view, GTK_TYPE_LIST_BASE);
}
// The keyboard sits on an entry (a row or a thumbnail) of `view`, not on the
// view itself.
bool focusOnEntry(GtkWindow *window, GtkWidget *view) {
  auto *focus = gtk_window_get_focus(window);
  return focus && view && gtk_widget_is_ancestor(focus, view) &&
         !GTK_IS_LIST_BASE(focus) && !GTK_IS_COLUMN_VIEW(focus);
}
// The button of `area` whose tooltip starts with `tip`.
GtkWidget *buttonWithTip(GtkWidget *area, const QString &tip) {
  if (GTK_IS_BUTTON(area) || GTK_IS_MENU_BUTTON(area)) {
    const char *text = gtk_widget_get_tooltip_text(area);
    if (text && QString::fromUtf8(text).startsWith(tip))
      return area;
  }
  for (auto *c = gtk_widget_get_first_child(area); c; c = gtk_widget_get_next_sibling(c))
    if (auto *found = buttonWithTip(c, tip))
      return found;
  return nullptr;
}
// Whether the keyboard focus of `window` is `area` or inside it.
bool focusIn(GtkWindow *window, GtkWidget *area) {
  auto *focus = gtk_window_get_focus(window);
  return focus && area && (focus == area || gtk_widget_is_ancestor(focus, area));
}
// The right-button click gesture of a library view.
GtkGesture *secondaryClick(GtkWidget *view) {
  GListModel *controllers = gtk_widget_observe_controllers(view);
  GtkGesture *found = nullptr;
  for (guint i = 0; i < g_list_model_get_n_items(controllers) && !found; ++i) {
    auto *controller = g_list_model_get_item(controllers, i);
    if (GTK_IS_GESTURE_CLICK(controller) &&
        gtk_gesture_single_get_button(GTK_GESTURE_SINGLE(controller)) ==
            GDK_BUTTON_SECONDARY)
      found = GTK_GESTURE(controller);
    g_object_unref(controller);
  }
  g_object_unref(controllers);
  return found;
}
// A visible popover below `widget` that offers `caption`.
GtkWidget *openPopoverWith(GtkWidget *widget, const QString &caption) {
  if (GTK_IS_POPOVER(widget) && gtk_widget_get_visible(widget) &&
      textOf(widget).split('\n').contains(caption))
    return widget;
  for (auto *c = gtk_widget_get_first_child(widget); c; c = gtk_widget_get_next_sibling(c))
    if (auto *found = openPopoverWith(c, caption))
      return found;
  return nullptr;
}
// Plain text of the clipboard, read back through GDK.
QString clipboardText(GtkWidget *widget) {
  struct Read {
    QString text;
    bool done = false;
  } read;
  gdk_clipboard_read_text_async(
      gtk_widget_get_clipboard(widget), nullptr,
      +[](GObject *source, GAsyncResult *result, gpointer data) {
        auto *state = static_cast<Read *>(data);
        g_autofree char *text =
            gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), result, nullptr);
        state->text = QString::fromUtf8(text);
        state->done = true;
      },
      &read);
  waitFor([&] { return read.done; }, "clipboard text read back");
  return read.text;
}
// The clipboard holds text only: no image, no file to paste into a folder.
bool clipboardTextOnly(GtkWidget *widget) {
  auto *formats = gdk_clipboard_get_formats(gtk_widget_get_clipboard(widget));
  return gdk_content_formats_contain_mime_type(formats, "text/plain;charset=utf-8") &&
         !gdk_content_formats_contain_mime_type(formats, "image/png") &&
         !gdk_content_formats_contain_mime_type(formats, "text/uri-list") &&
         !gdk_content_formats_contain_mime_type(formats, "x-special/gnome-copied-files");
}
bool noTemporaries(const QString &folder) {
  return QDir(folder)
      .entryList({".prism-copy-*"}, QDir::Files | QDir::Hidden | QDir::System)
      .isEmpty();
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
  // --- a freshly opened window: the arrows go to the library, not to the
  // buttons; in the list and in the thumbnails, with or without a selection ---
  pump(200);
  {
    auto *welcome = welcomeButtonOf(GTK_WIDGET(gtkWindow));
    check(welcome && gtk_window_get_focus(gtkWindow) == welcome,
          "the welcome button has the keyboard at start");
    const auto names0 = library->names();
    for (bool thumbnails : {false, true}) {
      window.setLibraryMode(thumbnails);
      pump(300);
      auto *view = library->focusWidget();
      const QString mode = thumbnails ? "thumbnails" : "list";
      for (bool chosen : {false, true}) {
        library->selectRows(chosen ? QList<int>{2} : QList<int>{});
        gtk_widget_grab_focus(welcome);
        windowKey(gtkWindow, GDK_KEY_Down);
        check(focusOnEntry(gtkWindow, view) && window.welcomeVisible(),
              qPrintable("an arrow at start hands the keyboard to an entry of the " +
                         mode + (chosen ? " (selection)" : " (no selection)")));
        // The view's own arrow then works from there: with no selection it
        // selects the entry it is on, the first; with one it moves on.
        check(boundKey(listBaseOf(view), thumbnails ? GDK_KEY_Right : GDK_KEY_Down),
              "the view binds its arrows");
        const QString expected = photos + "/" + names0.at(chosen ? 3 : 0);
        check(library->selectedPath() == expected,
              qPrintable("the " + mode + "'s own arrow continues from the entry " +
                         "the keyboard went to: " + library->selectedPath()));
      }
    }
    window.setLibraryMode(false);
    library->selectRows({});
    gtk_widget_grab_focus(welcome);
    pump(100);
  }
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

  // --- magnified pixels stay sharp: no interpolation from 1:1 upwards ---
  if (first->hardwareReady()) {
    const QString stripes = work + "/stripes.png";
    QImage pattern(64, 64, QImage::Format_RGB888); // 1-pixel black/white columns
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x)
        pattern.setPixelColor(x, y, x % 2 ? Qt::white : Qt::black);
    check(pattern.save(stripes, "PNG"), "stripes fixture");
    window.open(stripes);
    waitFor([&] { return first->imageSize() == QSize(64, 64) && !first->busy(); },
            "stripes decoded");
    window.trigger("actual");
    first->scaleCentered(8.0);
    waitFor([&] { return !first->busy(); }, "stripes magnified");
    pump(400);
    const auto frame = first->framebuffer();
    int blended = 0, sampled = 0;
    for (int y = frame.height() / 2 - 100; y < frame.height() / 2 + 100; y += 3)
      for (int x = frame.width() / 2 - 100; x < frame.width() / 2 + 100; ++x) {
        const int v = qGray(frame.pixel(x, y));
        ++sampled;
        if (v > 3 && v < 252)
          ++blended;
      }
    check(sampled > 0 && blended == 0,
          "8× zoom shows each photo pixel as a sharp block (no blended greys)");
    // The optional smoothing brings the interpolation back, and is saved.
    // Read what the window shows (the GL area is double-buffered, so the raw
    // framebuffer may still hold the previous frame).
    auto blendedNow = [&] {
      pump(400);
      const auto again = render(first->widget());
      int count = 0;
      for (int x = again.width() / 2 - 100; x < again.width() / 2 + 100; ++x) {
        const int v = qGray(again.pixel(x, again.height() / 2));
        if (v > 3 && v < 252)
          ++count;
      }
      return count;
    };
    check(window.trigger("smoothZoom") && first->smoothZoom() && blendedNow() > 0,
          "smoothing option interpolates magnified pixels");
    check(PrismSettings::storedSmoothZoom(PrismSettings::appearanceFile()),
          "smoothing option remembered");
    check(window.trigger("smoothZoom") && !first->smoothZoom() && blendedNow() == 0,
          "smoothing off: sharp pixels again");
    window.open(photos + "/red.jpg");
    waitFor([&] { return window.currentPhoto().endsWith("red.jpg") && !first->busy(); },
            "red again after stripes");
  }

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
  // Chrome background alpha: the least opaque pixel along the header row
  // (text and buttons are more opaque; the background is always there).
  auto chromeAlpha = [&] {
    const auto image = render(root);
    if (image.isNull())
      return -1;
    int least = 255;
    for (int x = image.width() / 4; x < image.width() * 3 / 4; ++x)
      least = std::min(least, qAlpha(image.pixel(x, 12)));
    return least;
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

  // --- arrows in another folder, after a mouse click on a file in the
  // library: the photo takes the keyboard (the list would swallow ←/→) and
  // the neighbours come from the new folder ---
  {
    const QString other = work + "/strzalki";
    QDir().mkpath(other);
    for (const char *name : {"s1.jpg", "s2.jpg", "s3.jpg"})
      solid(other + "/" + name, 48, 32, {40, 120, 200});
    window.browse(other);
    waitFor([&] { return !library->scanning() && library->count() == 3; },
            "another folder listed");
    // A real click puts the keyboard in the list first.
    auto *list = library->focusWidget();
    gtk_widget_grab_focus(list);
    auto *focus = gtk_window_get_focus(gtkWindow);
    check(focus && (focus == list || gtk_widget_is_ancestor(focus, list)),
          "the list has the keyboard before the click");
    library->fileClicked(other + "/s2.jpg");
    waitFor([&] { return window.currentPhoto().endsWith("strzalki/s2.jpg") && !first->busy(); },
            "photo clicked in another folder");
    auto *canvas = first->focusWidget();
    const bool photoFocused = gtk_window_get_focus(gtkWindow) == canvas;
    check(keyShortcut(canvas, GDK_KEY_Right, true), "→ on the photo");
    waitFor([&] { return window.currentPhoto().endsWith("strzalki/s3.jpg"); },
            "→ opens the next photo of the new folder");
    check(keyShortcut(canvas, GDK_KEY_Left, true), "← on the photo");
    waitFor([&] { return window.currentPhoto().endsWith("strzalki/s2.jpg"); },
            "← opens the previous photo of the new folder");
    check(photoFocused, "a click on a file in the library hands the arrows to the photo");
    // A photo added after the folder was listed: the first ← relists the
    // folder and then steps.
    solid(other + "/s4.jpg", 48, 32, {40, 120, 200});
    library->fileClicked(other + "/s4.jpg");
    waitFor([&] { return window.currentPhoto().endsWith("strzalki/s4.jpg") && !first->busy(); },
            "photo added after the listing opened");
    check(keyShortcut(canvas, GDK_KEY_Left, true), "← on the added photo");
    waitFor([&] { return window.currentPhoto().endsWith("strzalki/s3.jpg"); },
            "← from a photo added after the listing");
    // Enter in the list opens the file and keeps the keyboard in the list.
    library->refresh();
    waitFor([&] { return !library->scanning() && library->count() == 4; },
            "added photo listed");
    gtk_widget_grab_focus(list);
    check(library->activateRow(int(library->names().indexOf("s1.jpg"))),
          "Enter on s1.jpg");
    waitFor([&] { return window.currentPhoto().endsWith("strzalki/s1.jpg") && !first->busy(); },
            "Enter opens the photo");
    focus = gtk_window_get_focus(gtkWindow);
    check(focus && (focus == list || gtk_widget_is_ancestor(focus, list)),
          "Enter keeps the keyboard in the list");
    window.open(photos + "/green.jpg");
    waitFor([&] { return window.currentPhoto().endsWith("green.jpg") && !first->busy(); },
            "photo restored after the arrow checks");
  }

  // --- arrows never walk the keyboard over the buttons (GTK's own arrow
  // bindings of the window would); they browse. Tab walks the controls. ---
  {
    auto *canvas = first->focusWidget();
    auto *main = GTK_WIDGET(gtkWindow);
    auto at = [&](const char *name) { return window.currentPhoto().endsWith(name); };
    gtk_widget_grab_focus(canvas);
    check(windowKey(gtkWindow, GDK_KEY_Up) && windowKey(gtkWindow, GDK_KEY_Down) &&
              windowKey(gtkWindow, GDK_KEY_Up, Gx::Ctrl),
          "the window binds ↑/↓ itself");
    check(gtk_window_get_focus(gtkWindow) == canvas && at("/green.jpg"),
          "↑/↓ on the photo keep the keyboard on the photo");
    check(windowKey(gtkWindow, GDK_KEY_KP_Right), "keypad → reaches the window");
    waitFor([&] { return at("/IMG_10.jpg") && !first->busy(); },
            "keypad → on the photo opens the next photo");
    windowKey(gtkWindow, GDK_KEY_KP_Left);
    waitFor([&] { return at("/green.jpg") && !first->busy(); },
            "keypad ← on the photo opens the previous photo");
    check(gtk_window_get_focus(gtkWindow) == canvas, "keypad arrows keep the photo focused");
    // A mouse click on a toolbar button leaves the keyboard on that button.
    auto *newTab = buttonWithTip(main, "Nowa karta");
    check(newTab && gtk_widget_grab_focus(newTab) &&
              focusIn(gtkWindow, newTab),
          "a toolbar button holds the keyboard");
    windowKey(gtkWindow, GDK_KEY_Right);
    waitFor([&] { return at("/IMG_10.jpg") && !first->busy(); },
            "→ on a toolbar button opens the next photo");
    check(gtk_window_get_focus(gtkWindow) == canvas,
          "→ on a toolbar button hands the keyboard to the photo");
    gtk_widget_grab_focus(newTab);
    windowKey(gtkWindow, GDK_KEY_Left);
    waitFor([&] { return at("/green.jpg") && !first->busy(); },
            "← on a toolbar button opens the previous photo");
    gtk_widget_grab_focus(newTab);
    windowKey(gtkWindow, GDK_KEY_Down);
    check(gtk_window_get_focus(gtkWindow) == canvas && at("/green.jpg"),
          "↓ on a toolbar button hands the keyboard to the photo");
    // Nothing focused at all.
    gtk_window_set_focus(gtkWindow, nullptr);
    windowKey(gtkWindow, GDK_KEY_Right);
    waitFor([&] { return at("/IMG_10.jpg") && !first->busy(); },
            "→ with nothing focused opens the next photo");
    check(gtk_window_get_focus(gtkWindow) == canvas,
          "→ with nothing focused hands the keyboard to the photo");
    check(keyShortcut(canvas, GDK_KEY_Left, true), "← back on the photo");
    waitFor([&] { return at("/green.jpg") && !first->busy(); }, "back at green.jpg");
    // The path field keeps ↑/↓ to itself.
    window.trigger("focusDirectory");
    auto *field = gtk_window_get_focus(gtkWindow);
    check(field && GTK_IS_EDITABLE(field), "the path field has the keyboard");
    windowKey(gtkWindow, GDK_KEY_Down);
    windowKey(gtkWindow, GDK_KEY_Up);
    check(gtk_window_get_focus(gtkWindow) == field && at("/green.jpg") &&
              window.currentDirectory() == photos,
          "↑/↓ in the path field stay in the field");
    // A control of the library: arrows go into the list.
    // ↑ from the topmost button of the panel: GTK alone would go up to the
    // toolbar.
    auto *up = buttonWithTip(main, "Katalog nadrzędny");
    check(up && gtk_widget_grab_focus(up), "a library button holds the keyboard");
    windowKey(gtkWindow, GDK_KEY_Up);
    check(focusOnEntry(gtkWindow, library->focusWidget()) && at("/green.jpg"),
          "an arrow on a library button hands the keyboard to the list");
    // The photo's menu still walks its items with the arrows.
    window.showPhotoMenu(120, 120);
    pump(300);
    GtkWidget *menu = nullptr;
    std::function<void(GtkWidget *)> findMenu = [&](GtkWidget *w) {
      if (GTK_IS_POPOVER(w) && gtk_widget_has_css_class(w, "prism-menu") &&
          gtk_widget_get_visible(w))
        menu = w;
      for (auto *c = gtk_widget_get_first_child(w); c && !menu;
           c = gtk_widget_get_next_sibling(c))
        findMenu(c);
    };
    findMenu(main);
    auto *item = gtk_window_get_focus(gtkWindow);
    check(menu && item && focusIn(gtkWindow, menu),
          "the opened photo menu has the keyboard");
    windowKey(gtkWindow, GDK_KEY_Down);
    auto *next = gtk_window_get_focus(gtkWindow);
    check(next && next != item && focusIn(gtkWindow, menu),
          "↓ in the photo menu moves to the next item");
    if (menu)
      gtk_popover_popdown(GTK_POPOVER(menu));
    pump(150);
    // Tab walks the controls.
    gtk_widget_grab_focus(canvas);
    check(windowKey(gtkWindow, GDK_KEY_Tab), "the window binds Tab");
    auto *after = gtk_window_get_focus(gtkWindow);
    check(after && after != canvas, "Tab moves the keyboard from the photo to a control");
    windowKey(gtkWindow, GDK_KEY_Tab, Gx::Shift);
    check(gtk_window_get_focus(gtkWindow) == canvas, "Shift+Tab returns to the photo");
    check(at("/green.jpg"), "Tab does not change the photo");
  }

  // --- side buttons of the mouse: back / forward through the folder history ---
  {
    auto *back = buttonGesture(GTK_WIDGET(gtkWindow), 8),
         *forward = buttonGesture(GTK_WIDGET(gtkWindow), 9);
    check(back && forward &&
              gtk_event_controller_get_propagation_phase(GTK_EVENT_CONTROLLER(back)) ==
                  GTK_PHASE_CAPTURE,
          "the window listens to the back and forward mouse buttons");
    check(window.currentDirectory() == photos, "photo folder before the side buttons");
    g_signal_emit_by_name(back, "pressed", 1, 10.0, 10.0);
    check(window.currentDirectory().endsWith("/strzalki"), "back button: previous folder");
    g_signal_emit_by_name(forward, "pressed", 1, 10.0, 10.0);
    check(window.currentDirectory() == photos, "forward button: next folder");
    check(window.currentPhoto().endsWith("green.jpg"), "the photo stays while walking folders");
  }

  // --- horizontal wheel: an enlarged photo moves sideways and stops at its
  // edges; a photo that fits the width stays put ---
  {
    auto *canvas = first->focusWidget();
    auto *wheel = controllerOf(canvas, GTK_TYPE_EVENT_CONTROLLER_SCROLL);
    check(wheel && (gtk_event_controller_scroll_get_flags(
                        GTK_EVENT_CONTROLLER_SCROLL(wheel)) &
                    GTK_EVENT_CONTROLLER_SCROLL_HORIZONTAL),
          "the photo listens to the horizontal wheel");
    first->fit();
    pump(100);
    const QRectF fitted = first->displayedImageRect();
    check(!first->scrollSideways(200) && first->displayedImageRect() == fitted,
          "a photo that fits the width stays put");
    first->scaleCentered(4.0);
    const double view = gtk_widget_get_width(canvas);
    waitFor([&] { return first->displayedImageRect().width() > view + 200; },
            "photo enlarged for the horizontal wheel");
    pump(300);
    const QRectF before = first->displayedImageRect();
    gboolean handled = FALSE;
    g_signal_emit_by_name(wheel, "scroll", 1.0, 0.0, &handled);
    const QRectF moved = first->displayedImageRect();
    check(handled && moved.x() < before.x() - 10 && moved.y() == before.y() &&
              moved.size() == before.size(),
          "a wheel step to the right moves the photo left, without zooming");
    first->scrollSideways(1e7);
    const QRectF right = first->displayedImageRect();
    check(std::abs(right.x() + right.width() - view) <= 1,
          "the horizontal wheel stops at the right edge");
    first->scrollSideways(-1e7);
    check(std::abs(first->displayedImageRect().x()) <= 1,
          "the horizontal wheel stops at the left edge");
    const double wide = first->displayedImageRect().width();
    g_signal_emit_by_name(wheel, "scroll", 0.0, -1.0, &handled);
    waitFor([&] { return first->displayedImageRect().width() > wide * 1.1; },
            "the vertical wheel still zooms");
    // At 1:1 the offset snaps to device pixels: small touchpad steps add up.
    solid(work + "/szeroki.jpg", 4000, 300, {90, 90, 90});
    window.open(work + "/szeroki.jpg");
    waitFor([&] { return first->imageSize() == QSize(4000, 300) && !first->busy(); },
            "wide photo for small steps");
    first->actual();
    pump(100);
    const double start = first->displayedImageRect().x();
    for (int i = 0; i < 10; ++i)
      first->scrollSideways(0.3);
    check(std::abs(start - first->displayedImageRect().x() - 3.0) <= 0.6,
          "ten sideways steps of 0.3 px at 1:1 move the photo by 3 px");
    window.open(photos + "/green.jpg");
    waitFor([&] { return window.currentPhoto().endsWith("green.jpg") && !first->busy(); },
            "photo restored after the wheel checks");
    first->fit();
    pump(100);
  }

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
  {
    // On the welcome page of a new tab an arrow hands the keyboard to the
    // library.
    auto *welcome = welcomeButtonOf(GTK_WIDGET(gtkWindow));
    check(welcome && gtk_widget_grab_focus(welcome), "the welcome button holds the keyboard");
    windowKey(gtkWindow, GDK_KEY_Right);
    check(focusOnEntry(gtkWindow, library->focusWidget()) && window.welcomeVisible(),
          "on the welcome page of a new tab an arrow hands the keyboard to the library");
  }
  window.trigger("previousTab");
  check(window.activeTab() == 0 && window.currentPhoto().endsWith("green.jpg"),
        "first tab restores its photo");
  waitFor([&] { return !first->busy() && first->imageSize() == QSize(640, 480); },
          "tab photo reloaded");
  shot(root, "07-tabs");
  {
    // A click on a tab's button hands ←/→ to the tab's photo.
    QList<GtkWidget *> titles;
    std::function<void(GtkWidget *)> collect = [&](GtkWidget *w) {
      if (gtk_widget_has_css_class(w, "tabtitle"))
        titles.append(w);
      for (auto *c = gtk_widget_get_first_child(w); c; c = gtk_widget_get_next_sibling(c))
        collect(c);
    };
    collect(GTK_WIDGET(gtkWindow));
    check(titles.size() == 2, "two tab buttons");
    g_signal_emit_by_name(titles.at(1), "clicked");
    check(window.activeTab() == 1 && window.welcomeVisible(), "click on the empty tab");
    gtk_widget_grab_focus(titles.at(0));
    g_signal_emit_by_name(titles.at(0), "clicked");
    check(window.activeTab() == 0 && window.currentPhoto().endsWith("green.jpg") &&
              gtk_window_get_focus(gtkWindow) == first->focusWidget(),
          "a click on a tab hands the arrows to its photo");
    waitFor([&] { return !first->busy(); }, "tab photo after the click");
  }
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

  // --- drag and drop: the library's drag source, the window's drop target ---
  {
    auto *area = content(gtkWindow);
    const QString holiday = photos + "/Wakacje", green = photos + "/green.jpg",
                  before = window.currentPhoto();
    for (bool thumbnails : {false, true}) {
      window.setLibraryMode(thumbnails);
      pump(400);
      double fx = 0, fy = 0, px = 0, py = 0, gx = 0, gy = 0;
      waitFor(
          [&] {
            return library->pointOf(holiday, &fx, &fy) &&
                   library->pointOf(red, &px, &py) &&
                   library->pointOf(green, &gx, &gy);
          },
          "library rows on screen");
      check(library->dragFiles(fx, fy).isEmpty(), "folders do not drag");
      library->selectRows({});
      check(library->dragFiles(px, py) == QStringList{red},
            "an unselected photo drags alone");
      library->selectRows({int(library->names().indexOf("red.jpg")),
                           int(library->names().indexOf("green.jpg"))});
      const auto group = library->dragFiles(gx, gy);
      check(group.size() == 2 && group.first() == green && group.contains(red),
            "a selected photo drags the whole selection, itself first");
      // The view's real drag source: its prepare handler builds the content.
      auto *source = controllerOf(library->focusWidget(), GTK_TYPE_DRAG_SOURCE);
      check(source, "the view has a drag source");
      check(gtk_drag_source_get_actions(GTK_DRAG_SOURCE(source)) == GDK_ACTION_COPY,
            "other programs may only copy, never move");
      GdkContentProvider *provider = nullptr;
      g_signal_emit_by_name(source, "prepare", gx, gy, &provider);
      check(filesOf(provider) == group, "the drag carries the file list");
      GdkContentFormats *formats =
          provider ? gdk_content_provider_ref_formats(provider) : nullptr;
      formats = formats ? gdk_content_formats_union_serialize_mime_types(formats)
                        : nullptr;
      check(formats && gdk_content_formats_contain_mime_type(formats, "text/uri-list"),
            "other programs get text/uri-list");
      if (formats)
        gdk_content_formats_unref(formats);
      g_clear_object(&provider);
      g_signal_emit_by_name(source, "prepare", fx, fy, &provider);
      check(!provider, "no drag starts on a folder");
      g_clear_object(&provider);
      double rx = 0, ry = 0, ox = 0, oy = 0;
      check(dropPoint(window, holiday, &rx, &ry) && dropPoint(window, red, &ox, &oy),
            "rows map to the window");
      check(window.dropTargetAt(rx, ry, true) == "folder:" + holiday,
            "library drag onto a folder copies there");
      check(window.dropTargetAt(ox, oy, true) == "none",
            "library drag onto a photo row does nothing");
      check(window.dropTargetAt(rx, ry, false) == "open",
            "another program's drop on a folder row opens, as before");
    }
    window.setLibraryMode(false);
    pump(300);

    // Onto the photo: open. Onto the second photo: compare.
    window.showComparison(photos + "/blue.png");
    waitFor([&] { return second->imageSize() == QSize(320, 200) && !second->busy(); },
            "comparison for the drop");
    auto centre = [&](GtkWidget *widget, double *x, double *y) {
      graphene_rect_t b;
      check(gtk_widget_compute_bounds(widget, area, &b), "widget bounds");
      *x = b.origin.x + b.size.width / 2;
      *y = b.origin.y + b.size.height / 2;
    };
    double vx = 0, vy = 0, cx = 0, cy = 0;
    centre(first->widget(), &vx, &vy);
    centre(second->widget(), &cx, &cy);
    check(window.dropTargetAt(vx, vy, true) == "open", "drop on the photo opens");
    check(window.dropTargetAt(cx, cy, true) == "compare",
          "drop on the second photo compares");
    check(window.dropFiles({photos + "/IMG_2.jpg"}, cx, cy, true) &&
              window.comparedPhoto().endsWith("IMG_2.jpg"),
          "a dropped photo replaces the compared one");
    waitFor([&] { return second->imageSize() == QSize(800, 600) && !second->busy(); },
            "dropped comparison decoded");
    // The window's real drop handler, as for a drop from a file manager.
    auto *target = controllerOf(area, GTK_TYPE_DROP_TARGET);
    check(target, "the window has a drop target");
    GValue value = G_VALUE_INIT;
    g_value_init(&value, GDK_TYPE_FILE_LIST);
    GSList *one = g_slist_prepend(
        nullptr, g_file_new_for_path(QFile::encodeName(photos + "/IMG_10.jpg").constData()));
    g_value_take_boxed(&value, gdk_file_list_new_from_list(one));
    g_slist_free_full(one, g_object_unref);
    gboolean accepted = FALSE;
    g_signal_emit_by_name(target, "drop", &value, vx, vy, &accepted);
    g_value_unset(&value);
    check(accepted && window.currentPhoto().endsWith("IMG_10.jpg"),
          "a dropped file opens");
    waitFor([&] { return !first->busy(); }, "dropped photo decoded");
    window.trigger("compare");
    // Status messages below are the copy's own: let the decodes report first.
    waitFor(
        [&] {
          return !first->busy() && !second->busy() &&
                 window.statusText().startsWith("Gotowe");
        },
        "views idle before copying");
    pump(300);

    // Onto a folder: new names are copied, nothing is ever overwritten
    // without the question, and every answer keeps the files intact.
    double rx = 0, ry = 0;
    waitFor([&] { return dropPoint(window, holiday, &rx, &ry); }, "folder row");
    check(window.dropFiles({red}, rx, ry, true), "drop onto the folder accepted");
    waitFor(
        [&] {
          return !window.copyingToFolder() &&
                 window.statusText() == "Skopiowano do „Wakacje”: 1 nowy plik.";
        },
        "copied into the folder");
    check(bytesOf(holiday + "/red.jpg") == bytesOf(red), "the copy is byte-identical");
    const auto copiedInode = inodeOf(holiday + "/red.jpg");
    window.dropFiles({red}, rx, ry, true);
    waitFor([&] { return dialogOf(gtkWindow) != nullptr; }, "same-file notice");
    check(!press(dialogOf(gtkWindow), "Nadpisz") &&
              press(dialogOf(gtkWindow), "Zamknij"),
          "the same file: a notice without an overwrite button");
    waitFor(
        [&] {
          return !window.copyingToFolder() && !dialogOf(gtkWindow) &&
                 window.statusText() ==
                     "Nic nie skopiowano do „Wakacje”: 1 bez zmian (ten sam plik).";
        },
        "identical file reported");
    check(inodeOf(holiday + "/red.jpg") == copiedInode,
          "an identical file is never rewritten");
    solid(holiday + "/green.jpg", 64, 64, {1, 2, 3});
    const QByteArray kept = bytesOf(holiday + "/green.jpg");
    const auto keptInode = inodeOf(holiday + "/green.jpg");
    const QStringList pair{green, photos + "/IMG_2.jpg"};
    window.dropFiles(pair, rx, ry, true);
    waitFor([&] { return dialogOf(gtkWindow) != nullptr; }, "overwrite question");
    shot(GTK_WIDGET(dialogOf(gtkWindow)), "08b-overwrite-question");
    check(press(dialogOf(gtkWindow), "Anuluj"), "cancel button");
    waitFor([&] { return !dialogOf(gtkWindow); }, "question closed");
    check(window.statusText() == "Kopiowanie anulowane. Nic nie zmieniono." &&
              bytesOf(holiday + "/green.jpg") == kept &&
              inodeOf(holiday + "/green.jpg") == keptInode &&
              !QFileInfo::exists(holiday + "/IMG_2.jpg"),
          "Anuluj changes nothing");
    window.dropFiles(pair, rx, ry, true);
    waitFor([&] { return dialogOf(gtkWindow) != nullptr; }, "question again");
    check(press(dialogOf(gtkWindow), "Pomiń istniejące"), "skip button");
    waitFor(
        [&] {
          return !window.copyingToFolder() &&
                 window.statusText() ==
                     "Skopiowano do „Wakacje”: 1 nowy plik, 1 pominięty.";
        },
        "skip copies only new names");
    check(bytesOf(holiday + "/green.jpg") == kept &&
              inodeOf(holiday + "/green.jpg") == keptInode &&
              bytesOf(holiday + "/IMG_2.jpg") == bytesOf(photos + "/IMG_2.jpg"),
          "Pomiń istniejące keeps the existing file");
    window.dropFiles({green}, rx, ry, true);
    waitFor([&] { return dialogOf(gtkWindow) != nullptr; }, "single question");
    check(!press(dialogOf(gtkWindow), "Pomiń istniejące") &&
              press(dialogOf(gtkWindow), "Nadpisz"),
          "with no new names only cancel and overwrite are offered");
    waitFor(
        [&] {
          return !window.copyingToFolder() &&
                 window.statusText() == "Skopiowano do „Wakacje”: 1 nadpisany.";
        },
        "overwrite finished");
    check(bytesOf(holiday + "/green.jpg") == bytesOf(green) && noTemporaries(holiday),
          "Nadpisz replaces the file, no temporary file left");
    for (const auto &name : {"red.jpg", "green.jpg", "IMG_2.jpg"})
      QFile::remove(holiday + "/" + name);
    window.open(before);
    waitFor([&] { return !first->busy(); }, "photo restored after drops");
    library->selectRows({int(library->names().indexOf("red.jpg")),
                         int(library->names().indexOf("green.jpg"))});
  }

  // --- Delete: always a question first (Anuluj by default), then only the
  // system trash of the test folder; never a permanent deletion ---
  {
    const QString bin = work + "/kosz",
                  trash = qEnvironmentVariable("XDG_DATA_HOME") + "/Trash",
                  shown = window.currentPhoto();
    QDir().mkpath(bin + "/podfolder");
    for (const char *name : {"k1.jpg", "k2.jpg", "k3.jpg", "k4.jpg"})
      solid(bin + "/" + name, 48, 32, {200, 40, 40});
    const auto k1 = inodeOf(bin + "/k1.jpg"), k2 = inodeOf(bin + "/k2.jpg");
    check(!window.compareVisible(), "no comparison before the trash checks");
    window.setLibraryMode(false);
    window.browse(bin);
    waitFor([&] { return !library->scanning() && library->count() == 5; },
            "trash folder listed");
    auto row = [&](const char *name) { return int(library->names().indexOf(name)); };
    auto *list = library->focusWidget();
    check(keyShortcut(list, GDK_KEY_Delete, false) &&
              !keyShortcut(GTK_WIDGET(gtkWindow), GDK_KEY_Delete, false),
          "Delete belongs to the library views, not to the whole window "
          "(the path entry keeps its own Delete)");
    library->selectRows({});
    check(keyShortcut(list, GDK_KEY_Delete, true) && !dialogOf(gtkWindow) &&
              window.statusText() ==
                  "Zaznacz zdjęcia w bibliotece, aby przenieść je do kosza.",
          "nothing selected: no question");
    library->selectRows({row("podfolder")});
    window.trigger("trashSelection");
    check(!dialogOf(gtkWindow) && QFileInfo(bin + "/podfolder").isDir() &&
              window.statusText() == "Foldery nie trafiają do kosza. Zaznacz zdjęcia.",
          "a folder alone: no question");
    library->selectRows({row("k1.jpg"), row("k2.jpg"), row("podfolder")});
    check(keyShortcut(list, GDK_KEY_Delete, true), "Delete on the list");
    waitFor([&] { return dialogOf(gtkWindow) != nullptr; }, "trash question");
    auto *question = dialogOf(gtkWindow);
    shot(GTK_WIDGET(question), "08c-trash-question");
    const QString asked = textOf(GTK_WIDGET(question));
    check(asked.contains("2 pliki z folderu „kosz” zostaną przeniesione do kosza "
                         "systemowego") &&
              asked.contains("1 zaznaczony folder pozostaje bez zmian.") &&
              asked.contains("k1.jpg — ") && asked.contains("k2.jpg — ") &&
              !asked.contains("k3.jpg"),
          "the question lists exactly the selected files");
    auto *byDefault = gtk_window_get_default_widget(question);
    check(byDefault && textOf(byDefault).trimmed() == "Anuluj",
          "Enter answers Anuluj");
    check(press(question, "Anuluj"), "Anuluj button");
    waitFor([&] { return !dialogOf(gtkWindow) && !window.movingToTrash(); },
            "question closed");
    check(window.statusText() == "Anulowano. Nic nie przeniesiono do kosza." &&
              inodeOf(bin + "/k1.jpg") == k1 && inodeOf(bin + "/k2.jpg") == k2 &&
              !QFileInfo::exists(trash + "/files/k1.jpg"),
          "Anuluj changes nothing");
    window.trigger("trashSelection");
    waitFor([&] { return dialogOf(gtkWindow) != nullptr; }, "question again");
    closeDialog(dialogOf(gtkWindow)); // Esc and the close button
    waitFor([&] { return !dialogOf(gtkWindow); }, "question dismissed");
    check(window.statusText() == "Anulowano. Nic nie przeniesiono do kosza." &&
              inodeOf(bin + "/k1.jpg") == k1 && inodeOf(bin + "/k2.jpg") == k2,
          "closing the question changes nothing");
    // Right click on an unselected thumbnail selects it, then offers the menu.
    window.setLibraryMode(true);
    pump(400);
    auto *grid = library->focusWidget();
    double tx = 0, ty = 0;
    waitFor([&] { return library->pointOf(bin + "/k3.jpg", &tx, &ty); },
            "thumbnail on screen");
    check(keyShortcut(grid, GDK_KEY_Delete, false), "Delete on the thumbnails");
    auto *click = secondaryClick(grid);
    check(click, "the thumbnails have a right-click menu");
    g_signal_emit_by_name(click, "pressed", 1, tx, ty);
    pump(200);
    check(library->selectedFiles() == QStringList{bin + "/k3.jpg"},
          "right click selects the file under the pointer");
    auto *menu = openPopoverWith(content(gtkWindow), "Przenieś do kosza…");
    check(menu, "library menu shown");
    // The path of the file under the pointer, as plain text.
    check(pressIn(menu, "Kopiuj ścieżkę pliku"), "the thumbnails' menu offers the path");
    check(!gtk_widget_get_visible(menu), "the menu closes after copying the path");
    check(window.statusText() == "Skopiowano ścieżkę do schowka: " + bin + "/k3.jpg",
          "status names the copied path");
    check(clipboardText(GTK_WIDGET(gtkWindow)) == bin + "/k3.jpg" &&
              clipboardTextOnly(GTK_WIDGET(gtkWindow)),
          "the clipboard holds the thumbnail's path as plain text only");
    check(library->selectedFiles() == QStringList{bin + "/k3.jpg"} &&
              QFileInfo::exists(bin + "/k3.jpg"),
          "copying the path changes nothing in the library");
    g_signal_emit_by_name(click, "pressed", 1, tx, ty);
    pump(200);
    check(openPopoverWith(content(gtkWindow), "Przenieś do kosza…") == menu,
          "library menu shown again");
    check(pressIn(menu, "Przenieś do kosza…"), "menu button");
    waitFor([&] { return dialogOf(gtkWindow) != nullptr; }, "question from the menu");
    check(textOf(GTK_WIDGET(dialogOf(gtkWindow)))
              .contains("Plik „k3.jpg” zostanie przeniesiony z folderu „kosz” do "
                        "kosza systemowego."),
          "the menu asks about the file under the pointer");
    check(press(dialogOf(gtkWindow), "Anuluj"), "Anuluj from the menu");
    waitFor([&] { return !dialogOf(gtkWindow); }, "menu question closed");
    check(QFileInfo::exists(bin + "/k3.jpg"), "still there after Anuluj");
    // The list, several files selected: a right click on one of them keeps
    // the selection, and the menu copies every selected file's path, one per
    // line (folders do not count).
    {
      window.setLibraryMode(false);
      pump(300);
      library->selectRows({row("k1.jpg"), row("k2.jpg"), row("podfolder")});
      double lx = 0, ly = 0;
      waitFor([&] { return library->pointOf(bin + "/k2.jpg", &lx, &ly); },
              "row on screen");
      auto *listClick = secondaryClick(library->focusWidget());
      check(listClick, "the list has a right-click menu");
      g_signal_emit_by_name(listClick, "pressed", 1, lx, ly);
      pump(200);
      auto *listMenu = openPopoverWith(content(gtkWindow), "Kopiuj ścieżki plików");
      check(listMenu, "with several files selected the menu says paths");
      check(pressIn(listMenu, "Kopiuj ścieżki plików"), "copy the selected paths");
      check(clipboardText(GTK_WIDGET(gtkWindow)) ==
                    bin + "/k1.jpg\n" + bin + "/k2.jpg" &&
                clipboardTextOnly(GTK_WIDGET(gtkWindow)),
            "two paths, one per line, no folder, no newline at the end");
      check(window.statusText() == "Skopiowano do schowka 2 ścieżki.",
            "status counts the paths");
      // One file selected again: the caption is singular.
      library->selectRows({row("k2.jpg")});
      g_signal_emit_by_name(listClick, "pressed", 1, lx, ly);
      pump(200);
      auto *single = openPopoverWith(content(gtkWindow), "Kopiuj ścieżkę pliku");
      check(single, "one selected file: the menu says path");
      gtk_popover_popdown(GTK_POPOVER(single));
      pump(150);
    }

    // Qt before 6.7 picks the trash by the disk of HOME: then the test folder
    // could reach a trash outside it, and only the questions are checked here
    // (file_trash covers the move itself).
    const bool ownTrash =
        QLibraryInfo::version() >= QVersionNumber(6, 7) ||
        QStorageInfo(bin) == QStorageInfo(QDir::home());
    if (ownTrash) {
      window.setLibraryMode(false);
      pump(300);
      library->selectRows({row("k1.jpg"), row("k2.jpg"), row("podfolder")});
      window.trigger("trashSelection");
      waitFor([&] { return dialogOf(gtkWindow) != nullptr; }, "question to confirm");
      check(press(dialogOf(gtkWindow), "Przenieś 2 pliki do kosza"),
            "confirm button names the count");
      waitFor([&] {
        return !window.movingToTrash() &&
               window.statusText() == "Przeniesiono do kosza 2 pliki.";
      }, "moved to the trash");
      check(!QFileInfo::exists(bin + "/k1.jpg") && !QFileInfo::exists(bin + "/k2.jpg") &&
                inodeOf(trash + "/files/k1.jpg") == k1 &&
                inodeOf(trash + "/files/k2.jpg") == k2 &&
                QFileInfo::exists(trash + "/info/k1.jpg.trashinfo") &&
                QFileInfo(bin + "/podfolder").isDir(),
            "the very same files are in the test folder's trash, the folder stays");
      waitFor([&] { return !library->scanning() && library->count() == 3; },
              "library lists what is left");
      // The displayed photo: the next one of the folder takes its place.
      window.open(bin + "/k3.jpg");
      waitFor([&] { return window.currentPhoto() == bin + "/k3.jpg" && !first->busy(); },
              "k3 shown");
      pump(300);
      window.trigger("trashPhoto");
      waitFor([&] { return dialogOf(gtkWindow) != nullptr; }, "question for the photo");
      check(press(dialogOf(gtkWindow), "Przenieś do kosza"), "confirm the photo");
      waitFor([&] {
        return !window.movingToTrash() &&
               window.statusText() == "Przeniesiono do kosza 1 plik.";
      }, "photo moved");
      waitFor([&] { return window.currentPhoto() == bin + "/k4.jpg"; },
              "the next photo replaces the one in the trash");
      // Delete on the photo itself; the last photo leaves the welcome page.
      waitFor([&] { return !first->busy(); }, "k4 decoded");
      check(keyShortcut(first->focusWidget(), GDK_KEY_Delete, true),
            "Delete on the photo");
      waitFor([&] { return dialogOf(gtkWindow) != nullptr; }, "question from the photo");
      check(textOf(GTK_WIDGET(dialogOf(gtkWindow))).contains("Plik „k4.jpg”"),
            "Delete on the photo asks about that photo only");
      check(press(dialogOf(gtkWindow), "Przenieś do kosza"), "confirm k4");
      waitFor([&] { return !window.movingToTrash() && window.welcomeVisible(); },
              "no photo left to show");
      check(window.currentPhoto().isEmpty() && QFileInfo::exists(trash + "/files/k4.jpg"),
            "nothing shows a photo that left");
      // The compared photo: the next one that is not the left photo takes its
      // place; with none left the comparison closes.
      for (const char *name : {"k5.jpg", "k6.jpg", "k7.jpg"})
        solid(bin + "/" + name, 40, 30, {40, 40, 200});
      window.open(bin + "/k5.jpg");
      window.showComparison(bin + "/k6.jpg");
      waitFor([&] {
        return window.compareVisible() && !first->busy() && !second->busy();
      }, "comparison of k5 and k6");
      pump(300);
      check(keyShortcut(second->focusWidget(), GDK_KEY_Delete, true),
            "Delete on the compared photo");
      waitFor([&] { return dialogOf(gtkWindow) != nullptr; }, "question for the compared photo");
      check(textOf(GTK_WIDGET(dialogOf(gtkWindow))).contains("Plik „k6.jpg”"),
            "Delete on the compared photo asks about that photo only");
      check(press(dialogOf(gtkWindow), "Przenieś do kosza"), "confirm k6");
      waitFor([&] {
        return !window.movingToTrash() && window.comparedPhoto() == bin + "/k7.jpg";
      }, "the next photo takes the compared one's place");
      check(window.currentPhoto() == bin + "/k5.jpg", "the left photo stays");
      waitFor([&] { return !second->busy(); }, "k7 decoded");
      check(keyShortcut(second->focusWidget(), GDK_KEY_Delete, true), "Delete on k7");
      waitFor([&] { return dialogOf(gtkWindow) != nullptr; }, "question for k7");
      check(press(dialogOf(gtkWindow), "Przenieś do kosza"), "confirm k7");
      waitFor([&] { return !window.movingToTrash() && !window.compareVisible(); },
              "no photo left for the comparison: it closes");
      check(window.currentPhoto() == bin + "/k5.jpg" &&
                QFileInfo::exists(trash + "/files/k7.jpg") &&
                QFileInfo::exists(bin + "/k5.jpg"),
            "the left photo never fills the comparison");
    } else {
      std::cout << "trash: Qt " << qVersion()
                << " and the test folder on another disk than HOME: only the "
                   "questions are checked here\n";
    }
    window.setLibraryMode(true);
    window.browse(photos);
    waitFor([&] { return !library->scanning() && library->count() == 7; },
            "photos listed again");
    window.open(shown);
    waitFor([&] { return window.currentPhoto() == shown && !first->busy(); },
            "photo restored after the trash");
    library->selectRows({int(library->names().indexOf("red.jpg")),
                         int(library->names().indexOf("green.jpg"))});
    pump(200);
  }

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

  // --- clipboard: the photo as an sRGB PNG and as the file itself ---
  // (This replaces the session clipboard while the test runs.)
  {
    const QString redPath = photos + "/red.jpg";
    window.open(redPath);
    waitFor([&] { return window.currentPhoto() == redPath && !first->busy(); },
            "photo for the clipboard");
    check(window.trigger("copyImage"), "copy-to-clipboard action");
    waitFor(
        [&] {
          return !window.copyingToClipboard() &&
                 window.statusText().startsWith("Skopiowano do schowka:");
        },
        "clipboard filled");
    check(window.statusText() ==
              "Skopiowano do schowka: red.jpg (640 × 480 px)",
          "full resolution in the clipboard");
    auto *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(gtkWindow));
    auto *formats = gdk_clipboard_get_formats(clipboard);
    check(gdk_content_formats_contain_mime_type(formats, "image/png") &&
              gdk_content_formats_contain_mime_type(formats, "text/uri-list") &&
              gdk_content_formats_contain_mime_type(
                  formats, "x-special/gnome-copied-files"),
          "clipboard offers the image and the file");
    struct Read {
      GdkTexture *texture = nullptr;
      QStringList files; // copied: the clipboard owns the GFile list
      int pending = 2;
    } read;
    gdk_clipboard_read_texture_async(
        clipboard, nullptr,
        +[](GObject *source, GAsyncResult *result, gpointer data) {
          auto *state = static_cast<Read *>(data);
          state->texture = gdk_clipboard_read_texture_finish(
              GDK_CLIPBOARD(source), result, nullptr);
          --state->pending;
        },
        &read);
    gdk_clipboard_read_value_async(
        clipboard, GDK_TYPE_FILE_LIST, G_PRIORITY_DEFAULT, nullptr,
        +[](GObject *source, GAsyncResult *result, gpointer data) {
          auto *state = static_cast<Read *>(data);
          if (const GValue *value = gdk_clipboard_read_value_finish(
                  GDK_CLIPBOARD(source), result, nullptr)) {
            GSList *list = gdk_file_list_get_files(
                static_cast<GdkFileList *>(g_value_get_boxed(value)));
            for (GSList *l = list; l; l = l->next) {
              g_autofree char *path = g_file_get_path(G_FILE(l->data));
              state->files.append(QString::fromUtf8(path));
            }
            g_slist_free(list);
          }
          --state->pending;
        },
        &read);
    waitFor([&] { return read.pending == 0; }, "clipboard read back");
    check(read.texture && gdk_texture_get_width(read.texture) == 640 &&
              gdk_texture_get_height(read.texture) == 480,
          "pasted image has the photo's size");
    QImage pasted(640, 480, QImage::Format_ARGB32_Premultiplied);
    gdk_texture_download(read.texture, pasted.bits(), gsize(pasted.bytesPerLine()));
    const QColor middle = pasted.pixelColor(320, 240);
    check(near(middle.red(), 200, 3) && near(middle.green(), 40, 3) &&
              near(middle.blue(), 30, 3) && middle.alpha() == 255,
          "pasted image keeps the photo's colours");
    g_object_unref(read.texture);
    check(read.files == QStringList{redPath}, "pasted file is the photo itself");

    // --- export: byte-identical copy, extension kept, never overwrites ---
    const QString target = work + "/eksport-red";
    window.exportTo(target);
    waitFor(
        [&] {
          return !window.exporting() &&
                 window.statusText().startsWith("Wyeksportowano kopię:");
        },
        "export finished");
    QFile original(redPath), exported(target + ".jpg");
    check(original.open(QIODevice::ReadOnly) &&
              exported.open(QIODevice::ReadOnly) &&
              original.readAll() == exported.readAll(),
          "export is byte-identical, with the original extension");
    window.exportTo(target + ".jpg");
    check(window.statusText().startsWith("Nie wyeksportowano:") &&
              !window.exporting(),
          "export never overwrites an existing file");
    pump(300);
    closeDialog(dialogOf(gtkWindow));

    // --- right-click menu on the photo ---
    window.showPhotoMenu(120, 120);
    pump(300);
    GtkWidget *menu = nullptr;
    std::function<void(GtkWidget *)> find = [&](GtkWidget *w) {
      if (GTK_IS_POPOVER(w) && gtk_widget_has_css_class(w, "prism-menu"))
        menu = w;
      for (auto *c = gtk_widget_get_first_child(w); c && !menu;
           c = gtk_widget_get_next_sibling(c))
        find(c);
    };
    find(GTK_WIDGET(gtkWindow));
    check(menu && gtk_widget_get_visible(menu), "photo menu opens");
    shot(menu, "19-photo-menu");
    gtk_popover_popdown(GTK_POPOVER(menu));
    pump(150);
    check(!gtk_widget_get_visible(menu), "photo menu closes");
    check(window.trigger("photoMenu"), "keyboard photo-menu action");
    pump(300);
    check(gtk_widget_get_visible(menu), "Menu key / Shift+F10 open the menu");
    // "Kopiuj ścieżkę pliku": the displayed photo's path as plain text.
    const QString shownPhoto = window.currentPhoto();
    check(!shownPhoto.isEmpty() && pressIn(menu, "Kopiuj ścieżkę pliku"),
          "the photo menu offers the path");
    check(!gtk_widget_get_visible(menu), "the photo menu closes after copying the path");
    check(window.statusText() == "Skopiowano ścieżkę do schowka: " + shownPhoto,
          "status names the photo's path");
    check(clipboardText(GTK_WIDGET(gtkWindow)) == shownPhoto &&
              clipboardTextOnly(GTK_WIDGET(gtkWindow)),
          "the clipboard holds the photo's path as plain text only");
    // A copy of the photo still being prepared does not replace the path.
    check(window.trigger("copyImage") && window.trigger("copyPath"),
          "copy the photo, then at once its path");
    waitFor([&] { return !window.copyingToClipboard(); }, "photo copy stopped");
    pump(300);
    check(clipboardText(GTK_WIDGET(gtkWindow)) == shownPhoto &&
              clipboardTextOnly(GTK_WIDGET(gtkWindow)) &&
              window.statusText() == "Skopiowano ścieżkę do schowka: " + shownPhoto,
          "the path stays in the clipboard");
    gtk_popover_popdown(GTK_POPOVER(menu));
    pump(150);
  }

  // --- printing: the same pages the printer gets, written as PDF ---
  {
    auto pdfPages = [](const QString &file) {
      QFile pdf(file);
      if (!pdf.open(QIODevice::ReadOnly))
        return -1;
      // Cairo writes PDF 1.7 with compressed object streams: inflate every
      // stream, then count "/Type /Page" objects ("/Pages" is the tree).
      const QByteArray bytes = pdf.readAll();
      QByteArray text = bytes;
      for (qsizetype at = bytes.indexOf("stream"); at >= 0; at = bytes.indexOf("stream", at + 6)) {
        if (at >= 3 && bytes.mid(at - 3, 3) == "end")
          continue;
        qsizetype begin = at + 6;
        while (begin < bytes.size() && (bytes[begin] == '\r' || bytes[begin] == '\n'))
          ++begin;
        const qsizetype stop = bytes.indexOf("endstream", begin);
        if (stop < 0)
          break;
        z_stream z{};
        if (inflateInit(&z) != Z_OK)
          continue;
        z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(bytes.constData() + begin));
        z.avail_in = uInt(stop - begin);
        char chunk[65536];
        int rc = Z_OK;
        while (rc == Z_OK) {
          z.next_out = reinterpret_cast<Bytef *>(chunk);
          z.avail_out = sizeof chunk;
          rc = inflate(&z, Z_NO_FLUSH);
          text.append(chunk, qsizetype(sizeof chunk - z.avail_out));
        }
        inflateEnd(&z);
      }
      auto it = QRegularExpression("/Type\\s*/Page[^s]").globalMatch(QString::fromLatin1(text));
      int pages = 0;
      for (; it.hasNext(); it.next())
        ++pages;
      return pages;
    };
    const QString redPath = photos + "/red.jpg";
    window.open(redPath);
    waitFor([&] { return window.currentPhoto() == redPath && !first->busy(); }, "photo for printing");
    library->selectRows({});
    PrintOptions options;
    const QString single = work + "/druk-1.pdf";
    check(window.printToPdf(single, options), "print job starts");
    waitFor([&] { return !window.printing(); }, "single photo printed", 60000);
    check(window.statusText() == "Zapisano PDF: 1 strona" && pdfPages(single) == 1,
          "one photo, one page");
    // One photo four times on a sheet, greyscale, with captions.
    options.perPage = 4;
    options.grayscale = true;
    options.caption = true;
    const QString sheet = work + "/druk-4.pdf";
    check(window.printToPdf(sheet, options), "sheet job starts");
    waitFor([&] { return !window.printing(); }, "sheet printed", 60000);
    check(pdfPages(sheet) == 1, "repeated photo stays on one sheet");
    // Five selected photos, four per sheet: two pages.
    QList<int> rows;
    const auto names = library->names();
    for (const char *name : {"IMG_2.jpg", "IMG_10.jpg", "blue.png", "green.jpg", "red.jpg"})
      rows.append(int(names.indexOf(QString(name))));
    library->selectRows(rows);
    pump(100);
    const QString several = work + "/druk-5.pdf";
    options.grayscale = false;
    options.size = PrintOptions::Fixed;
    options.widthMm = 90;
    options.heightMm = 130;
    check(window.printToPdf(several, options), "selection job starts");
    waitFor([&] { return !window.printing(); }, "selection printed", 60000);
    check(window.statusText() == "Zapisano PDF: 2 strony" && pdfPages(several) == 2,
          "five photos, four per sheet: two pages");
    library->selectRows({});
    // PRISM_PRINT_PDF_DIR keeps the PDFs for a visual check.
    if (const auto keep = qEnvironmentVariable("PRISM_PRINT_PDF_DIR"); !keep.isEmpty())
      for (const auto &file : {single, sheet, several})
        QFile::copy(file, keep + "/" + QFileInfo(file).fileName());
    // The system dialog with the "Zdjęcie" tab.
    check(window.trigger("print"), "print action");
    GtkWindow *dialog = nullptr;
    waitFor([&] {
      GList *all = gtk_window_list_toplevels();
      for (GList *l = all; l; l = l->next)
        if (QString(G_OBJECT_TYPE_NAME(l->data)) == "GtkPrintUnixDialog" &&
            gtk_widget_get_visible(GTK_WIDGET(l->data)))
          dialog = GTK_WINDOW(l->data);
      g_list_free(all);
      return dialog != nullptr;
    }, "print dialog shown", 20000);
    bool photoTab = false;
    if (auto *tabs = findType(GTK_WIDGET(dialog), GTK_TYPE_NOTEBOOK)) {
      for (int i = 0; i < gtk_notebook_get_n_pages(GTK_NOTEBOOK(tabs)); ++i) {
        auto *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(tabs), i);
        auto *label = gtk_notebook_get_tab_label(GTK_NOTEBOOK(tabs), page);
        if (label && GTK_IS_LABEL(label) &&
            QString::fromUtf8(gtk_label_get_text(GTK_LABEL(label))) == "Zdjęcie") {
          gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs), i);
          photoTab = true;
        }
      }
      pump(2000); // previews are decoded off the main thread
    }
    check(photoTab, "print dialog has the \"Zdjęcie\" tab");
    shot(GTK_WIDGET(dialog), "20-print-dialog");
    gtk_window_close(dialog);
    waitFor([&] { return !window.printing(); }, "print dialog closed", 20000);
    check(window.statusText() == "Drukowanie anulowane.", "closing the dialog cancels");
    // "Print to File" through the dialog: the real job path (GtkPrintJob),
    // without paper. Saved settings preselect the printer and the file.
    {
      const QString printed = work + "/przez-okno.pdf";
      QDir().mkpath(config + "/ky-prism");
      QFile ini(config + "/ky-prism/print.ini");
      check(ini.open(QIODevice::WriteOnly), "print settings fixture");
      // GTK names its file printer in the session language.
      const QString fileprinter = QString::fromUtf8(g_dgettext("gtk40", "Print to File"));
      ini.write(QString("[Print Settings]\nprinter=%1\noutput-file-format=pdf\n"
                        "output-uri=file://%2\nky-prism-per-page=2\n")
                    .arg(fileprinter, printed)
                    .toUtf8());
      ini.close();
      check(window.trigger("print"), "print action for a file");
      GtkWidget *printButton = nullptr;
      dialog = nullptr;
      waitFor([&] {
        GList *all = gtk_window_list_toplevels();
        for (GList *l = all; l; l = l->next)
          if (QString(G_OBJECT_TYPE_NAME(l->data)) == "GtkPrintUnixDialog" &&
              gtk_widget_get_visible(GTK_WIDGET(l->data)))
            dialog = GTK_WINDOW(l->data);
        g_list_free(all);
        if (!dialog)
          return false;
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        printButton = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
        G_GNUC_END_IGNORE_DEPRECATIONS
        return printButton && gtk_widget_get_sensitive(printButton);
      }, "Print to File selected", 30000);
      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
      G_GNUC_END_IGNORE_DEPRECATIONS
      waitFor([&] { return !window.printing(); }, "job sent", 60000);
      check(window.statusText() == "Wysłano do drukarki: 1 strona" && pdfPages(printed) == 1,
            "dialog job: one photo, two per sheet, one page in the file");
      QFile saved(config + "/ky-prism/print.ini");
      check(saved.open(QIODevice::ReadOnly) && saved.readAll().contains("ky-prism-per-page=2"),
            "photo options remembered");
    }
  }

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

  // --- close while the overwrite question is open: nothing is written ---
  const QString askedFile = photos + "/Wakacje/red.jpg";
  solid(askedFile, 32, 32, {9, 9, 9});
  const QByteArray asked = bytesOf(askedFile);
  {
    double rx = 0, ry = 0;
    window.setLibraryMode(false);
    pump(300);
    waitFor([&] { return dropPoint(window, photos + "/Wakacje", &rx, &ry); },
            "folder row before closing");
    window.dropFiles({photos + "/red.jpg"}, rx, ry, true);
    waitFor([&] { return dialogOf(gtkWindow) != nullptr; }, "question before closing");
  }

  // --- close while a decode is in flight ---
  window.open(photos + "/IMG_10.jpg");
  window.close();
  check(bytesOf(askedFile) == asked && noTemporaries(photos + "/Wakacje"),
        "closing while asking overwrites nothing");
  check(closedSeen && window.shuttingDown(), "close cancels work and reports");
  check(!window.trigger("open"), "no action runs after shutdown began");
}
// Closing the window while the trash question is open moves nothing.
void closeWhileTrashAsked(const QString &work) {
  const QString bin = work + "/kosz-zamkniecie",
                trash = qEnvironmentVariable("XDG_DATA_HOME") + "/Trash";
  QDir().mkpath(bin);
  solid(bin + "/z.jpg", 24, 24, {10, 200, 10});
  const auto before = inodeOf(bin + "/z.jpg");
  PrismWindow window;
  window.show();
  auto *gtkWindow = window.window();
  waitFor([&] { return gtk_widget_get_mapped(GTK_WIDGET(gtkWindow)); },
          "second window mapped");
  window.browse(bin);
  auto *library = window.library();
  waitFor([&] { return !library->scanning() && library->count() == 1; },
          "folder of the close test listed");
  library->selectRows({0});
  window.trigger("trashSelection");
  waitFor([&] { return dialogOf(gtkWindow) != nullptr; },
          "trash question before closing");
  window.close();
  pump(300);
  check(inodeOf(bin + "/z.jpg") == before &&
            !QFileInfo::exists(trash + "/files/z.jpg") && !window.movingToTrash(),
        "closing while asking about the trash moves nothing");
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
long rssKiB() {
  // Return freed heap pages first: measure what is held, not fragmentation.
#if defined(__GLIBC__)
  malloc_trim(0);
#endif
  QFile status("/proc/self/status");
  if (!status.open(QIODevice::ReadOnly))
    return -1;
  for (const auto &line : status.readAll().split('\n'))
    if (line.startsWith("VmRSS:"))
      return line.mid(6).trimmed().split(' ').value(0).toLong();
  return -1;
}
// Runs the main loop until `done`, recording the longest single dispatch: a
// stall the user would feel as a frozen window.
qint64 longestStall = 0;
QString stallPhase;
void runUntil(const std::function<bool()> &done, const char *what,
              int timeout = 60000) {
  const qint64 before = longestStall;
  longestStall = 0;
  QElapsedTimer total, step;
  total.start();
  while (!done() && total.elapsed() < timeout) {
    step.start();
    while (g_main_context_iteration(nullptr, FALSE)) {
      longestStall = std::max(longestStall, step.elapsed());
      step.start();
    }
    g_usleep(1000);
  }
  if (longestStall >= 100)
    std::cout << "  stall " << longestStall << " ms during: " << what << "\n";
  longestStall = std::max(before, longestStall);
  if (!done())
    throw std::runtime_error(std::string("timeout: ") + what);
}
int stress(const QString &work, int rounds) {
  // 24 Mpx with noise: realistic decode and PNG sizes, nothing compresses away.
  const QString big = work + "/big.jpg", small = work + "/small.jpg";
  vips::VImage::gaussnoise(6000, 4000,
                           vips::VImage::option()->set("mean", 128.0)->set(
                               "sigma", 40.0))
      .bandjoin({vips::VImage::gaussnoise(6000, 4000),
                 vips::VImage::gaussnoise(6000, 4000)})
      .cast(VIPS_FORMAT_UCHAR)
      .copy(vips::VImage::option()->set("interpretation",
                                        VIPS_INTERPRETATION_sRGB))
      .jpegsave(big.toUtf8().constData(), vips::VImage::option()->set("Q", 90));
  solid(small, 1600, 1200, {180, 150, 90});
  PrismWindow window;
  window.show();
  runUntil([&] { return gtk_widget_get_mapped(GTK_WIDGET(window.window())); },
           "window mapped");
  const auto view = window.primaryView();
  QList<long> rss;
  const QString only = qEnvironmentVariable("PRISM_STRESS_ONLY");
  const bool doCopy = only.isEmpty() || only == "copy",
             doExport = only.isEmpty() || only == "export";
  for (int round = 0; round < rounds; ++round) {
    window.open(small);
    runUntil([&] { return window.currentPhoto() == small && !view->busy(); },
             "small photo");
    window.open(big);
    runUntil([&] {
      return window.currentPhoto() == big && !view->busy() &&
             view->imageSize() == QSize(6000, 4000);
    }, "big photo");
    pump(200);
    const long opened = rssKiB();
    if (doCopy) {
      window.trigger("copyImage");
      runUntil([&] {
        return !window.copyingToClipboard() &&
               window.statusText().startsWith("Skopiowano do schowka:");
      }, "clipboard copy");
    }
    pump(200);
    const long copied = rssKiB();
    if (doExport) {
      const QString target = work + QString("/export-%1.jpg").arg(round);
      window.exportTo(target);
      runUntil([&] {
        return !window.exporting() &&
               window.statusText().startsWith("Wyeksportowano kopię:");
      }, "export");
      QFile::remove(target);
    }
    pump(200);
    rss.append(rssKiB());
    std::cout << "round " << round + 1 << ": RSS after open " << opened / 1024
              << " MiB, after copy " << copied / 1024 << " MiB, after export "
              << rss.last() / 1024 << " MiB, longest stall " << longestStall
              << " ms\n";
    if (round == 0)
      longestStall = 0; // first GL frame, window mapping: measured above
  }
  window.close();
  pump(200);
  // The first half warms bounded caches (previews, tiles, thumbnails); the
  // second half must stay flat. Averages smooth out allocator noise.
  auto average = [&](int from, int count) {
    long sum = 0;
    for (int i = from; i < from + count; ++i)
      sum += rss.at(i);
    return sum / std::max(1, count);
  };
  const int n = int(rss.size()), window3 = std::min(3, n);
  const long growth = average(n - window3, window3) -
                      average(std::max(0, n / 2 - 1), window3);
  std::cout << "stress: growth over the second half " << growth / 1024
            << " MiB, longest stall " << longestStall << " ms\n";
  // The first round includes the first GL frame (shader compilation when the
  // driver's cache is cold); every later round must keep the window fluid.
  return growth < 64 * 1024 && longestStall < 250 ? 0 : 1;
}
} // namespace

// Runs xdotool while this process keeps handling events: the drag source and
// the drop target both live here.
bool xdotool(const QStringList &args) {
  std::vector<QByteArray> bytes{"xdotool"};
  for (const auto &a : args)
    bytes.push_back(a.toUtf8());
  std::vector<char *> argv;
  for (auto &b : bytes)
    argv.push_back(b.data());
  argv.push_back(nullptr);
  GPid pid = 0;
  GError *error = nullptr;
  if (!g_spawn_async(nullptr, argv.data(), nullptr,
                     GSpawnFlags(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD),
                     nullptr, nullptr, &pid, &error)) {
    g_clear_error(&error);
    return false;
  }
  int status = 0;
  QElapsedTimer timer;
  timer.start();
  while (waitpid(pid, &status, WNOHANG) == 0) {
    if (timer.elapsed() > 30000) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      return false;
    }
    pump(10);
  }
  g_spawn_close_pid(pid);
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
// Real pointer drags on X11 (see the header): click, drag to a folder in the
// list and in the thumbnails, drag onto a photo row, a group drag and the
// overwrite question, drag onto the photo.
int xdotoolDrag(const QString &photos) {
  PrismWindow window;
  window.show();
  auto *gtkWindow = window.window();
  waitFor([&] { return gtk_widget_get_mapped(GTK_WIDGET(gtkWindow)); }, "window mapped");
  auto *display = gtk_widget_get_display(GTK_WIDGET(gtkWindow));
  using Xid = unsigned long (*)(GdkSurface *);
  auto xidOf = reinterpret_cast<Xid>(dlsym(RTLD_DEFAULT, "gdk_x11_surface_get_xid"));
  if (QString(G_OBJECT_TYPE_NAME(display)) != "GdkX11Display" || !xidOf) {
    std::cout << "xdotool drag: needs an X11 display, skipped\n";
    return 77;
  }
  const QString wid =
      QString::number(xidOf(gtk_native_get_surface(GTK_NATIVE(gtkWindow))));
  pump(500);
  window.browse(photos);
  auto *library = window.library();
  try {
    waitFor([&] { return !library->scanning() && library->count() == 6; }, "listed");
  } catch (const std::exception &) {
    std::cerr << "listed: scanning=" << library->scanning() << " dir="
              << window.currentDirectory().toStdString() << " names="
              << library->names().join('|').toStdString() << "\n";
    throw;
  }
  window.setLibraryMode(false);
  pump(500);
  const QString target = photos + "/Cel", red = photos + "/red.jpg",
                green = photos + "/green.jpg", blue = photos + "/blue.jpg";
  auto spot = [&](const QString &path) {
    double rx = 0, ry = 0, sx = 0, sy = 0;
    waitFor([&] { return dropPoint(window, path, &rx, &ry); }, "row on screen");
    graphene_point_t in = Gx::point(float(rx), float(ry)), out;
    check(gtk_widget_compute_point(content(gtkWindow), GTK_WIDGET(gtkWindow), &in, &out),
          "row maps to the window");
    gtk_native_get_surface_transform(GTK_NATIVE(gtkWindow), &sx, &sy);
    return QPoint(int(out.x + sx), int(out.y + sy));
  };
  auto at = [&](QPoint p) {
    return QStringList{"mousemove", "--window", wid, QString::number(p.x()),
                       QString::number(p.y())};
  };
  auto drag = [&](QPoint from, QPoint to) {
    QStringList args = at(from);
    args << "sleep" << "0.3" << "mousedown" << "1" << "sleep" << "0.2";
    for (int step = 1; step <= 6; ++step)
      args << at(from + QPoint(0, 4 * step)) << "sleep" << "0.05";
    args << at(to) << "sleep" << "0.4" << at(to + QPoint(3, 1)) << "sleep" << "0.4"
         << "mouseup" << "1";
    check(xdotool(args), "xdotool drag ran");
    pump(600);
  };
  auto click = [&](QPoint p, bool control = false) {
    QStringList args = at(p);
    args << "sleep" << "0.2";
    if (control)
      args << "keydown" << "ctrl";
    args << "click" << "1";
    if (control)
      args << "keyup" << "ctrl";
    check(xdotool(args), "xdotool click ran");
    pump(400);
  };
  auto copied = [&](const QString &status) {
    waitFor([&] { return !window.copyingToFolder() && window.statusText() == status; },
            "copy status");
  };

  click(spot(green));
  waitFor([&] { return window.currentPhoto() == green; }, "a plain click opens the photo");
  std::cout << "xdotool drag: click opens a photo\n";

  drag(spot(red), spot(target));
  copied("Skopiowano do „Cel”: 1 nowy plik.");
  check(bytesOf(target + "/red.jpg") == bytesOf(red), "dragged copy is byte-identical");
  check(window.currentPhoto() == green, "the drag did not open the dragged photo");
  std::cout << "xdotool drag: list row onto a folder copies\n";

  drag(spot(red), spot(blue));
  pump(800);
  check(window.currentPhoto() == green && !dialogOf(gtkWindow) &&
            QDir(target).entryList(QDir::Files).size() == 1,
        "a drop on a photo row does nothing");
  std::cout << "xdotool drag: drop on a photo row is refused\n";

  solid(target + "/green.jpg", 32, 32, {1, 2, 3});
  const QByteArray kept = bytesOf(target + "/green.jpg");
  library->selectRows({});
  click(spot(red), true);
  click(spot(green), true);
  check(library->selectedFiles().size() == 2, "Ctrl+click selects two photos");
  drag(spot(green), spot(target));
  waitFor([&] { return dialogOf(gtkWindow) != nullptr; }, "overwrite question");
  QString details;
  std::function<void(GtkWidget *)> collect = [&](GtkWidget *w) {
    if (GTK_IS_LABEL(w))
      details += QString::fromUtf8(gtk_label_get_text(GTK_LABEL(w))) + "\n";
    for (auto *c = gtk_widget_get_first_child(w); c; c = gtk_widget_get_next_sibling(c))
      collect(c);
  };
  collect(GTK_WIDGET(dialogOf(gtkWindow)));
  check(details.contains("green.jpg — w folderze") &&
            details.contains("red.jpg — identyczna zawartość"),
        "the group drag carried both photos");
  check(press(dialogOf(gtkWindow), "Anuluj"), "cancel");
  check(bytesOf(target + "/green.jpg") == kept, "Anuluj keeps the file");
  std::cout << "xdotool drag: group drag asks before overwriting\n";

  window.setLibraryMode(true);
  pump(800);
  library->selectRows({});
  drag(spot(blue), spot(target));
  copied("Skopiowano do „Cel”: 1 nowy plik.");
  check(bytesOf(target + "/blue.jpg") == bytesOf(blue), "thumbnail drag copies");
  std::cout << "xdotool drag: thumbnail onto a folder copies\n";

  graphene_rect_t b;
  double sx = 0, sy = 0;
  check(gtk_widget_compute_bounds(window.primaryView()->widget(), GTK_WIDGET(gtkWindow), &b),
        "photo bounds");
  gtk_native_get_surface_transform(GTK_NATIVE(gtkWindow), &sx, &sy);
  drag(spot(blue), QPoint(int(b.origin.x + b.size.width / 2 + sx),
                          int(b.origin.y + b.size.height / 2 + sy)));
  waitFor([&] { return window.currentPhoto() == blue; }, "drop on the photo opens it");
  std::cout << "xdotool drag: drop on the photo opens it\n";
  check(noTemporaries(target), "no temporary files");
  window.close();
  pump(200);
  std::cout << "xdotool drag: ok\n";
  return 0;
}

int main(int argc, char **argv) {
  // Never touch the user's own preferences, favourites or caches.
  QTemporaryDir home;
  if (!home.isValid())
    return 2;
  for (const char *name : {"data", "config", "cache", "photos/Wakacje", "work"})
    QDir().mkpath(home.path() + "/" + name);
  qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
  qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
  // The GPU driver's compiled-shader cache stays where it normally is: a
  // cold cache would add a one-time compile stall that users do not see.
  const QString userCache = qEnvironmentVariableIsEmpty("XDG_CACHE_HOME")
                                ? QDir::homePath() + "/.cache"
                                : qEnvironmentVariable("XDG_CACHE_HOME");
  if (qEnvironmentVariableIsEmpty("__GL_SHADER_DISK_CACHE_PATH"))
    qputenv("__GL_SHADER_DISK_CACHE_PATH", (userCache + "/nvidia").toUtf8());
  if (qEnvironmentVariableIsEmpty("MESA_SHADER_CACHE_DIR"))
    qputenv("MESA_SHADER_CACHE_DIR", (userCache + "/mesa_shader_cache").toUtf8());
  qputenv("XDG_CACHE_HOME", (home.path() + "/cache").toUtf8());
  qputenv("PRISM_LANGUAGE", "pl");
  qunsetenv("PRISM_UI_OPACITY");
  if (qEnvironmentVariableIsEmpty("GSK_RENDERER"))
    qputenv("GSK_RENDERER", "gl");
  QString readmePhotos, readmeOut;
  int stressRounds = 0;
  bool pointerDrags = false;
  for (int i = 1; i < argc; ++i)
    if (QString(argv[i]) == "--xdotool-drag")
      pointerDrags = true;
  for (int i = 1; i + 1 < argc; ++i)
    if (QString(argv[i]) == "--stress")
      stressRounds = std::max(1, QString(argv[i + 1]).toInt());
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
    if (stressRounds) {
      PrismLanguage::initialize();
      Theme::install();
      result = stress(home.path() + "/work", stressRounds);
      Theme::shutdown();
      NvjpegBackend::shutdown();
      Document::clearSharedPreviews();
      vips_shutdown();
      return result;
    }
    if (pointerDrags) {
      PrismLanguage::initialize();
      Theme::install();
      const QString photos = home.path() + "/photos";
      QDir().mkpath(photos + "/Cel");
      solid(photos + "/red.jpg", 640, 480, {200, 40, 30});
      solid(photos + "/green.jpg", 640, 480, {30, 160, 60});
      solid(photos + "/blue.jpg", 320, 200, {20, 60, 190});
      solid(photos + "/IMG_2.jpg", 800, 600, {120, 120, 120});
      result = xdotoolDrag(photos);
      Theme::shutdown();
      NvjpegBackend::shutdown();
      Document::clearSharedPreviews();
      vips_shutdown();
      return result;
    }
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
    closeWhileTrashAsked(home.path() + "/work");
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
