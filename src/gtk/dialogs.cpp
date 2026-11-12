#include "dialogs.h"
#include "browser_history.h"
#include "favorites.h"
#include "file_ops_core.h"
#include "icons.h"
#include "language.h"
#include "library_pane.h"
#include "local_path.h"
#include "table.h"
#include "theme.h"
#include "thumbnail_cache.h"
#include "ui_text.h"
#include "usb_entries.h"
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent>
#include <cmath>

namespace {
int shellCount = 0;
void destroyLater(GtkWindow *window) {
  gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
  g_object_ref(window);
  Gx::later([window] {
    gtk_window_destroy(window);
    g_object_unref(window);
  });
}
GtkWidget *wrapped(const QString &source, const char *cssClass = nullptr) {
  auto *label = Ui::label(source, cssClass);
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_max_width_chars(GTK_LABEL(label), 70);
  return label;
}
GtkWidget *actionRow() {
  auto *row = Gx::hbox(8);
  gtk_widget_set_halign(row, GTK_ALIGN_FILL);
  return row;
}
GtkWidget *spacer() {
  auto *widget = Gx::hbox();
  gtk_widget_set_hexpand(widget, TRUE);
  return widget;
}
QString entryText(GtkWidget *entry) {
  return Gx::qs(gtk_editable_get_text(GTK_EDITABLE(entry)));
}
void focusEntry(GtkWidget *entry) {
  gtk_widget_grab_focus(entry);
  gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
}
} // namespace

namespace {
struct Transient {
  GtkWindow *parent;
  quint64 id;
  Gx::Callback dismiss;
};
std::vector<Transient> &transients() {
  static std::vector<Transient> value;
  return value;
}
quint64 addTransient(GtkWindow *parent, Gx::Callback dismiss) {
  static quint64 next = 0;
  transients().push_back({parent, ++next, std::move(dismiss)});
  return next;
}
void removeTransient(quint64 id) {
  auto &all = transients();
  all.erase(std::remove_if(all.begin(), all.end(),
                           [id](const Transient &t) { return t.id == id; }),
            all.end());
}
// Callbacks from asynchronous choosers must not reach a destroyed owner.
struct Alive {
  std::shared_ptr<bool> flag = std::make_shared<bool>(true);
  ~Alive() { *flag = false; }
};
} // namespace

namespace Dialogs {
int openShells() { return shellCount; }
void dismissAll(GtkWindow *parent) {
  const auto all = transients();
  for (const auto &entry : all)
    if (entry.parent == parent && entry.dismiss)
      entry.dismiss();
}
Shell shell(GtkWindow *parent, const QString &title, const QString &eyebrow,
            const QString &heading, int width, int height, bool modal) {
  Shell s;
  s.window = GTK_WINDOW(gtk_window_new());
  ++shellCount;
  g_object_weak_ref(
      G_OBJECT(s.window), +[](gpointer, GObject *) { --shellCount; }, nullptr);
  auto *windowWidget = GTK_WIDGET(s.window);
  Gx::addClass(windowWidget, "prism-dialog");
  if (parent)
    gtk_window_set_transient_for(s.window, parent);
  gtk_window_set_modal(s.window, modal);
  gtk_window_set_default_size(s.window, width, height);
  // An invisible title bar keeps client-side shadows and resize borders while
  // the dialog draws its own header.
  auto *hidden = Gx::hbox();
  gtk_widget_set_visible(hidden, FALSE);
  gtk_window_set_titlebar(s.window, hidden);
  Ui::title(s.window, title);
  s.root = Gx::vbox();
  Gx::addClass(s.root, "prism-dialog-root");
  gtk_widget_set_overflow(s.root, GTK_OVERFLOW_HIDDEN);
  gtk_window_set_child(s.window, s.root);
  auto *handle = gtk_window_handle_new();
  s.header = Gx::hbox(10);
  Gx::margins(s.header, 22, 16, 14, 2);
  gtk_window_handle_set_child(GTK_WINDOW_HANDLE(handle), s.header);
  auto *identity = Gx::vbox(2);
  gtk_widget_set_hexpand(identity, TRUE);
  if (!eyebrow.isEmpty())
    gtk_box_append(GTK_BOX(identity), Ui::label(eyebrow, "prism-eyebrow"));
  s.heading = Ui::label(heading.isEmpty() ? title : heading,
                        heading.isEmpty() ? "prism-title" : "prism-heading");
  gtk_label_set_ellipsize(GTK_LABEL(s.heading), PANGO_ELLIPSIZE_END);
  gtk_box_append(GTK_BOX(identity), s.heading);
  gtk_box_append(GTK_BOX(s.header), identity);
  auto close = s.close;
  auto *window = s.window;
  *close = [window] { gtk_widget_set_visible(GTK_WIDGET(window), FALSE); };
  auto *closeButton = Ui::button("close", "", "Zamknij • Esc", [close] {
    if (*close)
      (*close)();
  });
  Gx::addClass(closeButton, "windowcontrol");
  Gx::addClass(closeButton, "close");
  gtk_widget_set_valign(closeButton, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(s.header), closeButton);
  gtk_box_append(GTK_BOX(s.root), handle);
  s.body = Gx::vbox(12);
  Gx::addClass(s.body, "prism-dialog-body");
  gtk_widget_set_vexpand(s.body, TRUE);
  gtk_box_append(GTK_BOX(s.root), s.body);
  g_signal_connect_data(
      s.window, "close-request",
      G_CALLBACK(+[](GtkWindow *, gpointer data) -> gboolean {
        auto *run = static_cast<std::shared_ptr<Gx::Callback> *>(data);
        if (**run)
          (**run)();
        return TRUE;
      }),
      new std::shared_ptr<Gx::Callback>(close),
      +[](gpointer data, GClosure *) {
        delete static_cast<std::shared_ptr<Gx::Callback> *>(data);
      },
      GConnectFlags(0));
  auto run = [close] {
    if (*close)
      (*close)();
  };
  Gx::shortcut(windowWidget, GDK_KEY_Escape, Gx::NoModifier, run);
  Gx::shortcut(windowWidget, GDK_KEY_w, Gx::Ctrl, run, true);
  return s;
}

void message(GtkWindow *parent, const QString &title, const QString &text,
             const QString &detail, const QStringList &buttons,
             int defaultButton, int cancelButton,
             std::function<void(int)> done) {
  auto s = shell(parent, title, {}, {}, 480, 10, true);
  auto finished = std::make_shared<bool>(false);
  auto id = std::make_shared<quint64>(0);
  auto *window = s.window;
  auto finish = [window, finished, done, id](int index) {
    if (*finished)
      return;
    *finished = true;
    removeTransient(*id);
    destroyLater(window);
    if (done)
      done(index);
  };
  *id = addTransient(parent, [window, finished, id] {
    if (*finished)
      return;
    *finished = true;
    removeTransient(*id);
    destroyLater(window);
  });
  *s.close = [finish, cancelButton] { finish(cancelButton); };
  gtk_box_append(GTK_BOX(s.body), wrapped(text));
  if (!detail.isEmpty()) {
    // File names and paths: shown verbatim, never passed through the catalog.
    auto *label = gtk_label_new(Gx::u8(detail).constData());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 64);
    Gx::addClass(label, "prism-card");
    gtk_box_append(GTK_BOX(s.body), label);
  }
  auto *row = actionRow();
  gtk_box_append(GTK_BOX(row), spacer());
  for (int i = 0; i < buttons.size(); ++i) {
    auto *button = Ui::button({}, buttons.at(i), {}, [finish, i] { finish(i); });
    gtk_box_append(GTK_BOX(row), button);
    if (i == defaultButton) {
      gtk_window_set_default_widget(window, button);
      gtk_window_set_focus(window, button);
    }
  }
  gtk_box_append(GTK_BOX(s.body), row);
  gtk_window_present(window);
}

