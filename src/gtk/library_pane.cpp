#include "library_pane.h"
#include "icons.h"
#include "language.h"
#include "theme.h"
#include "thumbnail_cache.h"
#include "ui_text.h"
#include <QCache>
#include <QCollator>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QLocale>
#include <QSet>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>
#include <atomic>
#include <unordered_set>
#include <vector>

namespace {
struct Entry {
  QString name, path;
  bool dir = false;
  qint64 size = 0, modified = 0, born = -1;
  QCollatorSortKey key;
  Entry(QString n, QString p, bool d, qint64 s, qint64 m, qint64 b,
        QCollatorSortKey k)
      : name(std::move(n)), path(std::move(p)), dir(d), size(s), modified(m),
        born(b), key(std::move(k)) {}
};
struct ScanResult {
  quint64 generation = 0;
  QString directory;
  std::vector<std::shared_ptr<Entry>> entries;
};
const QSet<QString> &photoTypes() {
  static const QSet<QString> types{
      "jpg", "jpeg", "tif", "tiff", "png", "raw", "cr2", "cr3",
      "nef", "nrw",  "arw", "dng",  "raf", "rw2", "orf", "pef",
      "srw", "3fr",  "fff", "iiq",  "kdc", "mos"};
  return types;
}
} // namespace

// GObject wrapper so entries can live in a GListStore.
struct PrismEntry {
  GObject parent;
  std::shared_ptr<Entry> *entry;
};
struct PrismEntryClass {
  GObjectClass parent;
};
G_DEFINE_TYPE(PrismEntry, prism_entry, G_TYPE_OBJECT)
static void prism_entry_finalize(GObject *object) {
  auto *self = reinterpret_cast<PrismEntry *>(object);
  delete self->entry;
  self->entry = nullptr;
  G_OBJECT_CLASS(prism_entry_parent_class)->finalize(object);
}
static void prism_entry_class_init(PrismEntryClass *klass) {
  G_OBJECT_CLASS(klass)->finalize = prism_entry_finalize;
}
static void prism_entry_init(PrismEntry *self) { self->entry = nullptr; }
static PrismEntry *prism_entry_new(const std::shared_ptr<Entry> &entry) {
  auto *self = reinterpret_cast<PrismEntry *>(
      g_object_new(prism_entry_get_type(), nullptr));
  self->entry = new std::shared_ptr<Entry>(entry);
  return self;
}
static const Entry *entryOf(gpointer item) {
  auto *self = reinterpret_cast<PrismEntry *>(item);
  return self && self->entry ? self->entry->get() : nullptr;
}

namespace {
struct TextureHolder {
  GdkTexture *texture = nullptr;
  ~TextureHolder() { g_clear_object(&texture); }
};
// One grid cell. Owned by its list item's child widget.
struct Cell {
  GtkWidget *canvas = nullptr, *name = nullptr, *meta = nullptr;
  std::shared_ptr<Entry> entry;
  GdkTexture *texture = nullptr;
  ~Cell() { g_clear_object(&texture); }
};
struct RowIcon {
  bool dir = false;
};
} // namespace

struct LibraryPane::Impl : QObject {
  LibraryPane *q;
  ThumbnailCache *cache;
  bool photosOnly, stopped = false, scanInFlight = false, rescanQueued = false;
  GtkWidget *stack = nullptr, *listScroll = nullptr, *gridScroll = nullptr,
            *columnView = nullptr, *gridView = nullptr;
  GtkColumnViewColumn *nameColumn = nullptr, *sizeColumn = nullptr;
  GListStore *store = nullptr;
  GtkMultiSelection *selection = nullptr;
  Mode mode = List;
  QString directory;
  int sortColumn = 0;
  bool descending = false;
  quint64 generation = 0;
  std::vector<std::shared_ptr<Entry>> entries;
  std::shared_ptr<std::atomic_bool> cancel =
      std::make_shared<std::atomic_bool>(false);
  QThreadPool pool;
  QFutureWatcher<ScanResult> watcher;
  QFileSystemWatcher changes;
  QTimer rescanDelay, rebindDelay;
  QString pendingSelect;
  std::unordered_set<Cell *> bound;
  QCache<qint64, TextureHolder> textures{2000};
  std::shared_ptr<bool> alive = std::make_shared<bool>(true);

