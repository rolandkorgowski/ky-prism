#include "table.h"
#include "ui_text.h"

struct PrismRow {
  GObject parent;
  int index;
};
struct PrismRowClass {
  GObjectClass parent;
};
G_DEFINE_TYPE(PrismRow, prism_row, G_TYPE_OBJECT)
static void prism_row_class_init(PrismRowClass *) {}
static void prism_row_init(PrismRow *self) { self->index = -1; }
static int rowIndex(gconstpointer item) {
  return item ? reinterpret_cast<const PrismRow *>(item)->index : -1;
}

gboolean Table::matches(gpointer item, gpointer data) {
  auto *self = static_cast<Table *>(data);
  if (self->needle.isEmpty())
    return TRUE;
  const int index = rowIndex(item);
  if (index < 0 || index >= self->rows.size())
    return FALSE;
  for (const auto &cell : self->rows.at(index).cells)
    if (cell.contains(self->needle, Qt::CaseInsensitive))
      return TRUE;
  return FALSE;
}
int Table::compare(gconstpointer a, gconstpointer b, gpointer data) {
  auto *column = static_cast<Column *>(data);
  auto *self = column->table;
  const int left = rowIndex(a), right = rowIndex(b), c = column->index;
  if (left < 0 || right < 0 || left >= self->rows.size() ||
      right >= self->rows.size())
    return GTK_ORDERING_EQUAL;
  const auto &x = self->rows.at(left), &y = self->rows.at(right);
  if (self->options.numericColumns.contains(c) && c < x.numbers.size() &&
      c < y.numbers.size()) {
    const double l = x.numbers.at(c), r = y.numbers.at(c);
    return l < r ? GTK_ORDERING_SMALLER
                 : l > r ? GTK_ORDERING_LARGER : GTK_ORDERING_EQUAL;
  }
  const int order = QString::localeAwareCompare(x.cells.value(c).toLower(),
                                                y.cells.value(c).toLower());
  return order < 0 ? GTK_ORDERING_SMALLER
                   : order > 0 ? GTK_ORDERING_LARGER : GTK_ORDERING_EQUAL;
}