void chooseItem(GtkWindow *parent, const QString &title, const QString &label,
                const QStringList &items, std::function<void(int)> done) {
  auto s = shell(parent, title, {}, {}, 520, 10, true);
  auto finished = std::make_shared<bool>(false);
  auto chosen = std::make_shared<int>(0);
  auto id = std::make_shared<quint64>(0);
  auto *window = s.window;
  auto finish = [window, finished, done, id](int index) {
    if (*finished)
      return;
    *finished = true;
    removeTransient(*id);
    destroyLater(window);
    if (done)
      done(index);
  };
  *id = addTransient(parent, [window, finished, id] {
    if (*finished)
      return;
    *finished = true;
    removeTransient(*id);
    destroyLater(window);
  });
  *s.close = [finish] { finish(-1); };
  gtk_box_append(GTK_BOX(s.body), wrapped(label));
  auto *list = Gx::vbox(4);
  GtkWidget *group = nullptr;
  for (int i = 0; i < items.size(); ++i) {
    auto *radio = gtk_check_button_new_with_label(Gx::u8(items.at(i)).constData());
    if (group)
      gtk_check_button_set_group(GTK_CHECK_BUTTON(radio),
                                 GTK_CHECK_BUTTON(group));
    else
      group = radio;
    gtk_check_button_set_active(GTK_CHECK_BUTTON(radio), i == 0);
    Gx::on(radio, "toggled", [radio, chosen, i] {
      if (gtk_check_button_get_active(GTK_CHECK_BUTTON(radio)))
        *chosen = i;
    });
    gtk_box_append(GTK_BOX(list), radio);
  }
  auto *scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll),
                                                   TRUE);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 320);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
  gtk_box_append(GTK_BOX(s.body), scroll);
  auto *row = actionRow();
  gtk_box_append(GTK_BOX(row), spacer());
  gtk_box_append(GTK_BOX(row),
                 Ui::button({}, "&Anuluj", {}, [finish] { finish(-1); }));
  auto *accept =
      Ui::button({}, "&Otwórz", {}, [finish, chosen] { finish(*chosen); });
  Gx::addClass(accept, "suggested");
  gtk_box_append(GTK_BOX(row), accept);
  gtk_window_set_default_widget(window, accept);
  gtk_box_append(GTK_BOX(s.body), row);
  gtk_window_present(window);
}

namespace {
struct FileRequest {
  std::function<void(QString)> done;
  bool save = false;
};
void fileChosen(GObject *source, GAsyncResult *result, gpointer data) {
  std::unique_ptr<FileRequest> request(static_cast<FileRequest *>(data));
  g_autoptr(GError) error = nullptr;
  g_autoptr(GFile) file =
      request->save
          ? gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, &error)
          : gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source),
                                                 result, &error);
  QString path;
  if (file) {
    g_autofree char *name = g_file_get_path(file);
    path = QString::fromLocal8Bit(name ? name : "");
  }
  if (request->done)
    request->done(path);
}
GtkFileDialog *systemDialog(const QString &title, const QString &folder) {
  auto *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, Gx::u8(Ui::tr(title)).constData());
  gtk_file_dialog_set_modal(dialog, TRUE);
  if (!folder.isEmpty() && QFileInfo(folder).isDir()) {
    g_autoptr(GFile) initial =
        g_file_new_for_path(QFile::encodeName(folder).constData());
    gtk_file_dialog_set_initial_folder(dialog, initial);
  }
  return dialog;
}
} // namespace
void chooseFolder(GtkWindow *parent, const QString &title,
                  const QString &initial, std::function<void(QString)> done) {
  g_autoptr(GtkFileDialog) dialog = systemDialog(title, initial);
  gtk_file_dialog_select_folder(dialog, parent, nullptr, fileChosen,
                                new FileRequest{std::move(done), false});
}
void chooseSaveFile(GtkWindow *parent, const QString &title,
                    const QString &folder, const QString &name,
                    std::function<void(QString)> done) {
  g_autoptr(GtkFileDialog) dialog = systemDialog(title, folder);
  gtk_file_dialog_set_initial_name(dialog, Gx::u8(name).constData());
  gtk_file_dialog_save(dialog, parent, nullptr, fileChosen,
                       new FileRequest{std::move(done), true});
}

// ------------------------------------------------------- photo chooser ---
namespace {
struct Chooser : QObject {
  Shell s;
  ThumbnailCache cache;
  std::unique_ptr<LibraryPane> pane;
  GtkWidget *path = nullptr, *preview = nullptr, *caption = nullptr,
            *accept = nullptr;
  GdkTexture *texture = nullptr;
  std::function<void(QString)> done;
  bool finished = false;
  quint64 transient = 0;
  Chooser() : cache(this) {}
  ~Chooser() override { g_clear_object(&texture); }
  void browse(const QString &directory) {
    if (!validLocalPath(directory) || !QFileInfo(directory).isDir())
      return;
    const auto absolute = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    gtk_editable_set_text(GTK_EDITABLE(path), Gx::u8(absolute).constData());
    pane->setDirectory(absolute);
    refresh();
  }
  void refresh() {
    bool isDir = false;
    const auto selected = pane->selectedPath(&isDir);
    gtk_widget_set_sensitive(accept, !selected.isEmpty());
    QString text = "Zaznacz zdjęcie, aby zobaczyć podgląd";
    g_clear_object(&texture);
    if (!selected.isEmpty() && !isDir) {
      const QFileInfo file(selected);
      const auto image = cache.request(file, 512);
      if (!image.isNull()) {
        const QImage rgba = image.format() == QImage::Format_RGBA8888
                                ? image
                                : image.convertToFormat(QImage::Format_RGBA8888);
        auto *keep = new QImage(rgba);
        GBytes *bytes = g_bytes_new_with_free_func(
            keep->constBits(), gsize(keep->sizeInBytes()),
            +[](gpointer data) { delete static_cast<QImage *>(data); }, keep);
        texture = gdk_memory_texture_new(keep->width(), keep->height(),
                                         GDK_MEMORY_R8G8B8A8, bytes,
                                         gsize(keep->bytesPerLine()));
        g_bytes_unref(bytes);
        text.clear();
        Ui::literal(caption, file.fileName());
      } else
        text = cache.busy() ? "Ładowanie podglądu…"
                            : "Brak podglądu tego pliku";
    }
    if (!text.isEmpty())
      Ui::text(caption, text);
    gtk_widget_queue_draw(preview);
  }
  void finish(const QString &result) {
    if (finished)
      return;
    finished = true;
    removeTransient(transient);
    cache.setActive(false);
    pane->stop();
    auto callback = done;
    auto *window = s.window;
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    // Workers are joined and the views detached before the window goes away.
    Gx::later([this, window] {
      pane.reset();
      gtk_window_destroy(window);
      delete this;
    });
    if (callback && !result.isEmpty())
      callback(result);
  }
  void acceptSelection() {
    bool isDir = false;
    const auto selected = pane->selectedPath(&isDir);
    if (selected.isEmpty())
      return;
    if (isDir)
      browse(selected);
    else
      finish(selected);
  }
};
} // namespace
void choosePhoto(GtkWindow *parent, const QString &title,
                 const QString &directory, std::function<void(QString)> done) {
  auto *c = new Chooser;
  c->done = std::move(done);
  c->s = shell(parent, title, "BIBLIOTEKA", title, 1080, 660, true);
  *c->s.close = [c] { c->finish({}); };
  c->transient = addTransient(parent, [c] {
    c->done = {};
    c->finish({});
  });
  c->pane = std::make_unique<LibraryPane>(&c->cache, true);
  c->pane->setMode(LibraryPane::Thumbnails);

  auto *navigation = Gx::hbox(6);
  auto *up = Ui::button("arrowUp", "", "Katalog nadrzędny • Alt+↑", [c] {
    QDir dir(entryText(c->path));
    if (dir.cdUp())
      c->browse(dir.absolutePath());
  });
  gtk_box_append(GTK_BOX(navigation), up);
  c->path = gtk_entry_new();
  gtk_widget_set_hexpand(c->path, TRUE);
  Ui::placeholder(c->path, "Katalog zdjęć…");
  Ui::accessible(c->path, "Ścieżka katalogu");
  Gx::on(c->path, "activate", [c] { c->browse(entryText(c->path)); });
  gtk_box_append(GTK_BOX(navigation), c->path);
  auto *modes = Gx::hbox(0);
  Gx::addClass(modes, "toolgroup");
  auto *listMode = Ui::toggle("list", "", "Lista");
  auto *gridMode = Ui::toggle("grid", "", "Miniatury");
  gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(gridMode),
                              GTK_TOGGLE_BUTTON(listMode));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gridMode), TRUE);
  Gx::on(listMode, "toggled", [c, listMode] {
    c->pane->setMode(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(listMode))
                         ? LibraryPane::List
                         : LibraryPane::Thumbnails);
  });
  gtk_box_append(GTK_BOX(modes), listMode);
  gtk_box_append(GTK_BOX(modes), gridMode);
  gtk_box_append(GTK_BOX(navigation), modes);
  auto *all = Ui::toggle({}, "Wszystkie pliki", {});
  Gx::on(all, "toggled", [c, all] {
    c->pane->setPhotosOnly(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(all)));
  });
  gtk_box_append(GTK_BOX(navigation), all);
  gtk_box_append(GTK_BOX(c->s.body), navigation);

  auto *content = Gx::hbox(14);
  gtk_widget_set_vexpand(content, TRUE);
  auto *frame = Gx::vbox();
  Gx::addClass(frame, "prism-card");
  gtk_widget_set_hexpand(frame, TRUE);
  gtk_box_append(GTK_BOX(frame), c->pane->widget());
  gtk_box_append(GTK_BOX(content), frame);
  auto *side = Gx::vbox(10);
  gtk_widget_set_size_request(side, 250, -1);
  c->preview = prism_canvas_new(250, 220, [c](GtkSnapshot *snapshot, int w, int h) {
    if (!c->texture)
      return;
    const double tw = gdk_texture_get_width(c->texture),
                 th = gdk_texture_get_height(c->texture);
    const double scale = std::min(w / tw, h / th);
    graphene_rect_t bounds =
        Gx::rect(float((w - tw * scale) / 2), float((h - th * scale) / 2),
                           float(tw * scale), float(th * scale));
    gtk_snapshot_append_scaled_texture(snapshot, c->texture,
                                       GSK_SCALING_FILTER_TRILINEAR, &bounds);
  });
  gtk_widget_set_vexpand(c->preview, TRUE);
  Ui::accessible(c->preview, "Podgląd zaznaczonego zdjęcia");
  gtk_box_append(GTK_BOX(side), c->preview);
  c->caption = wrapped("Zaznacz zdjęcie, aby zobaczyć podgląd", "prism-dim");
  gtk_label_set_xalign(GTK_LABEL(c->caption), 0.5f);
  gtk_label_set_justify(GTK_LABEL(c->caption), GTK_JUSTIFY_CENTER);
  gtk_label_set_max_width_chars(GTK_LABEL(c->caption), 28);
  gtk_box_append(GTK_BOX(side), c->caption);
  gtk_box_append(GTK_BOX(content), side);
  gtk_box_append(GTK_BOX(c->s.body), content);

  auto *row = actionRow();
  gtk_box_append(GTK_BOX(row), spacer());
  gtk_box_append(GTK_BOX(row),
                 Ui::button({}, "&Anuluj", {}, [c] { c->finish({}); }));
  c->accept = Ui::button("openPhoto", "&Otwórz", {}, [c] { c->acceptSelection(); });
  Gx::addClass(c->accept, "suggested");
  gtk_widget_set_sensitive(c->accept, FALSE);
  gtk_box_append(GTK_BOX(row), c->accept);
  gtk_box_append(GTK_BOX(c->s.body), row);

  c->pane->selectionChanged = [c] { c->refresh(); };
  c->pane->activated = [c](const QString &path, bool isDir) {
    if (isDir)
      c->browse(path);
    else
      c->finish(path);
  };
  QObject::connect(&c->cache, &ThumbnailCache::changed, c, [c] {
    if (!c->finished)
      c->refresh();
  });
  Gx::shortcut(GTK_WIDGET(c->s.window), GDK_KEY_Up, Gx::Alt, [c] {
    QDir dir(entryText(c->path));
    if (dir.cdUp())
      c->browse(dir.absolutePath());
  }, true);
  Gx::shortcut(GTK_WIDGET(c->s.window), GDK_KEY_l, Gx::Ctrl,
               [c] { focusEntry(c->path); }, true);
  c->cache.setActive(true);
  c->browse(QFileInfo(directory).isDir() ? directory : QDir::homePath());
  gtk_window_present(c->s.window);
  gtk_widget_grab_focus(c->pane->focusWidget());
}
} // namespace Dialogs