  Impl(LibraryPane *owner, ThumbnailCache *thumbs, bool photos)
      : q(owner), cache(thumbs), photosOnly(photos) {}

  bool less(const Entry &a, const Entry &b) const {
    if (sortColumn == 1 && a.size != b.size)
      return a.size < b.size;
    if (sortColumn == 2 && a.born != b.born)
      return a.born < b.born;
    if (sortColumn == 3 && a.modified != b.modified)
      return a.modified < b.modified;
    return a.key.compare(b.key) < 0;
  }
  void sortEntries() {
    std::stable_sort(entries.begin(), entries.end(),
                     [this](const auto &a, const auto &b) {
                       if (a->dir != b->dir)
                         return a->dir; // directories always lead
                       return descending ? less(*b, *a) : less(*a, *b);
                     });
  }
  QStringList selectedPaths() const {
    QStringList result;
    GtkBitset *set =
        gtk_selection_model_get_selection(GTK_SELECTION_MODEL(selection));
    GtkBitsetIter iter;
    guint position = 0;
    if (gtk_bitset_iter_init_first(&iter, set, &position))
      do {
        if (position < entries.size())
          result.append(entries[position]->path);
      } while (gtk_bitset_iter_next(&iter, &position));
    gtk_bitset_unref(set);
    return result;
  }
  void publish(const QStringList &keepSelected) {
    std::vector<gpointer> items;
    items.reserve(entries.size());
    for (const auto &e : entries)
      items.push_back(prism_entry_new(e));
    g_list_store_splice(store, 0, g_list_model_get_n_items(G_LIST_MODEL(store)),
                        items.data(), guint(items.size()));
    for (auto item : items)
      g_object_unref(item);
    if (!keepSelected.isEmpty()) {
      const QSet<QString> wanted(keepSelected.cbegin(), keepSelected.cend());
      for (guint i = 0; i < entries.size(); ++i)
        if (wanted.contains(entries[i]->path))
          gtk_selection_model_select_item(GTK_SELECTION_MODEL(selection), i,
                                          FALSE);
    }
  }
  void scan() {
    if (stopped || directory.isEmpty())
      return;
    if (scanInFlight) {
      cancel->store(true);
      rescanQueued = true;
      return;
    }
    rescanQueued = false;
    cancel = std::make_shared<std::atomic_bool>(false);
    auto token = cancel;
    const auto dir = directory;
    const bool photos = photosOnly;
    const quint64 gen = generation;
    scanInFlight = true;
    watcher.setFuture(QtConcurrent::run(&pool, [dir, token, photos, gen] {
      ScanResult result;
      result.generation = gen;
      result.directory = dir;
      QCollator collator;
      collator.setNumericMode(true);
      collator.setCaseSensitivity(Qt::CaseInsensitive);
      QDirIterator it(dir, QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
      int count = 0;
      while (it.hasNext() && !token->load() && count < 50000) {
        it.next();
        const auto info = it.fileInfo();
        const bool isDir = info.isDir();
        if (!isDir && photos &&
            !photoTypes().contains(info.suffix().toLower()))
          continue;
        const auto birth = info.birthTime();
        result.entries.push_back(std::make_shared<Entry>(
            info.fileName(), info.absoluteFilePath(), isDir,
            isDir ? 0 : info.size(), info.lastModified().toMSecsSinceEpoch(),
            birth.isValid() ? birth.toMSecsSinceEpoch() : -1,
            collator.sortKey(info.fileName())));
        ++count;
      }
      return result;
    }));
  }
  static bool sameListing(const std::vector<std::shared_ptr<Entry>> &a,
                          const std::vector<std::shared_ptr<Entry>> &b) {
    if (a.size() != b.size())
      return false;
    QHash<QString, const Entry *> index;
    index.reserve(qsizetype(a.size()));
    for (const auto &e : a)
      index.insert(e->path, e.get());
    for (const auto &e : b) {
      const auto *old = index.value(e->path);
      if (!old || old->size != e->size || old->modified != e->modified ||
          old->dir != e->dir)
        return false;
    }
    return true;
  }
  void finishScan() {
    scanInFlight = false;
    if (stopped)
      return;
    auto result = watcher.result();
    if (rescanQueued || cancel->load() || result.generation != generation ||
        result.directory != directory) {
      scan();
      return;
    }
    if (!sameListing(entries, result.entries)) {
      const auto keep = selectedPaths();
      entries = std::move(result.entries);
      sortEntries();
      publish(keep);
    }
    if (!pendingSelect.isEmpty()) {
      const auto path = pendingSelect;
      pendingSelect.clear();
      q->select(path);
    }
    if (q->listed)
      q->listed();
  }

  // ----------------------------------------------------------- views ----
  GdkTexture *textureFor(const QImage &source) {
    const qint64 key = source.cacheKey();
    if (auto *hit = textures.object(key))
      return hit->texture;
    QImage image = source.format() == QImage::Format_RGBA8888
                       ? source
                       : source.convertToFormat(QImage::Format_RGBA8888);
    auto *keep = new QImage(image);
    GBytes *bytes = g_bytes_new_with_free_func(
        keep->constBits(), gsize(keep->sizeInBytes()),
        +[](gpointer data) { delete static_cast<QImage *>(data); }, keep);
    auto *holder = new TextureHolder;
    holder->texture = gdk_memory_texture_new(
        keep->width(), keep->height(), GDK_MEMORY_R8G8B8A8, bytes,
        gsize(keep->bytesPerLine()));
    g_bytes_unref(bytes);
    GdkTexture *texture = holder->texture;
    // The cell keeps its own reference; eviction here never frees a visible one.
    g_object_ref(texture);
    textures.insert(key, holder);
    g_object_unref(texture);
    return textures.contains(key) ? texture : nullptr;
  }
  void requestThumb(Cell *cell) {
    if (!cell->entry || cell->entry->dir || !cache)
      return;
    const auto image = cache->request(cell->entry->path, cell->entry->size,
                                      cell->entry->modified);
    if (image.isNull())
      return;
    GdkTexture *texture = textureFor(image);
    if (texture && texture != cell->texture) {
      g_set_object(&cell->texture, texture);
      gtk_widget_queue_draw(cell->canvas);
    }
  }
  void rebindThumbnails() {
    if (stopped)
      return;
    for (auto *cell : bound)
      requestThumb(cell);
  }
  void describe(Cell *cell) {
    if (!cell->entry)
      return;
    gtk_label_set_text(GTK_LABEL(cell->name),
                       Gx::u8(cell->entry->name).constData());
    const auto meta =
        cell->entry->dir
            ? Ui::tr("Katalog")
            : Ui::tr(QString("%1 MiB").arg(cell->entry->size / 1048576.0, 0,
                                           'f', 1));
    gtk_label_set_text(GTK_LABEL(cell->meta), Gx::u8(meta).constData());
  }
  static void paintThumb(Cell *cell, GtkSnapshot *snapshot, int w, int h) {
    if (cell->texture) {
      const double tw = gdk_texture_get_width(cell->texture),
                   th = gdk_texture_get_height(cell->texture);
      const double scale = std::min(w / tw, h / th);
      const double dw = tw * scale, dh = th * scale;
      graphene_rect_t bounds = Gx::rect(
          float((w - dw) / 2), float((h - dh) / 2), float(dw), float(dh));
      gtk_snapshot_append_scaled_texture(
          snapshot, cell->texture, GSK_SCALING_FILTER_TRILINEAR, &bounds);
      return;
    }
    graphene_rect_t all = Gx::rect(0, 0, float(w), float(h));
    cairo_t *cr = gtk_snapshot_append_cairo(snapshot, &all);
    GdkRGBA ink{1, 1, 1, 1};
    gtk_widget_get_color(cell->canvas, &ink);
    ink.alpha *= 0.75f;
    if (cell->entry && cell->entry->dir)
      Icons::folderGlyph(cr, w / 2.0, h / 2.0, ink);
    else
      Icons::photoGlyph(cr, w / 2.0, h / 2.0, ink);
    cairo_destroy(cr);
  }
  GtkListItemFactory *gridFactory() {
    auto *factory = gtk_signal_list_item_factory_new();
    Gx::connect(
        factory, "setup",
        +[](GtkSignalListItemFactory *, GtkListItem *item,
                       gpointer) {
          auto *cell = new Cell;
          auto *box = Gx::vbox(3);
          cell->canvas = prism_canvas_new(
              104, 84, [cell](GtkSnapshot *s, int w, int h) {
                paintThumb(cell, s, w, h);
              });
          gtk_box_append(GTK_BOX(box), cell->canvas);
          cell->name = gtk_label_new(nullptr);
          gtk_label_set_ellipsize(GTK_LABEL(cell->name),
                                  PANGO_ELLIPSIZE_MIDDLE);
          // The picture alone sets the column width (two columns in the
          // default library panel); the caption fills it and ellipsizes.
          gtk_label_set_width_chars(GTK_LABEL(cell->name), 1);
          gtk_label_set_max_width_chars(GTK_LABEL(cell->name), 1);
          gtk_widget_set_hexpand(cell->name, TRUE);
          Gx::addClass(cell->name, "prism-thumbname");
          gtk_box_append(GTK_BOX(box), cell->name);
          cell->meta = gtk_label_new(nullptr);
          gtk_label_set_ellipsize(GTK_LABEL(cell->meta), PANGO_ELLIPSIZE_END);
          gtk_label_set_width_chars(GTK_LABEL(cell->meta), 1);
          gtk_label_set_max_width_chars(GTK_LABEL(cell->meta), 1);
          Gx::addClass(cell->meta, "prism-thumbmeta");
          gtk_box_append(GTK_BOX(box), cell->meta);
          g_object_set_data_full(
              G_OBJECT(box), "prism-cell", cell,
              +[](gpointer data) { delete static_cast<Cell *>(data); });
          gtk_list_item_set_child(item, box);
        },
        nullptr);
    Gx::connect(
        factory, "bind",
        +[](GtkSignalListItemFactory *, GtkListItem *item,
                       gpointer data) {
          auto *self = static_cast<Impl *>(data);
          auto *box = gtk_list_item_get_child(item);
          auto *cell =
              static_cast<Cell *>(g_object_get_data(G_OBJECT(box), "prism-cell"));
          auto *object =
              reinterpret_cast<PrismEntry *>(gtk_list_item_get_item(item));
          cell->entry = object && object->entry ? *object->entry : nullptr;
          g_clear_object(&cell->texture);
          self->describe(cell);
          if (cell->entry)
            gtk_widget_set_tooltip_text(box,
                                        Gx::u8(cell->entry->path).constData());
          self->bound.insert(cell);
          self->requestThumb(cell);
          gtk_widget_queue_draw(cell->canvas);
        },
        this);
    Gx::connect(
        factory, "unbind",
        +[](GtkSignalListItemFactory *, GtkListItem *item,
                       gpointer data) {
          auto *self = static_cast<Impl *>(data);
          auto *box = gtk_list_item_get_child(item);
          if (!box)
            return;
          auto *cell =
              static_cast<Cell *>(g_object_get_data(G_OBJECT(box), "prism-cell"));
          self->bound.erase(cell);
          cell->entry.reset();
          g_clear_object(&cell->texture);
        },
        this);
    Gx::connect(
        factory, "teardown",
        +[](GtkSignalListItemFactory *, GtkListItem *item,
                       gpointer data) {
          auto *self = static_cast<Impl *>(data);
          if (auto *box = gtk_list_item_get_child(item))
            self->bound.erase(static_cast<Cell *>(
                g_object_get_data(G_OBJECT(box), "prism-cell")));
        },
        this);
    return factory;
  }
  static GtkListItemFactory *nameFactory() {
    auto *factory = gtk_signal_list_item_factory_new();
    Gx::connect(
        factory, "setup",
        +[](GtkSignalListItemFactory *, GtkListItem *item,
                       gpointer) {
          auto *row = Gx::hbox(8);
          auto *state = new RowIcon;
          auto *icon = prism_cairo_canvas_new(
              20, 20, [state](cairo_t *cr, int w, int h, const GdkRGBA &ink) {
                Painter p(cr);
                p.translate((w - 20) / 2.0, (h - 20) / 2.0);
                const auto accent = Theme::rgba(Theme::palette().accent);
                p.setPen(withAlpha(ink, 0.78), 1.3);
                if (state->dir) {
                  p.setBrush(withAlpha(accent, 0.38));
                  p.roundedRect(2, 4.5, 8, 5, 1.4);
                  p.roundedRect(2, 7, 16, 9.5, 1.8);
                } else {
                  p.setBrush(withAlpha(ink, 0.10));
                  p.roundedRect(4.5, 2, 11, 16, 1.8);
                  p.line(7.5, 8.5, 12.5, 8.5);
                  p.line(7.5, 11.8, 12.5, 11.8);
                }
              });
          g_object_set_data_full(
              G_OBJECT(icon), "prism-row-icon", state,
              +[](gpointer data) { delete static_cast<RowIcon *>(data); });
          gtk_box_append(GTK_BOX(row), icon);
          auto *label = gtk_label_new(nullptr);
          gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
          gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
          gtk_widget_set_hexpand(label, TRUE);
          gtk_box_append(GTK_BOX(row), label);
          gtk_list_item_set_child(item, row);
        },
        nullptr);
    Gx::connect(
        factory, "bind",
        +[](GtkSignalListItemFactory *, GtkListItem *item,
                       gpointer) {
          const auto *entry = entryOf(gtk_list_item_get_item(item));
          auto *row = gtk_list_item_get_child(item);
          if (!entry || !row)
            return;
          auto *icon = gtk_widget_get_first_child(row);
          auto *label = gtk_widget_get_last_child(row);
          static_cast<RowIcon *>(
              g_object_get_data(G_OBJECT(icon), "prism-row-icon"))
              ->dir = entry->dir;
          gtk_widget_queue_draw(icon);
          gtk_label_set_text(GTK_LABEL(label), Gx::u8(entry->name).constData());
          gtk_widget_set_tooltip_text(row, Gx::u8(entry->path).constData());
        },
        nullptr);
    return factory;
  }
  static GtkListItemFactory *sizeFactory() {
    auto *factory = gtk_signal_list_item_factory_new();
    Gx::connect(
        factory, "setup",
        +[](GtkSignalListItemFactory *, GtkListItem *item,
                       gpointer) {
          auto *label = gtk_label_new(nullptr);
          gtk_label_set_xalign(GTK_LABEL(label), 1.0f);
          Gx::addClass(label, "prism-dim");
          gtk_list_item_set_child(item, label);
        },
        nullptr);
    Gx::connect(
        factory, "bind",
        +[](GtkSignalListItemFactory *, GtkListItem *item,
                       gpointer) {
          const auto *entry = entryOf(gtk_list_item_get_item(item));
          auto *label = gtk_list_item_get_child(item);
          if (!entry || !label)
            return;
          const auto text =
              entry->dir ? QString()
                         : QLocale().formattedDataSize(entry->size, 1);
          gtk_label_set_text(GTK_LABEL(label), Gx::u8(text).constData());
        },
        nullptr);
    return factory;
  }
  // True when (x, y) in `view` coordinates lies on an item, not on the header,
  // a scrollbar or empty space below the last row.
  static bool onItem(GtkWidget *view, double x, double y) {
    for (auto *hit = gtk_widget_pick(view, x, y, GTK_PICK_DEFAULT);
         hit && hit != view; hit = gtk_widget_get_parent(hit)) {
      const QString node = Gx::qs(gtk_widget_get_css_name(hit));
      if (node == "header" || node == "scrollbar")
        return false;
      if (node == "row" || node == "child" || node == "cell")
        return true;
    }
    return false;
  }
  void watchClicks(GtkWidget *view) {
    auto *legacy = gtk_event_controller_legacy_new();
    gtk_event_controller_set_propagation_phase(legacy, GTK_PHASE_CAPTURE);
    g_object_set_data(G_OBJECT(legacy), "prism-view", view);
    Gx::connect(
        legacy, "event",
        +[](GtkEventControllerLegacy *controller, GdkEvent *event,
                       gpointer data) -> gboolean {
          auto *self = static_cast<Impl *>(data);
          if (gdk_event_get_event_type(event) != GDK_BUTTON_RELEASE ||
              gdk_button_event_get_button(event) != GDK_BUTTON_PRIMARY)
            return FALSE;
          if (gdk_event_get_modifier_state(event) &
              (GDK_CONTROL_MASK | GDK_SHIFT_MASK))
            return FALSE;
          auto *view = GTK_WIDGET(
              g_object_get_data(G_OBJECT(controller), "prism-view"));
          double sx = 0, sy = 0, nx = 0, ny = 0;
          if (!gdk_event_get_position(event, &sx, &sy))
            return FALSE;
          auto *native = gtk_widget_get_native(view);
          if (!native)
            return FALSE;
          gtk_native_get_surface_transform(native, &nx, &ny);
          graphene_point_t in = Gx::point(float(sx - nx),
                                                    float(sy - ny));
          graphene_point_t out;
          if (!gtk_widget_compute_point(GTK_WIDGET(native), view, &in, &out) ||
              !onItem(view, out.x, out.y))
            return FALSE;
          // The list updates its selection while handling this same release.
          Gx::later([self, guard = self->alive] {
            if (!*guard || self->stopped)
              return;
            bool isDir = false;
            const auto path = self->q->selectedPath(&isDir);
            if (!path.isEmpty() && !isDir && self->q->fileClicked)
              self->q->fileClicked(path);
          });
          return FALSE;
        },
        this);
    gtk_widget_add_controller(view, legacy);
  }
  void activate(guint position) {
    if (position >= entries.size() || !q->activated)
      return;
    const auto entry = entries[position];
    q->activated(entry->path, entry->dir);
  }
  void retitle() {
    gtk_column_view_column_set_title(nameColumn,
                                     Gx::u8(Ui::tr("Nazwa")).constData());
    gtk_column_view_column_set_title(sizeColumn,
                                     Gx::u8(Ui::tr("Rozmiar")).constData());
    for (auto *cell : bound)
      describe(cell);
  }
  void build() {
    store = g_list_store_new(prism_entry_get_type());
    selection = gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(store)));
    Gx::on(selection, "selection-changed", [this] {
      if (q->selectionChanged && !stopped)
        q->selectionChanged();
    });