Table::Table(Options tableOptions) : options(std::move(tableOptions)) {
  store = g_list_store_new(prism_row_get_type());
  filter = gtk_custom_filter_new(matches, this, nullptr);
  columnView = gtk_column_view_new(nullptr);
  gtk_column_view_set_show_row_separators(GTK_COLUMN_VIEW(columnView), FALSE);
  gtk_column_view_set_reorderable(GTK_COLUMN_VIEW(columnView), FALSE);
  for (int i = 0; i < options.headers.size(); ++i) {
    columns.push_back(std::make_unique<Column>(Column{this, i}));
    auto *context = columns.back().get();
    auto *factory = gtk_signal_list_item_factory_new();
    Gx::connect(
        factory, "setup",
        +[](GtkSignalListItemFactory *, GtkListItem *item, gpointer data) {
          auto *column = static_cast<Column *>(data);
          auto *label = gtk_label_new(nullptr);
          gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
          if (column->table->options.wrap) {
            gtk_label_set_wrap(GTK_LABEL(label), TRUE);
            gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
          } else
            gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
          gtk_list_item_set_child(item, label);
        },
        context);
    Gx::connect(
        factory, "bind",
        +[](GtkSignalListItemFactory *, GtkListItem *item, gpointer data) {
          auto *column = static_cast<Column *>(data);
          auto *self = column->table;
          const int index = rowIndex(gtk_list_item_get_item(item));
          auto *label = gtk_list_item_get_child(item);
          if (!label || index < 0 || index >= self->rows.size())
            return;
          const auto &row = self->rows.at(index);
          auto text = row.cells.value(column->index);
          if (self->options.translateCells)
            text = Ui::tr(text);
          gtk_label_set_text(GTK_LABEL(label), Gx::u8(text).constData());
          Gx::setClass(label, "prism-rowhead", row.head);
          const auto tip = row.tooltip.isEmpty() ? text : row.tooltip;
          gtk_widget_set_tooltip_text(
              label, self->options.wrap || tip.isEmpty()
                         ? nullptr
                         : Gx::u8(tip).constData());
        },
        context);
    auto *column = gtk_column_view_column_new(nullptr, factory);
    if (i < options.widths.size() && options.widths.at(i) > 0)
      gtk_column_view_column_set_fixed_width(column, options.widths.at(i));
    gtk_column_view_column_set_resizable(column, TRUE);
    if (i == options.expandColumn)
      gtk_column_view_column_set_expand(column, TRUE);
    if (options.sortable) {
      auto *sorter = gtk_custom_sorter_new(compare, context, nullptr);
      gtk_column_view_column_set_sorter(column, GTK_SORTER(sorter));
      g_object_unref(sorter);
    }
    gtk_column_view_append_column(GTK_COLUMN_VIEW(columnView), column);
    viewColumns.push_back(column);
    g_object_unref(column);
  }
  auto *filtered = gtk_filter_list_model_new(G_LIST_MODEL(g_object_ref(store)),
                                             GTK_FILTER(g_object_ref(filter)));
  auto *sorted = gtk_sort_list_model_new(
      G_LIST_MODEL(filtered),
      GTK_SORTER(g_object_ref(
          gtk_column_view_get_sorter(GTK_COLUMN_VIEW(columnView)))));
  selection = gtk_single_selection_new(G_LIST_MODEL(sorted));
  gtk_single_selection_set_autoselect(selection, TRUE);
  gtk_column_view_set_model(GTK_COLUMN_VIEW(columnView),
                            GTK_SELECTION_MODEL(selection));
  selectionHandler = Gx::on(selection, "notify::selected-item", [this] {
    if (selectionChanged)
      selectionChanged();
  });
  Gx::connect(
      columnView, "activate",
      +[](GtkColumnView *, guint position, gpointer data) {
        auto *self = static_cast<Table *>(data);
        g_autoptr(GObject) item = G_OBJECT(g_list_model_get_item(
            G_LIST_MODEL(self->selection), position));
        const int index = rowIndex(item);
        if (index >= 0 && self->activated)
          self->activated(index);
      },
      this);
  scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), columnView);
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_widget_set_hexpand(scroll, TRUE);
  g_object_add_weak_pointer(G_OBJECT(scroll),
                            reinterpret_cast<gpointer *>(&scroll));
  g_object_add_weak_pointer(G_OBJECT(columnView),
                            reinterpret_cast<gpointer *>(&columnView));
  retitle();
  Ui::dynamic(columnView, [this] {
    retitle();
    if (options.translateCells) {
      const guint n = g_list_model_get_n_items(G_LIST_MODEL(store));
      g_list_model_items_changed(G_LIST_MODEL(store), 0, n, n);
    }
  });
}
Table::~Table() {
  // Components are destroyed before their window. Dropping the view here also
  // drops its factories and sorters, the only users of the Column contexts.
  activated = {};
  selectionChanged = {};
  if (selection && selectionHandler)
    g_signal_handler_disconnect(selection, selectionHandler);
  if (columnView) {
    g_object_remove_weak_pointer(G_OBJECT(columnView),
                                 reinterpret_cast<gpointer *>(&columnView));
    gtk_column_view_set_model(GTK_COLUMN_VIEW(columnView), nullptr);
  }
  if (scroll) {
    g_object_remove_weak_pointer(G_OBJECT(scroll),
                                 reinterpret_cast<gpointer *>(&scroll));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), nullptr);
  }
  if (store)
    g_list_store_remove_all(store);
  rows.clear();
  g_clear_object(&selection);
  g_clear_object(&filter);
  g_clear_object(&store);
}
void Table::retitle() {
  for (size_t i = 0; i < viewColumns.size(); ++i)
    gtk_column_view_column_set_title(
        viewColumns[i], Gx::u8(Ui::tr(options.headers.value(int(i)))).constData());
}
void Table::setRows(const QList<Row> &value) {
  rows = value;
  std::vector<gpointer> items;
  items.reserve(size_t(rows.size()));
  for (int i = 0; i < rows.size(); ++i) {
    auto *row = reinterpret_cast<PrismRow *>(
        g_object_new(prism_row_get_type(), nullptr));
    row->index = i;
    items.push_back(row);
  }
  g_list_store_splice(store, 0, g_list_model_get_n_items(G_LIST_MODEL(store)),
                      items.data(), guint(items.size()));
  for (auto item : items)
    g_object_unref(item);
}
int Table::selected() const {
  return rowIndex(gtk_single_selection_get_selected_item(selection));
}
void Table::select(int sourceIndex) {
  const guint n = g_list_model_get_n_items(G_LIST_MODEL(selection));
  for (guint i = 0; i < n; ++i) {
    g_autoptr(GObject) item =
        G_OBJECT(g_list_model_get_item(G_LIST_MODEL(selection), i));
    if (rowIndex(item) == sourceIndex) {
      gtk_single_selection_set_selected(selection, i);
      return;
    }
  }
}
void Table::selectFirst() {
  if (g_list_model_get_n_items(G_LIST_MODEL(selection)) > 0)
    gtk_single_selection_set_selected(selection, 0);
}
void Table::setFilter(const QString &text) {
  if (needle == text)
    return;
  needle = text;
  gtk_filter_changed(GTK_FILTER(filter), GTK_FILTER_CHANGE_DIFFERENT);
}
void Table::toggleSort(int column) {
  if (!options.sortable || column < 0 || column >= int(viewColumns.size()))
    return;
  auto *sorter = GTK_COLUMN_VIEW_SORTER(
      gtk_column_view_get_sorter(GTK_COLUMN_VIEW(columnView)));
  const bool ascendingNow =
      gtk_column_view_sorter_get_primary_sort_column(sorter) ==
          viewColumns[size_t(column)] &&
      gtk_column_view_sorter_get_primary_sort_order(sorter) ==
          GTK_SORT_ASCENDING;
  gtk_column_view_sort_by_column(
      GTK_COLUMN_VIEW(columnView), viewColumns[size_t(column)],
      ascendingNow ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
}
int Table::visibleCount() const {
  return int(g_list_model_get_n_items(G_LIST_MODEL(selection)));
}