// --------------------------------------------------------------- about ---
struct AboutDialog::Impl {
  Dialogs::Shell s;
  GtkWidget *stack = nullptr, *mark = nullptr;
  std::unique_ptr<Table> keys;
  Animation reveal;
  double progress = 1.0;
  void start() {
    reveal.start(mark, 640, [this](double t) {
      progress = 1 - std::pow(1 - t, 3);
      gtk_widget_queue_draw(mark);
    });
  }
  void retitle() {
    gtk_stack_page_set_title(
        gtk_stack_get_page(GTK_STACK(stack),
                           gtk_stack_get_child_by_name(GTK_STACK(stack), "about")),
        Gx::u8(Ui::mnemonic(Ui::tr("O &programie"))).constData());
    gtk_stack_page_set_title(
        gtk_stack_get_page(GTK_STACK(stack),
                           gtk_stack_get_child_by_name(GTK_STACK(stack), "keys")),
        Gx::u8(Ui::mnemonic(Ui::tr("&Klawiatura"))).constData());
  }
};
AboutDialog::AboutDialog(GtkWindow *parent) : d(new Impl) {
  auto *impl = d.get();
  d->s = Dialogs::shell(parent, "O programie · ky-prism", {}, {}, 680, 640,
                        false);
  d->stack = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(d->stack),
                                GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_stack_set_transition_duration(GTK_STACK(d->stack), 140);
  auto *switcher = gtk_stack_switcher_new();
  gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(d->stack));
  gtk_widget_set_halign(switcher, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(d->s.body), switcher);
  gtk_widget_set_vexpand(d->stack, TRUE);
  gtk_box_append(GTK_BOX(d->s.body), d->stack);

  auto *identity = Gx::vbox(8);
  d->mark = prism_cairo_canvas_new(
      170, 170, [impl](cairo_t *cr, int w, int h, const GdkRGBA &ink) {
        Icons::productMark(cr, w, h, impl->progress, ink,
                           Theme::rgba(Theme::palette().accent));
      });
  gtk_widget_set_halign(d->mark, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top(d->mark, 8);
  gtk_box_append(GTK_BOX(identity), d->mark);
  auto centered = [&](const QString &text, const char *cssClass,
                      bool selectable = false) {
    auto *label = wrapped(text, cssClass);
    gtk_label_set_xalign(GTK_LABEL(label), 0.5f);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_label_set_selectable(GTK_LABEL(label), selectable);
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(identity), label);
    return label;
  };
  centered("ky-prism", "prism-product");
  centered(QString("Wersja ") + PRISM_VERSION + " · przeglądarka zdjęć GPU",
           nullptr);
  centered("Krzysztof Kylen Lenartowicz", "prism-author", true);
  centered("© 2026 Krzysztof Kylen Lenartowicz", nullptr);
  // clang-format off  (publication/prepare.py replaces the notice line)
  centered("GTK 4 · Qt Core · OpenGL · libvips · libjpeg-turbo · LibRaw · Little "
           "CMS\nBiblioteki zewnętrzne zachowują własne prawa i licencje.",
           "prism-dim");
  // clang-format on
  gtk_stack_add_named(GTK_STACK(d->stack), identity, "about");

  Table::Options options;
  options.headers = {"Klawisze", "Działanie"};
  options.widths = {250, 0};
  options.expandColumn = 1;
  options.sortable = false;
  options.translateCells = true;
  options.wrap = true;
  d->keys = std::make_unique<Table>(options);
  Ui::accessible(d->keys->view(), "Skróty klawiaturowe ky-prism");
  const QList<QPair<QString, QString>> bindings = {
      {"Ctrl+T / Ctrl+Shift+W", "Nowa karta / zamknij bieżącą kartę"},
      {"Ctrl+Tab / Ctrl+Shift+Tab", "Następna / poprzednia karta"},
      {"Ctrl+U", "Otwórz pendrive"},
      {"Ctrl+Shift+P", "Włącz / wyłącz porównanie dwóch zdjęć"},
      {"Ctrl+O", "Otwórz zdjęcie / plik"},
      {"Ctrl+B", "Pokaż / ukryj lewą listę plików"},
      {"Ctrl+L lub Alt+D", "Wpisz ścieżkę katalogu"},
      {"F6 / Shift+F6", "Przełącz fokus między zdjęciem, listą i ścieżką"},
      {"Alt+← / Alt+→", "Poprzedni / następny katalog"},
      {"Alt+↑", "Katalog nadrzędny"},
      {"Ctrl+D", "Dodaj / usuń zakładkę bieżącego katalogu"},
      {"Ctrl+Shift+B", "Przeglądarka zakładek katalogów"},
      {"Ctrl+Shift+H", "Dodaj / usuń zdjęcie z ulubionych"},
      {"Ctrl+Shift+F", "Przeglądarka ulubionych zdjęć"},
      {"Ctrl+C", "Kopiuj wyświetlone zdjęcie do schowka (obraz i plik)"},
      {"Ctrl+E", "Eksportuj kopię pliku — wybierz miejsce i nazwę…"},
      {"Ctrl+Shift+C", "Kopiuj zaznaczone pliki / bieżące zdjęcie…"},
      {"Ctrl+Shift+D", "Szukaj duplikatów plików…"},
      {"Prawy przycisk na zdjęciu / Menu / Shift+F10",
       "Menu: kopiuj, eksportuj, ulubione, EXIF"},
      {"Ctrl+I", "Pokaż / ukryj EXIF"},
      {"Ctrl+Alt+O", "Krycie interfejsu — suwak przezroczystości oprawy"},
      {"Ctrl+M", "Maksymalizuj / przywróć okno"},
      {"F11 / Esc", "Pełny ekran / wyjdź z pełnego ekranu"},
      {"Ctrl++ / Ctrl+−", "Przybliż / oddal"},
      {"Ctrl+0 / Ctrl+1", "Dopasuj widok / oryginalne 1:1"},
      {"← / → lub PageUp / PageDown",
       "Poprzednie / następne zdjęcie (fokus na zdjęciu)"},
      {"F / 1 / + / −", "Dopasuj / 1:1 / zoom (fokus na zdjęciu)"},
      {"Shift+strzałki",
       "Przesuwaj zdjęcie; z Ctrl: większy krok (fokus na zdjęciu)"},
      {"Tab / Shift+Tab", "Następny / poprzedni element interfejsu"},
      {"Spacja / Enter", "Uruchom przycisk; Enter otwiera pozycję listy"},
      {"Ctrl+F (w przeglądarce)", "Filtruj zakładki lub ulubione"},
      {"Alt+1 / Alt+2 (zakładki)",
       "Sortuj po nazwie / ścieżce; powtórzenie odwraca kolejność"},
      {"Alt+1…4 (ulubione)", "Sortuj po nazwie / ścieżce / MD5 / rozmiarze"},
      {"Delete (lista zakładek)", "Usuń zakładkę, pozostaw katalog"},
      {"Alt+litera przycisku", "Akcja opisana podkreśloną literą w dialogu"},
      {"Ctrl+W / Esc (dialog)",
       "Zamknij dialog; trwającą operację pliku najpierw anuluj"},
      {"Ctrl+Esc", "Anuluj operację ulubionych"},
      {"F1 / Ctrl+F1", "Lista skrótów / informacje o autorze"},
      {"Ctrl+Alt+L", "Przełącz język PL / EN"},
      {"Ctrl+W (główne okno)", "Zamknij ky-prism"}};
  QList<Table::Row> rows;
  for (const auto &binding : bindings)
    rows.append(Table::Row{{binding.first, binding.second}, {}, false, {}});
  d->keys->setRows(rows);
  gtk_stack_add_named(GTK_STACK(d->stack), d->keys->widget(), "keys");
  for (const char *name : {"about", "keys"})
    gtk_stack_page_set_use_underline(
        gtk_stack_get_page(GTK_STACK(d->stack),
                           gtk_stack_get_child_by_name(GTK_STACK(d->stack), name)),
        TRUE);
  d->retitle();
  Ui::dynamic(d->stack, [impl] { impl->retitle(); });

  auto *row = actionRow();
  gtk_box_append(GTK_BOX(row), spacer());
  auto close = d->s.close;
  gtk_box_append(GTK_BOX(row), Ui::button({}, "&Zamknij", {}, [close] {
                   (*close)();
                 }));
  gtk_box_append(GTK_BOX(d->s.body), row);
  *d->s.close = [impl] {
    impl->reveal.stop();
    gtk_widget_set_visible(GTK_WIDGET(impl->s.window), FALSE);
  };
  Gx::on(d->stack, "notify::visible-child-name", [impl] {
    const QString page =
        Gx::qs(gtk_stack_get_visible_child_name(GTK_STACK(impl->stack)));
    if (page == "about" && gtk_widget_get_mapped(impl->mark))
      impl->start();
    else
      impl->reveal.stop();
  });
  auto *window = GTK_WIDGET(d->s.window);
  Gx::shortcut(window, GDK_KEY_1, Gx::Alt, [impl] {
    gtk_stack_set_visible_child_name(GTK_STACK(impl->stack), "about");
  }, true);
  Gx::shortcut(window, GDK_KEY_2, Gx::Alt, [impl] {
    gtk_stack_set_visible_child_name(GTK_STACK(impl->stack), "keys");
  }, true);
}
AboutDialog::~AboutDialog() {
  d->reveal.stop();
  d->keys.reset();
  gtk_window_destroy(d->s.window);
}
void AboutDialog::showPage(int page) {
  gtk_stack_set_visible_child_name(GTK_STACK(d->stack),
                                   page == 0 ? "about" : "keys");
  gtk_window_present(d->s.window);
  if (page == 0)
    d->start();
  else
    gtk_widget_grab_focus(d->keys->view());
}
void AboutDialog::hide() { (*d->s.close)(); }
bool AboutDialog::animating() const { return d->reveal.running(); }
bool AboutDialog::visible() const {
  return gtk_widget_get_visible(GTK_WIDGET(d->s.window));
}
int AboutDialog::currentPage() const {
  return Gx::qs(gtk_stack_get_visible_child_name(GTK_STACK(d->stack))) ==
                 "about"
             ? 0
             : 1;
}