    columnView = gtk_column_view_new(GTK_SELECTION_MODEL(g_object_ref(selection)));
    gtk_column_view_set_show_row_separators(GTK_COLUMN_VIEW(columnView), FALSE);
    gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(columnView),
                                               FALSE);
    gtk_column_view_set_reorderable(GTK_COLUMN_VIEW(columnView), FALSE);
    nameColumn = gtk_column_view_column_new(nullptr, nameFactory());
    gtk_column_view_column_set_expand(nameColumn, TRUE);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(columnView), nameColumn);
    sizeColumn = gtk_column_view_column_new(nullptr, sizeFactory());
    gtk_column_view_column_set_fixed_width(sizeColumn, 92);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(columnView), sizeColumn);
    // The column view holds its columns; drop the creation references so the
    // columns die with the view (they leaked once per window before).
    g_object_unref(nameColumn);
    g_object_unref(sizeColumn);
    Ui::accessible(columnView, "Pliki i katalogi zdjęć");
    Gx::connect(columnView, "activate",
                     +[](GtkColumnView *, guint position,
                                    gpointer data) {
                       static_cast<Impl *>(data)->activate(position);
                     },
                     this);
    watchClicks(columnView);
    listScroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(listScroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(listScroll), columnView);

    gridView = gtk_grid_view_new(GTK_SELECTION_MODEL(g_object_ref(selection)),
                                 gridFactory());
    gtk_grid_view_set_min_columns(GTK_GRID_VIEW(gridView), 1);
    gtk_grid_view_set_max_columns(GTK_GRID_VIEW(gridView), 48);
    Ui::accessible(gridView, "Miniatury zdjęć i katalogów");
    Gx::connect(gridView, "activate",
                     +[](GtkGridView *, guint position,
                                    gpointer data) {
                       static_cast<Impl *>(data)->activate(position);
                     },
                     this);
    watchClicks(gridView);
    gridScroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(gridScroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(gridScroll), gridView);
    // Scrolling drops queued decodes for cells that left the viewport; cells
    // still visible ask again once the frame has been laid out.
    Gx::on(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(gridScroll)),
           "value-changed", [this] {
             if (stopped || !cache)
               return;
             cache->discardRequests();
             rebindDelay.start();
           });

    stack = gtk_stack_new();
    gtk_stack_set_hhomogeneous(GTK_STACK(stack), TRUE);
    gtk_stack_add_named(GTK_STACK(stack), listScroll, "list");
    gtk_stack_add_named(GTK_STACK(stack), gridScroll, "grid");
    gtk_widget_set_vexpand(stack, TRUE);
    gtk_widget_set_hexpand(stack, TRUE);
    retitle();
    Ui::dynamic(columnView, [this] { retitle(); });
    g_object_add_weak_pointer(G_OBJECT(stack),
                              reinterpret_cast<gpointer *>(&stack));
  }
};

