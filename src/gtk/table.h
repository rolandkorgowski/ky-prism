#pragma once
#include "gtkbase.h"
#include <QList>
#include <QStringList>
#include <functional>
#include <memory>
#include <vector>

// Text table over GtkColumnView: optional header sorting, substring filter and
// single selection. Rows are addressed by their index in the list last passed
// to setRows(), independent of the current sort or filter.
class Table {
public:
  struct Options {
    QStringList headers;       // source (Polish) column titles
    QList<int> widths;         // fixed width per column; <= 0 = automatic
    int expandColumn = -1;     // column that takes the remaining width
    bool sortable = true;
    bool translateCells = false; // cell text is interface vocabulary
    bool wrap = false;
    QList<int> numericColumns; // sorted by Row::numbers instead of text
  };
  struct Row {
    QStringList cells;
    QList<double> numbers; // parallel to cells for numeric columns
    bool head = false;     // emphasised group row
    QString tooltip;
  };
  explicit Table(Options options);
  ~Table();
  Table(const Table &) = delete;
  GtkWidget *widget() const { return scroll; }
  GtkWidget *view() const { return columnView; }
  void setRows(const QList<Row> &rows);
  int selected() const;
  void select(int sourceIndex);
  void selectFirst();
  void setFilter(const QString &needle);
  void toggleSort(int column);
  int visibleCount() const;
  int rowCount() const { return int(rows.size()); }
  const Row &row(int index) const { return rows.at(index); }
  void retitle();
  std::function<void(int)> activated;
  std::function<void()> selectionChanged;

private:
  struct Column {
    Table *table;
    int index;
  };
  Options options;
  QList<Row> rows;
  QString needle;
  GtkWidget *scroll = nullptr, *columnView = nullptr;
  GListStore *store = nullptr;
  GtkCustomFilter *filter = nullptr;
  GtkSingleSelection *selection = nullptr;
  gulong selectionHandler = 0;
  std::vector<std::unique_ptr<Column>> columns;
  std::vector<GtkColumnViewColumn *> viewColumns;
  static gboolean matches(gpointer item, gpointer data);
  static int compare(gconstpointer a, gconstpointer b, gpointer data);
};