// ----------------------------------------------------------- bookmarks ---
struct BookmarksDialog::Impl {
  BookmarksDialog *q = nullptr;
  Dialogs::Shell s;
  std::unique_ptr<Table> table;
  GtkWidget *filter = nullptr, *message = nullptr;
  std::function<QString()> directory;
  QStringList paths;
  QString selected() const {
    const int index = table->selected();
    return index >= 0 && index < paths.size() ? paths.at(index) : QString();
  }
  void openSelected() {
    const auto path = selected();
    if (path.isEmpty())
      return;
    if (!QFileInfo(path).isDir()) {
      Ui::text(message, "Katalog jest niedostępny: " + path);
      return;
    }
    if (q->openRequested)
      q->openRequested(path);
    gtk_widget_set_visible(GTK_WIDGET(s.window), FALSE);
  }
  void change(bool add) {
    try {
      const auto path = add ? directory() : selected();
      if (path.isEmpty())
        return;
      FolderBookmarks::change(path, add);
      q->refresh();
      if (q->changed)
        q->changed();
    } catch (const std::exception &e) {
      Ui::text(message, QString::fromUtf8(e.what()));
    }
  }
};
BookmarksDialog::BookmarksDialog(std::function<QString()> currentDirectory,
                                 GtkWindow *parent)
    : d(new Impl) {
  auto *impl = d.get();
  d->q = this;
  d->directory = std::move(currentDirectory);
  d->s = Dialogs::shell(parent, "Zakładki katalogów · ky-prism", "BIBLIOTEKA",
                        "Zakładki katalogów", 800, 500, false);
  d->filter = gtk_entry_new();
  Ui::placeholder(d->filter, "Filtruj nazwę lub ścieżkę · Ctrl+F");
  Ui::accessible(d->filter, "Filtr zakładek");
  gtk_box_append(GTK_BOX(d->s.body), d->filter);
  Table::Options options;
  options.headers = {"Katalog", "Pełna ścieżka"};
  options.widths = {200, 0};
  options.expandColumn = 1;
  d->table = std::make_unique<Table>(options);
  Ui::accessible(d->table->view(), "Zapisane katalogi");
  auto *card = Gx::vbox();
  Gx::addClass(card, "prism-card");
  gtk_widget_set_vexpand(card, TRUE);
  gtk_box_append(GTK_BOX(card), d->table->widget());
  gtk_box_append(GTK_BOX(d->s.body), card);
  auto *row = actionRow();
  auto *open = Ui::button("folder", "&Otwórz", {}, [impl] { impl->openSelected(); });
  Gx::addClass(open, "suggested");
  gtk_box_append(GTK_BOX(row), open);
  gtk_box_append(GTK_BOX(row), Ui::button("star", "&Dodaj bieżący katalog", {},
                                          [impl] { impl->change(true); }));
  gtk_box_append(GTK_BOX(row), Ui::button("trash", "&Usuń zakładkę", {},
                                          [impl] { impl->change(false); }));
  gtk_box_append(GTK_BOX(row), spacer());
  gtk_box_append(GTK_BOX(row),
                 Ui::button({}, "Odś&wież", {}, [this] { refresh(); }));
  auto close = d->s.close;
  gtk_box_append(GTK_BOX(row),
                 Ui::button({}, "&Zamknij", {}, [close] { (*close)(); }));
  gtk_box_append(GTK_BOX(d->s.body), row);
  d->message = wrapped(
      "Enter: otwórz katalog · Delete: usuń zakładkę, pozostawiając katalog.",
      "prism-dim");
  gtk_box_append(GTK_BOX(d->s.body), d->message);
  Gx::on(d->filter, "changed",
         [impl] { impl->table->setFilter(entryText(impl->filter)); });
  d->table->activated = [impl](int) { impl->openSelected(); };
  auto *window = GTK_WIDGET(d->s.window);
  Gx::shortcut(window, GDK_KEY_f, Gx::Ctrl, [impl] { focusEntry(impl->filter); },
               true);
  Gx::shortcut(window, GDK_KEY_F5, Gx::NoModifier, [this] { refresh(); }, true);
  Gx::shortcut(window, GDK_KEY_r, Gx::Ctrl, [this] { refresh(); }, true);
  for (int column = 0; column < 2; ++column)
    Gx::shortcut(window, GDK_KEY_1 + guint(column), Gx::Alt,
                 [impl, column] { impl->table->toggleSort(column); }, true);
  Gx::shortcut(d->table->view(), GDK_KEY_Delete, Gx::NoModifier,
               [impl] { impl->change(false); });
  Gx::on(d->s.window, "show", [this, impl] {
    refresh();
    focusEntry(impl->filter);
  });
}
BookmarksDialog::~BookmarksDialog() {
  d->table.reset();
  gtk_window_destroy(d->s.window);
}
void BookmarksDialog::present() { gtk_window_present(d->s.window); }
void BookmarksDialog::hide() {
  gtk_widget_set_visible(GTK_WIDGET(d->s.window), FALSE);
}
int BookmarksDialog::rowCount() const { return d->table->rowCount(); }
void BookmarksDialog::refresh() {
  try {
    d->paths = FolderBookmarks::read();
    QList<Table::Row> rows;
    for (const auto &path : d->paths) {
      const QString name = QFileInfo(path).fileName();
      rows.append(Table::Row{{name.isEmpty() ? path : name, path}, {}, false, path});
    }
    d->table->setRows(rows);
    d->table->selectFirst();
    Ui::text(d->message, QString("%1 zapisanych katalogów · Enter: otwórz · "
                                 "Delete: usuń tylko zakładkę.")
                             .arg(d->paths.size()));
  } catch (const std::exception &e) {
    Ui::text(d->message, QString::fromUtf8(e.what()));
  }
}