LibraryPane::LibraryPane(ThumbnailCache *thumbnails, bool photosOnly)
    : d(new Impl(this, thumbnails, photosOnly)) {
  auto *impl = d.get();
  d->pool.setMaxThreadCount(1);
  d->pool.setThreadPriority(QThread::LowPriority);
  QObject::connect(&d->watcher, &QFutureWatcher<ScanResult>::finished, impl,
                   [impl] { impl->finishScan(); });
  d->rescanDelay.setSingleShot(true);
  d->rescanDelay.setInterval(350);
  QObject::connect(&d->rescanDelay, &QTimer::timeout, impl,
                   [impl] { impl->scan(); });
  d->rebindDelay.setSingleShot(true);
  d->rebindDelay.setInterval(16);
  QObject::connect(&d->rebindDelay, &QTimer::timeout, impl,
                   [impl] { impl->rebindThumbnails(); });
  QObject::connect(&d->changes, &QFileSystemWatcher::directoryChanged, impl,
                   [impl] {
                     if (!impl->stopped)
                       impl->rescanDelay.start();
                   });
  if (thumbnails)
    QObject::connect(thumbnails, &ThumbnailCache::changed, impl,
                     [impl] { impl->rebindThumbnails(); });
  d->build();
}
LibraryPane::~LibraryPane() {
  *d->alive = false;
  stop();
  d->pool.waitForDone();
  // Destroyed before its window: remove both views (and with them every
  // factory and controller that points back at this object).
  if (d->stack) {
    g_object_remove_weak_pointer(G_OBJECT(d->stack),
                                 reinterpret_cast<gpointer *>(&d->stack));
    gtk_column_view_set_model(GTK_COLUMN_VIEW(d->columnView), nullptr);
    gtk_grid_view_set_model(GTK_GRID_VIEW(d->gridView), nullptr);
    gtk_stack_remove(GTK_STACK(d->stack), d->listScroll);
    gtk_stack_remove(GTK_STACK(d->stack), d->gridScroll);
  }
  d->bound.clear();
  if (d->selection)
    g_signal_handlers_disconnect_matched(d->selection, G_SIGNAL_MATCH_ID,
                                         g_signal_lookup("selection-changed",
                                                         GTK_TYPE_SELECTION_MODEL),
                                         0, nullptr, nullptr, nullptr);
  g_clear_object(&d->selection);
  g_clear_object(&d->store);
}
void LibraryPane::stop() {
  d->stopped = true;
  d->cancel->store(true);
  d->rescanDelay.stop();
  d->rebindDelay.stop();
  d->watcher.disconnect(d.get());
  d->changes.disconnect(d.get());
  if (d->cache)
    QObject::disconnect(d->cache, nullptr, d.get(), nullptr);
}
GtkWidget *LibraryPane::widget() const { return d->stack; }
GtkWidget *LibraryPane::focusWidget() const {
  return d->mode == List ? d->columnView : d->gridView;
}
LibraryPane::Mode LibraryPane::mode() const { return d->mode; }
void LibraryPane::setMode(Mode mode) {
  d->mode = mode;
  gtk_stack_set_visible_child_name(GTK_STACK(d->stack),
                                   mode == List ? "list" : "grid");
  if (mode == Thumbnails)
    d->rebindDelay.start();
}
QString LibraryPane::directory() const { return d->directory; }
void LibraryPane::setDirectory(const QString &absolutePath) {
  if (d->stopped || absolutePath == d->directory)
    return;
  if (!d->changes.directories().isEmpty())
    d->changes.removePaths(d->changes.directories());
  d->directory = absolutePath;
  ++d->generation;
  d->pendingSelect.clear();
  d->entries.clear();
  d->publish({});
  if (QFileInfo(absolutePath).isDir())
    d->changes.addPath(absolutePath);
  if (d->cache)
    d->cache->discardRequests();
  d->scan();
}
void LibraryPane::refresh() { d->scan(); }
void LibraryPane::setSort(int column, bool descending) {
  if (d->sortColumn == column && d->descending == descending)
    return;
  d->sortColumn = column;
  d->descending = descending;
  const auto keep = d->selectedPaths();
  d->sortEntries();
  d->publish(keep);
}
void LibraryPane::setPhotosOnly(bool enabled) {
  if (d->photosOnly == enabled)
    return;
  d->photosOnly = enabled;
  ++d->generation;
  d->scan();
}
QStringList LibraryPane::selectedFiles() const {
  QStringList result;
  GtkBitset *set =
      gtk_selection_model_get_selection(GTK_SELECTION_MODEL(d->selection));
  GtkBitsetIter iter;
  guint position = 0;
  if (gtk_bitset_iter_init_first(&iter, set, &position))
    do {
      if (position < d->entries.size() && !d->entries[position]->dir)
        result.append(d->entries[position]->path);
    } while (gtk_bitset_iter_next(&iter, &position));
  gtk_bitset_unref(set);
  return result;
}
QString LibraryPane::selectedPath(bool *isDirectory) const {
  GtkBitset *set =
      gtk_selection_model_get_selection(GTK_SELECTION_MODEL(d->selection));
  QString result;
  if (gtk_bitset_get_size(set) == 1) {
    const guint position = gtk_bitset_get_nth(set, 0);
    if (position < d->entries.size()) {
      result = d->entries[position]->path;
      if (isDirectory)
        *isDirectory = d->entries[position]->dir;
    }
  }
  gtk_bitset_unref(set);
  return result;
}
void LibraryPane::select(const QString &path) {
  if (d->scanInFlight) {
    d->pendingSelect = path;
    return;
  }
  for (guint i = 0; i < d->entries.size(); ++i)
    if (d->entries[i]->path == path) {
      // Keep a multi-selection (e.g. two files chosen for comparison) intact.
      if (!gtk_selection_model_is_selected(GTK_SELECTION_MODEL(d->selection),
                                           i))
        gtk_selection_model_select_item(GTK_SELECTION_MODEL(d->selection), i,
                                        TRUE);
      // Scroll only a realized view; an unmapped one has no row geometry yet.
      if (d->mode == List && gtk_widget_get_mapped(d->columnView))
        gtk_column_view_scroll_to(GTK_COLUMN_VIEW(d->columnView), i, nullptr,
                                  GTK_LIST_SCROLL_NONE, nullptr);
      else if (d->mode == Thumbnails && gtk_widget_get_mapped(d->gridView))
        gtk_grid_view_scroll_to(GTK_GRID_VIEW(d->gridView), i,
                                GTK_LIST_SCROLL_NONE, nullptr);
      return;
    }
}
void LibraryPane::refreshThumbnails() { d->rebindThumbnails(); }
int LibraryPane::count() const { return int(d->entries.size()); }
bool LibraryPane::scanning() const { return d->scanInFlight; }
QStringList LibraryPane::names() const {
  QStringList result;
  for (const auto &e : d->entries)
    result.append(e->name);
  return result;
}
bool LibraryPane::entryAt(double x, double y) const {
  return Impl::onItem(focusWidget(), x, y);
}
bool LibraryPane::activateRow(int row) {
  if (row < 0 || row >= count())
    return false;
  d->activate(guint(row));
  return true;
}
void LibraryPane::selectRows(const QList<int> &rows) {
  gtk_selection_model_unselect_all(GTK_SELECTION_MODEL(d->selection));
  for (int row : rows)
    if (row >= 0 && row < count())
      gtk_selection_model_select_item(GTK_SELECTION_MODEL(d->selection),
                                      guint(row), FALSE);
}
