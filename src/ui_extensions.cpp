#include "localized_widgets.h"
#include "glass_widgets.h"
#include "ui_material.h"
#include "ui_extensions.h"
#include "browser_history.h"
#include <QCoreApplication>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QShortcut>
#include <QShowEvent>
#include <QVBoxLayout>
#include <cmath>
namespace {
template <typename Key>
void command(QWidget *owner, Key key, std::function<void()> run) {
  auto s = new QShortcut(key, owner);
  s->setAutoRepeat(false);
  QObject::connect(s, &QShortcut::activated, owner, std::move(run));
}
} // namespace
BookmarksDialog::BookmarksDialog(std::function<QString()> currentDirectory,
                                 QWidget *parent)
    : UiDialog(parent), model(this), proxy(this),
      directory(std::move(currentDirectory)) {
  setObjectName("bookmarksDialog");
  setWindowTitle("Zakładki katalogów · ky-prism");
  resize(790, 470);
  setMinimumSize(490, 300);
  auto layout = new QVBoxLayout(this);
  filter = new UiLineEdit;
  filter->setObjectName("bookmarkFilter");
  filter->setPlaceholderText("Filtruj nazwę lub ścieżkę · Ctrl+F");
  uiAccessible(filter,"Filtr zakładek");
  layout->addWidget(filter);
  model.setHorizontalHeaderLabels({"Katalog", "Pełna ścieżka"});
  proxy.setSourceModel(&model);
  proxy.setFilterKeyColumn(-1);
  proxy.setFilterCaseSensitivity(Qt::CaseInsensitive);
  table = new QTableView;
  table->setObjectName("bookmarkTable");
  uiAccessible(table,"Zapisane katalogi");
  table->setModel(&proxy);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSortingEnabled(true);
  table->setTabKeyNavigation(false);
  table->setWordWrap(false);
  table->verticalHeader()->hide();
  table->horizontalHeader()->setStretchLastSection(true);
  table->setColumnWidth(0, 190);
  layout->addWidget(table, 1);
  auto actions = new QHBoxLayout;
  layout->addLayout(actions);
  auto button = [&](QString text, auto fn) {
    auto b = new GlassButton(text);
    b->setAutoDefault(false);
    actions->addWidget(b);
    connect(b, &QPushButton::clicked, this, fn);
    return b;
  };
  button("&Otwórz", [this] { openSelected(); })->setObjectName("bookmarkOpen");
  button("&Dodaj bieżący katalog", [this] {
    change(true);
  })->setObjectName("bookmarkAdd");
  button("&Usuń zakładkę", [this] {
    change(false);
  })->setObjectName("bookmarkRemove");
  button("Odś&wież", [this] { refresh(); });
  button("&Zamknij", [this] { hide(); });
  message = new UiLabel(
      "Enter: otwórz katalog · Delete: usuń zakładkę, pozostawiając katalog.");
  message->setTextFormat(Qt::PlainText);
  message->setWordWrap(true);
  layout->addWidget(message);
  connect(filter, &UiLineEdit::textChanged, &proxy,
          &QSortFilterProxyModel::setFilterFixedString);
  connect(table, &QTableView::activated, this, [this] { openSelected(); });
  command(this, QKeySequence::Find, [this] {
    filter->setFocus();
    filter->selectAll();
  });
  command(this, QKeySequence::Refresh, [this] { refresh(); });
  for (int column = 0; column < 2; ++column)
    command(this,
            QKeySequence(QKeyCombination(Qt::AltModifier,
                                         Qt::Key(int(Qt::Key_1) + column))),
            [this, column] {
              auto header = table->horizontalHeader();
              auto order =
                  header->sortIndicatorSection() == column &&
                          header->sortIndicatorOrder() == Qt::AscendingOrder
                      ? Qt::DescendingOrder
                      : Qt::AscendingOrder;
              table->sortByColumn(column, order);
            });
  command(this, QKeySequence::Close, [this] { hide(); });
  auto remove = new QShortcut(QKeySequence(Qt::Key_Delete), table);
  remove->setContext(Qt::WidgetWithChildrenShortcut);
  connect(remove, &QShortcut::activated, this, [this] { change(false); });
}
QString BookmarksDialog::selected() const {
  auto index = proxy.index(table->currentIndex().row(), 1);
  return index.isValid() ? proxy.data(index).toString() : QString();
}
void BookmarksDialog::refresh() {
  try {
    auto paths = FolderBookmarks::read();
    model.removeRows(0, model.rowCount());
    for (const auto &path : paths) {
      QString name = QFileInfo(path).fileName();
      auto item = new QStandardItem(name.isEmpty() ? path : name);
      auto location = new QStandardItem(path);
      item->setToolTip(path);
      location->setToolTip(path);
      model.appendRow({item, location});
    }
    if (proxy.rowCount() > 0)
      table->selectRow(0);
    message->setText(QString("%1 zapisanych katalogów · Enter: otwórz · "
                             "Delete: usuń tylko zakładkę.")
                         .arg(paths.size()));
  } catch (const std::exception &e) {
    message->setText(QString::fromUtf8(e.what()));
  }
}
void BookmarksDialog::change(bool add) {
  try {
    auto path = add ? directory() : selected();
    if (path.isEmpty())
      return;
    FolderBookmarks::change(path, add);
    refresh();
    emit changed();
  } catch (const std::exception &e) {
    message->setText(QString::fromUtf8(e.what()));
  }
}
void BookmarksDialog::openSelected() {
  auto path = selected();
  if (path.isEmpty())
    return;
  if (!QFileInfo(path).isDir()) {
    message->setText("Katalog jest niedostępny: " + path);
    return;
  }
  emit openRequested(path);
  hide();
}
void BookmarksDialog::showEvent(QShowEvent *event) {
  QDialog::showEvent(event);
  refresh();
  filter->setFocus();
  filter->selectAll();
}