// ----------------------------------------------------------- favorites ---
struct FavoritesDialog::Impl : QObject {
  FavoritesDialog *q = nullptr;
  Dialogs::Shell s;
  Favorites *favorites = nullptr;
  std::unique_ptr<Table> table;
  GtkWidget *search = nullptr, *message = nullptr, *open = nullptr,
            *find = nullptr, *remove = nullptr, *refresh = nullptr,
            *cancel = nullptr;
  QList<Favorite> rows;
  Alive alive;
  Favorite selected() const {
    const int index = table->selected();
    return index >= 0 && index < rows.size() ? rows.at(index) : Favorite{};
  }
  void openSelected() {
    const auto e = selected();
    if (e.id.isEmpty())
      return;
    if (!QFileInfo(e.path).isFile()) {
      Ui::text(message,
               "Pliku nie ma w zapisanym miejscu. Użyj „Odnajdź zdjęcie”.");
      return;
    }
    if (q->openRequested)
      q->openRequested(e.path);
    gtk_widget_set_visible(GTK_WIDGET(s.window), FALSE);
  }
  void update() {
    rows = favorites->all();
    QList<Table::Row> data;
    for (const auto &e : rows)
      data.append(Table::Row{
          {e.name, e.path, e.md5,
           QString::number(e.size / 1048576.0, 'f', 1) + " MiB"},
          {0, 0, 0, double(e.size)},
          false,
          e.path + "\nMD5: " + e.md5});
    table->setRows(data);
    table->selectFirst();
    Ui::title(s.window, QString("Ulubione zdjęcia · %1").arg(rows.size()));
    Ui::text(s.heading, QString("Ulubione zdjęcia · %1").arg(rows.size()));
  }
  void working(bool busy) {
    for (auto *button : {find, remove, refresh, open})
      gtk_widget_set_sensitive(button, !busy);
    gtk_widget_set_visible(cancel, busy);
  }
};
FavoritesDialog::FavoritesDialog(Favorites *controller, GtkWindow *parent)
    : d(new Impl) {
  auto *impl = d.get();
  d->q = this;
  d->favorites = controller;
  d->s = Dialogs::shell(parent, "Ulubione zdjęcia", "BIBLIOTEKA",
                        "Ulubione zdjęcia", 1060, 590, false);
  d->search = gtk_entry_new();
  Ui::placeholder(d->search, "Filtruj nazwę, ścieżkę lub MD5…");
  Ui::accessible(d->search, "Filtr ulubionych");
  gtk_box_append(GTK_BOX(d->s.body), d->search);
  Table::Options options;
  options.headers = {"Zdjęcie", "Ścieżka", "MD5", "Rozmiar"};
  options.widths = {210, 0, 250, 96};
  options.expandColumn = 1;
  options.numericColumns = {3};
  d->table = std::make_unique<Table>(options);
  Ui::accessible(d->table->view(), "Ulubione zdjęcia");
  auto *card = Gx::vbox();
  Gx::addClass(card, "prism-card");
  gtk_widget_set_vexpand(card, TRUE);
  gtk_box_append(GTK_BOX(card), d->table->widget());
  gtk_box_append(GTK_BOX(d->s.body), card);
  auto *row = actionRow();
  d->open = Ui::button("openPhoto", "&Otwórz", {}, [impl] { impl->openSelected(); });
  Gx::addClass(d->open, "suggested");
  gtk_box_append(GTK_BOX(row), d->open);
  d->find = Ui::button("search", "O&dnajdź zdjęcie…", {}, [impl] {
    const auto e = impl->selected();
    if (e.id.isEmpty())
      return;
    Dialogs::chooseFolder(impl->s.window, "Gdzie szukać zdjęcia?",
                          QDir::homePath(),
                          [impl, id = e.id, alive = impl->alive.flag](
                              QString directory) {
                            if (*alive && !directory.isEmpty())
                              impl->favorites->find(id, directory);
                          });
  });
  gtk_box_append(GTK_BOX(row), d->find);
  d->remove = Ui::button("trash", "&Usuń z ulubionych", {}, [impl] {
    const auto e = impl->selected();
    if (!e.id.isEmpty())
      impl->favorites->remove(e.id);
  });
  gtk_box_append(GTK_BOX(row), d->remove);
  gtk_box_append(GTK_BOX(row), spacer());
  d->cancel = Ui::button({}, "&Anuluj operację", {},
                         [impl] { impl->favorites->cancel(); });
  gtk_box_append(GTK_BOX(row), d->cancel);
  d->refresh = Ui::button({}, "Odś&wież", {}, [impl] { impl->favorites->reload(); });
  gtk_box_append(GTK_BOX(row), d->refresh);
  auto close = d->s.close;
  gtk_box_append(GTK_BOX(row),
                 Ui::button({}, "&Zamknij", {}, [close] { (*close)(); }));
  gtk_box_append(GTK_BOX(d->s.body), row);
  d->message = wrapped(
      "Lista przechowuje odwołania do zdjęć — nie kopiuje oryginałów.",
      "prism-dim");
  gtk_box_append(GTK_BOX(d->s.body), d->message);

  auto *window = GTK_WIDGET(d->s.window);
  for (int column = 0; column < 4; ++column)
    Gx::shortcut(window, GDK_KEY_1 + guint(column), Gx::Alt,
                 [impl, column] { impl->table->toggleSort(column); }, true);
  Gx::shortcut(window, GDK_KEY_f, Gx::Ctrl, [impl] { focusEntry(impl->search); },
               true);
  Gx::shortcut(window, GDK_KEY_F5, Gx::NoModifier,
               [impl] { impl->favorites->reload(); }, true);
  Gx::shortcut(window, GDK_KEY_r, Gx::Ctrl,
               [impl] { impl->favorites->reload(); }, true);
  Gx::on(d->search, "changed",
         [impl] { impl->table->setFilter(entryText(impl->search)); });
  d->table->activated = [impl](int) { impl->openSelected(); };
  QObject::connect(controller, &Favorites::changed, impl,
                   [impl] { impl->update(); });
  d->update();
  QObject::connect(controller, &Favorites::working, impl, [impl](bool busy) {
    impl->working(busy);
    Ui::text(impl->message,
             busy ? "Operacja w tle — możesz ją anulować."
                  : "Gotowe. Ścieżkę aktualizuje wyłącznie plik o zgodnym "
                    "rozmiarze i MD5.");
  });
  d->working(controller->busy());
  QObject::connect(controller, &Favorites::error, impl,
                   [impl](const QString &text) { Ui::text(impl->message, text); });
  QObject::connect(controller, &Favorites::progress, impl,
                   [impl](qint64 n, qint64 total) {
                     Ui::text(impl->message,
                              total < 0
                                  ? QString("Przejrzano %1 wpisów…").arg(n)
                                  : QString("MD5: %1 / %2 MiB")
                                        .arg(n / 1048576.0, 0, 'f', 1)
                                        .arg(total / 1048576.0, 0, 'f', 1));
                   });
  QObject::connect(
      controller, &Favorites::found, impl,
      [impl](const QString &id, const QStringList &matches, bool limited) {
        if (matches.isEmpty()) {
          Ui::text(impl->message,
                   limited
                       ? "Osiągnięto limit 500 000 wpisów. Wskaż węższy katalog."
                       : "Nie znaleziono zgodnego pliku w dostępnych "
                         "katalogach. Możesz wskazać inne miejsce.");
          return;
        }
        if (matches.size() == 1) {
          impl->favorites->relocate(id, matches.first());
          return;
        }
        Dialogs::chooseItem(
            impl->s.window, "Zgodne kopie zdjęcia",
            limited ? "Znaleziono do 20 kopii. Wybierz ścieżkę:"
                    : "Wybierz ścieżkę:",
            matches, [impl, id, matches, alive = impl->alive.flag](int index) {
              if (*alive && index >= 0 && index < matches.size())
                impl->favorites->relocate(id, matches.at(index));
            });
      });
}
FavoritesDialog::~FavoritesDialog() {
  Dialogs::dismissAll(d->s.window);
  QObject::disconnect(d->favorites, nullptr, d.get(), nullptr);
  d->table.reset();
  gtk_window_destroy(d->s.window);
}
void FavoritesDialog::present() { gtk_window_present(d->s.window); }
void FavoritesDialog::hide() {
  gtk_widget_set_visible(GTK_WIDGET(d->s.window), FALSE);
}
int FavoritesDialog::rowCount() const { return d->table->rowCount(); }

