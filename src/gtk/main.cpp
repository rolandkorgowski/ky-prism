// ky-prism — GTK 4 front end.
#include "gtkbase.h"
#include "document.h"
#include "language.h"
#include "nvjpeg_backend.h"
#include "theme.h"
#include "window.h"
#include <QAbstractEventDispatcher>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <glib-unix.h>
#include <iostream>
#include <vips/vips8>

int main(int argc, char **argv) {
  const QString cacheRoot = qEnvironmentVariable(
      "PRISM_CACHE_DIR",
      qEnvironmentVariable("XDG_CACHE_HOME", QDir::homePath() + "/.cache") +
          "/omarchy-prism");
  if (!QDir().mkpath(cacheRoot)) {
    std::cerr << "Nie można utworzyć cache\n";
    return 1;
  }
  QTemporaryDir scratch(cacheRoot + "/session-XXXXXX");
  if (!scratch.isValid()) {
    std::cerr << "Nie można utworzyć katalogu roboczego\n";
    return 1;
  }
  qputenv("TMPDIR", scratch.path().toUtf8());
  qputenv("VIPS_DISC_THRESHOLD", "512m");
  // The engine's timers, futures and process notifications are dispatched by
  // Qt's GLib event dispatcher on the very main context GTK iterates.
  qunsetenv("QT_NO_GLIB");
  // The photo canvas is an OpenGL texture: keep GTK's own compositor on
  // OpenGL as well so tiles are shared on the GPU instead of being copied
  // through another API. An explicit GSK_RENDERER still wins.
  if (qEnvironmentVariableIsEmpty("GSK_RENDERER"))
    qputenv("GSK_RENDERER", "gl");
  if (VIPS_INIT(argv[0]))
    return 1;
  vips_concurrency_set(4);
  vips_cache_set_max_mem(128ULL * 1024 * 1024);
  vips_cache_set_max(0);
  vips_cache_set_max_files(32);
  int rc = 0;
  {
    // Window class / Wayland app_id: matched by the desktop entry and by the
    // Hyprland rule in prism.lua.
    g_set_prgname("ky-prism");
    g_set_application_name("ky-prism");
    QCoreApplication::setApplicationName("ky-prism");
    QCoreApplication::setApplicationVersion(PRISM_VERSION);
    QCoreApplication app(argc, argv);
    PrismLanguage::initialize();
    QCommandLineParser parser;
    parser.setApplicationDescription(
        PrismLanguage::text("ky-prism — kafelkowa przeglądarka zdjęć SDR"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(PrismLanguage::text("zdjęcie"),
                                 PrismLanguage::text("Lokalny plik zdjęcia"));
    parser.addOption(
        {"display-profile",
         PrismLanguage::text("Profil ICC aktualnego monitora (bez "
                             "automatycznego przełączania)."),
         PrismLanguage::text("plik")});
    parser.addOption(
        {"fullscreen", PrismLanguage::text("Uruchom na pełnym ekranie")});
    parser.process(app);
    if (!gtk_init_check()) {
      std::cerr << PrismLanguage::text("ky-prism: nie można otworzyć ekranu (Wayland/X11).\n").toStdString();
      vips_shutdown();
      return 1;
    }
    gtk_window_set_default_icon_name("ky-prism");
    Theme::install();
    guint pump = 0;
    const QByteArray dispatcher =
        app.eventDispatcher()->metaObject()->className();
    if (!dispatcher.contains("Glib"))
      // Qt built without GLib integration: drive its event queue ourselves.
      pump = g_timeout_add(
          5,
          +[](gpointer) -> gboolean {
            QCoreApplication::sendPostedEvents();
            QCoreApplication::processEvents();
            return G_SOURCE_CONTINUE;
          },
          nullptr);
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    {
      PrismWindow window(parser.value("display-profile"));
      window.closed = [loop] { g_main_loop_quit(loop); };
      auto quit = +[](gpointer data) -> gboolean {
        static_cast<PrismWindow *>(data)->close();
        return G_SOURCE_REMOVE;
      };
      const guint onInterrupt = g_unix_signal_add(SIGINT, quit, &window);
      const guint onTerminate = g_unix_signal_add(SIGTERM, quit, &window);
      window.show(parser.isSet("fullscreen"));
      if (!parser.positionalArguments().isEmpty())
        window.open(parser.positionalArguments().first());
      g_main_loop_run(loop);
      // A source that already fired removed itself; ignore those ids.
      for (guint id : {onInterrupt, onTerminate})
        if (g_main_context_find_source_by_id(nullptr, id))
          g_source_remove(id);
    }
    if (pump)
      g_source_remove(pump);
    g_main_loop_unref(loop);
    // Release vendor contexts while Qt and the decoder runtime still exist,
    // rather than depending on global/static destructor ordering at exit.
    NvjpegBackend::shutdown();
    Document::clearSharedPreviews();
    Theme::shutdown();
  }
  vips_shutdown();
  return rc;
}