ProductMark::ProductMark(QWidget *parent) : QWidget(parent) {
  setObjectName("aboutMark");
  setFixedHeight(170);
  reveal.setDuration(640);
  reveal.setEasingCurve(QEasingCurve::OutCubic);
  reveal.setStartValue(0.0);
  reveal.setEndValue(1.0);
  connect(&reveal, &QVariantAnimation::valueChanged, this,
          qOverload<>(&ProductMark::update));
}
void ProductMark::start() {
  reveal.stop();
  reveal.start();
}
void ProductMark::hideEvent(QHideEvent *event) {
  reveal.stop();
  QWidget::hideEvent(event);
}
void ProductMark::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.translate(width() / 2.0, height() / 2.0);
  double progress =
      reveal.currentValue().isValid() ? reveal.currentValue().toDouble() : 1.0;
  QColor ink = palette().color(QPalette::WindowText),
         accent = palette().color(QPalette::Highlight);
  painter.setOpacity(0.25 + 0.75 * progress);
  painter.rotate(-28 * (1 - progress));
  for (int i = 0; i < 7; ++i) {
    painter.save();
    painter.rotate(i * 360.0 / 7);
    QPainterPath blade;
    blade.moveTo(0, -60);
    blade.cubicTo(32, -56, 57, -17, 36, 17);
    blade.lineTo(15 + progress * 13, 5);
    blade.cubicTo(30, -23, 18, -40, 0, -60);
    QColor fill = accent;
    fill.setAlpha(18 + i * 4);
    painter.setPen(QPen(ink, 1.3));
    painter.setBrush(fill);
    painter.drawPath(blade);
    painter.restore();
  }
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(accent, 1.6));
  painter.drawEllipse(QPointF(0, 0), 66, 66);
  painter.setPen(QPen(ink, 0.8));
  painter.drawEllipse(QPointF(0, 0), 73, 73);
  if (progress < 1) {
    QColor glint = accent;
    glint.setAlphaF((1 - progress) * 0.8);
    painter.setPen(QPen(glint, 2));
    painter.drawArc(QRectF(-78, -78, 156, 156), int(progress * 300 * 16),
                    65 * 16);
  }
}
AboutDialog::AboutDialog(QWidget *parent) : UiDialog(parent) {
  setObjectName("aboutDialog");
  setWindowTitle("O programie · ky-prism");
  resize(670, 610);
  setMinimumSize(490, 400);
  auto layout = new QVBoxLayout(this);
  tabs = new UiTabWidget;
  tabs->setObjectName("aboutTabs");
  layout->addWidget(tabs, 1);
  auto identity = new QWidget;
  auto identityLayout = new QVBoxLayout(identity);
  mark = new ProductMark;
  identityLayout->addWidget(mark);
  auto name = new UiLabel("ky-prism");
  name->setObjectName("productName");
  name->setAlignment(Qt::AlignCenter);
  identityLayout->addWidget(name);
  auto version = new UiLabel("Wersja " + QCoreApplication::applicationVersion() +
                            " · przeglądarka zdjęć GPU");
  version->setAlignment(Qt::AlignCenter);
  identityLayout->addWidget(version);
  auto author = new UiLabel("Krzysztof Kylen Lenartowicz");
  author->setObjectName("authorName");
  author->setAlignment(Qt::AlignCenter);
  author->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                  Qt::TextSelectableByKeyboard);
  author->setFocusPolicy(Qt::StrongFocus);
  identityLayout->addWidget(author);
  auto copyright = new UiLabel("© 2026 Krzysztof Kylen Lenartowicz");
  copyright->setObjectName("copyright");
  copyright->setAlignment(Qt::AlignCenter);
  identityLayout->addWidget(copyright);
  auto libraries = new UiLabel(
      "Qt 6 · OpenGL · libvips · libjpeg-turbo · LibRaw · Little "
      "CMS\nBiblioteki zewnętrzne zachowują własne prawa i licencje.");
  libraries->setAlignment(Qt::AlignCenter);
  libraries->setWordWrap(true);
  identityLayout->addWidget(libraries);
  identityLayout->addStretch();
  tabs->addTab(identity, "O &programie");
  auto help = new QTableView;
  help->setObjectName("keyboardHelp");
  uiAccessible(help,"Skróty klawiaturowe ky-prism");
  auto keys = new UiStandardModel(help);
  keys->setHorizontalHeaderLabels({"Klawisze", "Działanie"});
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
      {"Ctrl+Shift+C", "Kopiuj zaznaczone pliki / bieżące zdjęcie…"},
      {"Ctrl+Shift+D", "Szukaj duplikatów plików…"},
      {"Ctrl+I", "Pokaż / ukryj EXIF"},
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
  for (const auto &binding : bindings)
    keys->appendRow(
        {uiItem(binding.first), uiItem(binding.second)});
  help->setModel(keys);
  help->setEditTriggers(QAbstractItemView::NoEditTriggers);
  help->setSelectionBehavior(QAbstractItemView::SelectRows);
  help->setTabKeyNavigation(false);
  help->verticalHeader()->hide();
  help->horizontalHeader()->setStretchLastSection(true);
  help->setColumnWidth(0, 235);
  help->setWordWrap(true);
  help->resizeRowsToContents();
  connect(help->horizontalHeader(), &QHeaderView::sectionResized, help,
          [help] { help->resizeRowsToContents(); });
  tabs->addTab(help, "&Klawiatura");
  auto close = new GlassButton("&Zamknij");
  close->setAutoDefault(false);
  layout->addWidget(close, 0, Qt::AlignRight);
  connect(close, &QPushButton::clicked, this, &QDialog::hide);
  connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
    if (index == 0 && isVisible())
      mark->start();
  });
  command(this, QKeySequence::Close, [this] { hide(); });
  command(this, QKeySequence(Qt::ALT | Qt::Key_1),
          [this] { tabs->setCurrentIndex(0); });
  command(this, QKeySequence(Qt::ALT | Qt::Key_2),
          [this] { tabs->setCurrentIndex(1); });
}
void AboutDialog::showPage(int page) {
  tabs->setCurrentIndex(page);
  show();
  raise();
  activateWindow();
  tabs->setFocus();
  if (page == 0)
    mark->start();
}