// ------------------------------------------------- copy / duplicates -----
struct FileOpsDialog::Impl : QObject {
  FileOpsDialog *q = nullptr;
  Dialogs::Shell s;
  QThreadPool pool;
  QFutureWatcher<FileResult> watcher;
  std::shared_ptr<std::atomic_bool> token =
      std::make_shared<std::atomic_bool>(false);
  std::unique_ptr<Table> table;
  GtkWidget *message = nullptr, *destinationLabel = nullptr,
            *progress = nullptr, *action = nullptr, *cancelButton = nullptr,
            *choose = nullptr, *copyList = nullptr, *startButton = nullptr;
  guint pulse = 0;
  QString source, destination, root, copyListSource;
  QStringList sources;
  QList<DuplicateGroup> groups;
  // Table row → (group, path) for path rows; group rows map to (-1, "").
  QList<QPair<int, QString>> rowTargets;
  bool searching = false, active = false, shuttingDown = false;
  Alive alive;

  void setBusy(bool indeterminate) {
    if (indeterminate && !pulse)
      pulse = g_timeout_add(
          90,
          +[](gpointer data) -> gboolean {
            gtk_progress_bar_pulse(GTK_PROGRESS_BAR(data));
            return G_SOURCE_CONTINUE;
          },
          progress);
    else if (!indeterminate && pulse) {
      g_source_remove(pulse);
      pulse = 0;
    }
  }
  void renderCopyList() {
    // First and last rows are interface text; the paths between stay literal.
    auto rows = copyListSource.split('\n');
    if (!rows.isEmpty() &&
        rows.first().endsWith(
            " plików • Ctrl/Shift w bibliotece: zaznacz wiele")) {
      rows.first() = PrismLanguage::text(rows.first() + "\n").chopped(1);
      if (rows.last().startsWith("… i ") &&
          rows.last().endsWith(" dalszych plików"))
        rows.last() = PrismLanguage::text("\n" + rows.last()).mid(1);
    }
    const auto bytes = rows.join('\n').toUtf8();
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(copyList)),
                             bytes.constData(), bytes.size());
  }
  void showGroups() {
    QList<Table::Row> rows;
    rowTargets.clear();
    for (int g = 0; g < groups.size(); ++g) {
      const auto &group = groups.at(g);
      rows.append(Table::Row{
          {group.name, QString::number(group.size) + " B · " + group.md5},
          {},
          true,
          {}});
      rowTargets.append(QPair<int, QString>(-1, QString()));
      for (const auto &path : group.paths) {
        rows.append(Table::Row{{path, {}}, {}, false, path});
        rowTargets.append(QPair<int, QString>(g, path));
      }
    }
    table->setRows(rows);
    if (rowTargets.size() > 1)
      table->select(1);
  }
  QPair<int, QString> selectedTarget() const {
    const int index = table ? table->selected() : -1;
    return index >= 0 && index < rowTargets.size() ? rowTargets.at(index)
                                                   : QPair<int, QString>{-1, {}};
  }
  void updateSelection() {
    const auto target = selectedTarget();
    const bool selected = target.first >= 0;
    gtk_widget_set_sensitive(
        action, !active && (searching ? selected : !destination.isEmpty()));
    if (searching && selected && !active)
      Ui::text(message,
               "Wybrany plik: " + target.second +
                   "\nAlt+K: przenieś do kosza po osobnym potwierdzeniu.");
  }
  void start(std::function<FileResult(const std::shared_ptr<std::atomic_bool> &,
                                      const Progress &)>
                 job) {
    if (active || shuttingDown)
      return;
    active = true;
    token = std::make_shared<std::atomic_bool>(false);
    gtk_widget_set_sensitive(choose, FALSE);
    if (startButton)
      gtk_widget_set_sensitive(startButton, FALSE);
    gtk_widget_set_sensitive(action, FALSE);
    gtk_widget_set_sensitive(cancelButton, TRUE);
    Ui::text(Ui::buttonLabel(cancelButton), "&Anuluj");
    gtk_widget_set_visible(progress, TRUE);
    setBusy(true);
    auto t = token;
    auto report = [this, t](qint64 n, qint64 total) {
      QMetaObject::invokeMethod(
          this,
          [this, t, n, total] {
            if (!active || t != token)
              return;
            if (total > 0) {
              setBusy(false);
              gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress),
                                            double(n) / double(total));
            } else
              setBusy(true);
            Ui::text(message,
                     total == -1   ? QString("Skanowanie: %1 wpisów…").arg(n)
                     : total == -2 ? QString("Porównano: %1 plików…").arg(n)
                     : total == -3
                         ? QString("Weryfikacja przed przeniesieniem: %1 MiB…")
                               .arg(n / 1048576.0, 0, 'f', 1)
                         : QString("Przetworzono: %1 / %2 MiB")
                               .arg(n / 1048576.0, 0, 'f', 1)
                               .arg(total / 1048576.0, 0, 'f', 1));
          },
          Qt::QueuedConnection);
    };
    watcher.setFuture(QtConcurrent::run(&pool, [job, t, report] {
      try {
        return job(t, report);
      } catch (const std::exception &e) {
        return FileResult{{}, QString::fromUtf8(e.what()), {}, t->load()};
      }
    }));
  }
  void finished() {
    const auto result = watcher.result();
    active = false;
    setBusy(false);
    gtk_widget_set_sensitive(choose, TRUE);
    if (startButton)
      gtk_widget_set_sensitive(startButton, !root.isEmpty());
    gtk_widget_set_sensitive(cancelButton, TRUE);
    Ui::text(Ui::buttonLabel(cancelButton), "Z&amknij");
    gtk_progress_bar_set_fraction(
        GTK_PROGRESS_BAR(progress),
        result.error.isEmpty() && !result.cancelled ? 1.0 : 0.0);
    if (result.cancelled)
      Ui::text(message, result.error.isEmpty()
                            ? QString("Anulowano. Wcześniej ukończone kopie "
                                      "pozostają w katalogu.")
                            : result.error);
    else if (!result.error.isEmpty())
      Ui::text(message, result.error);
    else {
      Ui::text(message, result.message);
      if (searching) {
        groups = result.groups;
        showGroups();
      }
    }
    updateSelection();
  }
  void requestClose() {
    if (active) {
      token->store(true);
      Ui::text(message, "Anulowanie…");
      return;
    }
    gtk_widget_set_visible(GTK_WIDGET(s.window), FALSE);
    if (q->closed)
      Gx::later([this, guard = alive.flag] {
        if (*guard && q->closed)
          q->closed();
      });
  }
  void setDestination(const QString &path) {
    if (sources.size() > 1) {
      destination = path;
      Ui::literal(destinationLabel, path);
      Ui::text(message,
               QString("%1 plików → %2\nIstniejące pliki nie będą nadpisane.")
                   .arg(sources.size())
                   .arg(path));
      gtk_widget_set_sensitive(action, TRUE);
      return;
    }
    if (QFileInfo::exists(path) || QFileInfo(path).isSymLink()) {
      Ui::text(message, "Cel już istnieje. Wybierz inną nazwę; istniejących "
                        "plików nie nadpisujemy.");
      return;
    }
    destination = path;
    Ui::literal(destinationLabel, path);
    Ui::text(message, "Kopia: " + destination);
    gtk_widget_set_sensitive(action, TRUE);
  }
  void chooseDestination() {
    if (sources.size() > 1) {
      Dialogs::chooseFolder(s.window, "Gdzie zapisać kopie zdjęć?", root,
                            [this, alive = alive.flag](QString directory) {
                              if (*alive && !directory.isEmpty() &&
                                  !shuttingDown)
                                setDestination(directory);
                            });
      return;
    }
    Dialogs::chooseSaveFile(s.window, "Gdzie zapisać kopię zdjęcia?",
                            QFileInfo(source).absolutePath(),
                            "kopia-" + QFileInfo(source).fileName(),
                            [this, alive = alive.flag](QString name) {
                              if (*alive && !name.isEmpty() && !shuttingDown)
                                setDestination(name);
                            });
  }
  // Choosing a folder only selects it; the search starts with its own button.
  void selectRoot(const QString &directory) {
    if (active)
      return;
    root = directory;
    Ui::literal(destinationLabel, directory);
    groups.clear();
    showGroups();
    gtk_widget_set_visible(progress, FALSE);
    Ui::text(message, "Katalog wybrany. Kliknij „Rozpocznij wyszukiwanie”.");
    gtk_widget_set_sensitive(startButton, TRUE);
    gtk_widget_grab_focus(startButton);
    updateSelection();
  }
  void searchIn(const QString &directory) {
    root = directory;
    Ui::literal(destinationLabel, directory);
    groups.clear();
    showGroups();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), 0.0);
    start([directory](auto t, auto p) {
      auto found = FileOps::duplicates(directory, t, p);
      return FileResult{
          found,
          {},
          found.isEmpty()
              ? QString("Nie znaleziono duplikatów. Duplikat to plik o tej "
                        "samej nazwie, wielkości i zawartości (MD5) w innym "
                        "miejscu wybranego katalogu lub jego podkatalogów.")
              : QString("Znaleziono %1 grup duplikatów. Wybierz "
                        "konkretną ścieżkę do przeniesienia do kosza.")
                    .arg(found.size()),
          false};
    });
  }
  void search() {
    Dialogs::chooseFolder(s.window, "Gdzie szukać duplikatów?", root,
                          [this, alive = alive.flag](QString directory) {
                            if (*alive && !directory.isEmpty() && !shuttingDown)
                              selectRoot(directory);
                          });
  }
  void copy() {
    auto list = sources;
    auto dst = destination;
    start([list, dst](auto t, auto p) {
      if (list.size() > 1)
        FileOps::copyBatch(list, dst, t, p);
      else
        FileOps::copy(list.value(0), dst, t, p);
      return FileResult{
          {},
          {},
          QString("Zapisano %1 plików: %2").arg(list.size()).arg(dst),
          false};
    });
  }
  void removeSelected() {
    const auto target = selectedTarget();
    if (active || target.first < 0 || target.first >= groups.size())
      return;
    const auto group = groups.at(target.first);
    const QString path = target.second;
    Dialogs::message(
        s.window, "Potwierdź przeniesienie do kosza",
        "Przenieść ten plik do kosza?",
        path + PrismLanguage::text("\n\nPrzed przeniesieniem ponownie sprawdzę "
                                   "zawartość i drugą kopię."),
        {"&Anuluj", "Przenieś do kosza"}, 0, 0,
        [this, group, path, alive = alive.flag](int choice) {
          if (!*alive || choice != 1 || shuttingDown)
            return;
          auto existing = groups;
          start([group, path, existing](auto t, auto p) mutable {
            FileOps::trash(group, path, t, p);
            for (auto &g : existing)
              g.paths.removeAll(path);
            existing.removeIf([](const auto &g) { return g.paths.size() < 2; });
            return FileResult{existing, {}, "Przeniesiono do kosza: " + path,
                              false};
          });
        });
  }
};
FileOpsDialog::FileOpsDialog(const QString &photo, bool duplicates,
                             const QString &directory, GtkWindow *parent)
    : d(new Impl) {
  auto *impl = d.get();
  d->q = this;
  d->source = photo;
  d->root = directory;
  d->searching = duplicates;
  if (!photo.isEmpty())
    d->sources.append(photo);
  d->pool.setMaxThreadCount(1);
  d->pool.setThreadPriority(QThread::LowPriority);
  d->s = Dialogs::shell(
      parent, duplicates ? "Duplikaty · ky-prism" : "Kopia zdjęcia · ky-prism",
      duplicates ? "BIBLIOTEKA  /  PORZĄDKOWANIE" : "BIBLIOTEKA  /  KOPIOWANIE",
      duplicates ? "Znajdź duplikaty" : "Utwórz kopię zdjęć",
      duplicates ? 900 : 740, duplicates ? 600 : 480, true);
  *d->s.close = [impl] { impl->requestClose(); };
  if (!duplicates) {
    d->copyList = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(d->copyList), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(d->copyList), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(d->copyList), GTK_WRAP_CHAR);
    Ui::accessible(d->copyList, "Pliki do skopiowania");
    auto *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 96);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 110);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), d->copyList);
    auto *card = Gx::vbox();
    Gx::addClass(card, "prism-card");
    Gx::addClass(card, "prism-exif");
    gtk_box_append(GTK_BOX(card), scroll);
    gtk_box_append(GTK_BOX(d->s.body), card);
    d->copyListSource = d->sources.join("\n");
    d->renderCopyList();
    Ui::dynamic(d->copyList, [impl] { impl->renderCopyList(); });
  }
  d->message = wrapped(
      duplicates
          ? "Identyczność: nazwa + wielkość + MD5. Wskaż katalog i kliknij "
            "„Rozpocznij wyszukiwanie”. Przenoszenie do kosza wymaga "
            "dodatkowego potwierdzenia."
          : "Wybierz miejsce zapisania kopii. Oryginał pozostanie bez zmian.");
  gtk_box_append(GTK_BOX(d->s.body), d->message);
  d->destinationLabel =
      wrapped(duplicates ? "Katalog do przeszukania nie został wybrany"
                         : "Miejsce zapisu nie zostało wybrane",
              "prism-card");
  gtk_label_set_selectable(GTK_LABEL(d->destinationLabel), TRUE);
  gtk_box_append(GTK_BOX(d->s.body), d->destinationLabel);
  if (duplicates) {
    Table::Options options;
    options.headers = {"Plik / pełna ścieżka", "Rozmiar / MD5"};
    options.widths = {0, 300};
    options.expandColumn = 0;
    options.sortable = false;
    d->table = std::make_unique<Table>(options);
    Ui::accessible(d->table->view(), "Grupy duplikatów i ścieżki plików");
    auto *card = Gx::vbox();
    Gx::addClass(card, "prism-card");
    gtk_widget_set_vexpand(card, TRUE);
    gtk_box_append(GTK_BOX(card), d->table->widget());
    gtk_box_append(GTK_BOX(d->s.body), card);
    d->table->selectionChanged = [impl] { impl->updateSelection(); };
  } else {
    auto *filler = Gx::vbox();
    gtk_widget_set_vexpand(filler, TRUE);
    gtk_box_append(GTK_BOX(d->s.body), filler);
  }
  d->progress = gtk_progress_bar_new();
  gtk_widget_set_visible(d->progress, FALSE);
  gtk_box_append(GTK_BOX(d->s.body), d->progress);
  auto *row = actionRow();
  d->choose = Ui::button("folder",
                         duplicates ? "&Wybierz katalog" : "&Wybierz miejsce",
                         {}, [impl] {
                           if (impl->searching)
                             impl->search();
                           else
                             impl->chooseDestination();
                         });
  gtk_box_append(GTK_BOX(row), d->choose);
  if (duplicates) {
    d->startButton = Ui::button("search", "&Rozpocznij wyszukiwanie", {}, [impl] {
      if (!impl->root.isEmpty())
        impl->searchIn(impl->root);
    });
    Gx::addClass(d->startButton, "suggested");
    gtk_box_append(GTK_BOX(row), d->startButton);
  }
  gtk_box_append(GTK_BOX(row), spacer());
  d->action = Ui::button(duplicates ? "trash" : "copyFiles",
                         duplicates ? "Do &kosza" : "&Zapisz kopię", {}, [impl] {
                           if (impl->searching)
                             impl->removeSelected();
                           else
                             impl->copy();
                         });
  Gx::addClass(d->action, duplicates ? "destructive" : "suggested");
  gtk_widget_set_sensitive(d->action, FALSE);
  gtk_box_append(GTK_BOX(row), d->action);
  d->cancelButton = Ui::button({}, "&Anuluj", {}, [impl] {
    if (impl->active) {
      impl->token->store(true);
      gtk_widget_set_sensitive(impl->cancelButton, FALSE);
      Ui::text(impl->message, "Anulowanie…");
    } else
      impl->requestClose();
  });
  gtk_box_append(GTK_BOX(row), d->cancelButton);
  gtk_box_append(GTK_BOX(d->s.body), row);
  if (duplicates) {
    // The library's current folder is the natural first candidate.
    const bool ready = !d->root.isEmpty() && QFileInfo(d->root).isDir();
    if (!ready)
      d->root.clear();
    else
      Ui::literal(d->destinationLabel, d->root);
    gtk_widget_set_sensitive(d->startButton, ready);
  }
  gtk_window_set_focus(d->s.window, duplicates && !d->root.isEmpty()
                                        ? d->startButton
                                        : d->choose);
  QObject::connect(&d->watcher, &QFutureWatcher<FileResult>::finished, impl,
                   [impl] { impl->finished(); });
}
FileOpsDialog::~FileOpsDialog() {
  Dialogs::dismissAll(d->s.window);
  cancelForShutdown();
  closed = {};
  d->setBusy(false);
  d->pool.waitForDone();
  d->table.reset();
  gtk_window_destroy(d->s.window);
}
void FileOpsDialog::cancelForShutdown() {
  d->shuttingDown = true;
  d->token->store(true);
  d->watcher.disconnect(d.get());
  d->active = false;
}
void FileOpsDialog::present() { gtk_window_present(d->s.window); }
void FileOpsDialog::hide() {
  gtk_widget_set_visible(GTK_WIDGET(d->s.window), FALSE);
}
bool FileOpsDialog::searching() const { return d->searching; }
bool FileOpsDialog::active() const { return d->active; }
bool FileOpsDialog::canStart() const {
  return d->startButton && gtk_widget_get_sensitive(d->startButton);
}
void FileOpsDialog::selectFolderForTest(const QString &path) {
  if (d->searching)
    d->selectRoot(path);
}
void FileOpsDialog::startForTest() {
  if (canStart())
    g_signal_emit_by_name(d->startButton, "clicked");
}
QString FileOpsDialog::messageText() const {
  return Ui::sourceText(d->message);
}
void FileOpsDialog::setDestinationForTest(const QString &path) {
  if (d->searching)
    d->searchIn(path);
  else
    d->setDestination(path);
}
void FileOpsDialog::runForTest() {
  if (!d->searching && !d->destination.isEmpty())
    d->copy();
}
void FileOpsDialog::setSources(const QStringList &value) {
  if (d->active || d->searching)
    return;
  d->sources = value;
  d->sources.removeDuplicates();
  d->source = d->sources.value(0);
  if (d->copyList) {
    d->copyListSource =
        QString("%1 plików • Ctrl/Shift w bibliotece: zaznacz wiele\n")
            .arg(d->sources.size()) +
        d->sources.mid(0, 256).join("\n") +
        (d->sources.size() > 256
             ? QString("\n… i %1 dalszych plików").arg(d->sources.size() - 256)
             : QString());
    d->renderCopyList();
  }
  d->destination.clear();
  if (d->sources.isEmpty() || d->sources.size() > 4096) {
    Ui::text(d->message, "Zaznacz od 1 do 4096 plików.");
    gtk_widget_set_sensitive(d->choose, FALSE);
  } else
    gtk_widget_set_sensitive(d->choose, TRUE);
  gtk_widget_set_sensitive(d->action, FALSE);
}

