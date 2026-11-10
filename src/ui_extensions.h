#pragma once
#include "localized_widgets.h"
#include <QDialog>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableView>
#include <QVariantAnimation>
#include <functional>


class BookmarksDialog final : public UiDialog {
  Q_OBJECT
  UiStandardModel model;
  QSortFilterProxyModel proxy;
  QTableView *table;
  UiLineEdit *filter;
  UiLabel *message;
  std::function<QString()> directory;
  QString selected() const;
  void openSelected();
  void change(bool add);

public:
  explicit BookmarksDialog(std::function<QString()> currentDirectory,
                           QWidget *parent = nullptr);
  void refresh();
signals:
  void openRequested(const QString &);
  void changed();

protected:
  void showEvent(QShowEvent *) override;
};

class ProductMark final : public QWidget {
  QVariantAnimation reveal;

protected:
  void paintEvent(QPaintEvent *) override;
  void hideEvent(QHideEvent *) override;

public:
  explicit ProductMark(QWidget *parent = nullptr);
  void start();
  bool animating() const {
    return reveal.state() == QAbstractAnimation::Running;
  }
};
class AboutDialog final : public UiDialog {
  Q_OBJECT
  UiTabWidget *tabs;
  ProductMark *mark;

public:
  explicit AboutDialog(QWidget *parent = nullptr);
  void showPage(int page);
  bool animating() const { return mark->animating(); }
  int currentPage() const { return tabs->currentIndex(); }
};