// ------------------------------------------------------------------ usb ---
struct UsbAccess::Impl : QObject {
  UsbAccess *q = nullptr;
  GtkWindow *window = nullptr;
  QProcess process;
  QTimer timeout;
  bool mounting = false, stopped = false;
  QString device;
  Alive alive;
  void fail(const QString &text) {
    if (q->failed && !stopped)
      q->failed(text);
  }
  void mount(const UsbEntry &row) {
    if (!row.mount.isEmpty()) {
      if (q->opened)
        q->opened(row.mount);
      return;
    }
    const auto command = QStandardPaths::findExecutable("udisksctl");
    if (command.isEmpty()) {
      fail("Brak udisksctl. Zamontuj nośnik w programie Pliki lub zainstaluj "
           "udisks2.");
      return;
    }
    mounting = true;
    device = row.device;
    process.start(command, {"mount", "--block-device", device});
    timeout.start();
  }
  void finished(int code) {
    timeout.stop();
    if (stopped)
      return;
    if (code) {
      fail("USB: " +
           QString::fromUtf8(process.readAllStandardError()).left(500));
      return;
    }
    if (mounting) {
      for (const auto &v : QStorageInfo::mountedVolumes())
        if (v.isReady() && QString::fromLocal8Bit(v.device()) == device) {
          if (q->opened)
            q->opened(v.rootPath());
          return;
        }
      fail("Nośnik zamontowano, ale nie potwierdzono jego ścieżki. Spróbuj "
           "ponownie.");
      return;
    }
    const auto rows = usbEntries(process.readAllStandardOutput());
    if (rows.isEmpty()) {
      Dialogs::message(
          window, "Pendrive",
          "Nie ma pendrive’a ani dostępnego nośnika USB ze zdjęciami.", {},
          {"&Zamknij"}, 0, 0, {});
      return;
    }
    if (rows.size() == 1) {
      mount(rows.first());
      return;
    }
    QStringList names;
    for (const auto &r : rows)
      names.append(
          (r.label.isEmpty() ? Ui::tr("Nośnik USB") : r.label) + " · " +
          r.device);
    Dialogs::chooseItem(window, "Wybierz pendrive", "Dostępne nośniki USB",
                        names, [this, rows, alive = alive.flag](int index) {
                          if (*alive && index >= 0 && index < rows.size() &&
                              !stopped)
                            mount(rows.at(index));
                        });
  }
};
UsbAccess::UsbAccess(GtkWindow *parent) : d(new Impl) {
  auto *impl = d.get();
  d->q = this;
  d->window = parent;
  d->timeout.setSingleShot(true);
  d->timeout.setInterval(20000);
  QObject::connect(&d->timeout, &QTimer::timeout, impl, [impl] {
    impl->process.kill();
    impl->fail("Nośnik nie odpowiedział w ciągu 20 sekund.");
  });
  QObject::connect(&d->process, &QProcess::errorOccurred, impl,
                   [impl](QProcess::ProcessError) {
                     impl->timeout.stop();
                     impl->fail("Nie można uruchomić obsługi USB: " +
                                impl->process.errorString());
                   });
  QObject::connect(&d->process,
                   qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                   impl, [impl](int code, QProcess::ExitStatus) {
                     impl->finished(code);
                   });
}
UsbAccess::~UsbAccess() {
  shutdown();
  d->process.waitForFinished(1000);
}
void UsbAccess::shutdown() {
  d->stopped = true;
  d->timeout.stop();
  d->process.disconnect(d.get());
  d->process.kill();
}
void UsbAccess::open() {
  if (d->stopped || d->process.state() != QProcess::NotRunning)
    return;
  d->mounting = false;
  d->process.start("lsblk", {"--json", "--paths", "--output",
                             "NAME,TYPE,TRAN,RM,FSTYPE,MOUNTPOINTS,LABEL"});
  d->timeout.start();
}
