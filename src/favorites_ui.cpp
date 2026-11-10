#include "localized_widgets.h"
#include "glass_widgets.h"
#include "ui_material.h"
#include "favorites_ui.h"
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>
#include <cmath>
HeartButton::HeartButton(QWidget *parent) : UiAbstractButton(parent) {
  setObjectName("favoriteHeart");
  setFocusPolicy(Qt::StrongFocus);
  setFixedSize(40, 36);
  setCursor(Qt::PointingHandCursor);
  pulse.setDuration(550);
  pulse.setStartValue(0.0);
  pulse.setEndValue(1.0);
  connect(&pulse, &QVariantAnimation::valueChanged, this,
          qOverload<>(&HeartButton::update));
  setSelected(false);
}
void HeartButton::setSelected(bool value, bool animate) {
  selected = value;
  if (!value)
    pulse.stop();
  setToolTip(value ? "Usuń zdjęcie z ulubionych"
                   : "Dodaj do ulubionych — zapisz nazwę, ścieżkę i MD5");
  setAccessibleName(value ? "Usuń zdjęcie z ulubionych" : "Dodaj do ulubionych — zapisz nazwę, ścieżkę i MD5");
  if (animate && value) {
    pulse.stop();
    pulse.start();
  }
  update();
}
void HeartButton::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  paintGlass(p, rect(), palette(), underMouse(), isDown(), false, hasFocus(), isEnabled());
  p.translate(width() / 2.0, height() / 2.0);
  if (hasFocus()) {
    p.setPen(QPen(palette().color(QPalette::Highlight), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(-18, -16, 36, 32), 5, 5);
  }
  QColor color =
      selected ? QColor("#e24659") : palette().color(QPalette::WindowText);
  if (!isEnabled())
    color.setAlpha(110);
  if (animating()) {
    double phase = pulse.currentValue().toDouble();
    QColor ray("#e24659");
    ray.setAlphaF(1 - phase);
    p.setPen(QPen(ray, 1.0));
    for (int i = 0; i < 10; ++i) {
      double angle = i * 2 * std::acos(-1) / 10;
      double r = 11 + phase * 5;
      p.drawLine(QPointF(std::cos(angle) * r, std::sin(angle) * r),
                 QPointF(std::cos(angle) * (r + 2), std::sin(angle) * (r + 2)));
    }
    double beat = 1 + 0.12 * std::sin(phase * std::acos(-1));
    p.scale(beat, beat);
  }
  QPainterPath heart;
  heart.moveTo(0, 9);
  heart.cubicTo(-3, 6, -10, 1, -10, -4);
  heart.cubicTo(-10, -11, -3, -12, 0, -6);
  heart.cubicTo(3, -12, 10, -11, 10, -4);
  heart.cubicTo(10, 1, 3, 6, 0, 9);
  p.setPen(QPen(color, 1.5));
  p.setBrush(selected ? QBrush(color) : Qt::NoBrush);
  p.drawPath(heart);
}
QVariant FavoriteModel::data(const QModelIndex &i, int role) const {
  if (!i.isValid() || i.row() < 0 || i.row() >= rows.size())
    return {};
  const auto &e = rows.at(i.row());
  if (role == Qt::ToolTipRole)
    return e.path + "\nMD5: " + e.md5;
  if (role == Qt::UserRole + 1)
    return i.column() == 3 ? QVariant(e.size)
                           : QVariant(i.column() == 0   ? e.name
                                      : i.column() == 1 ? e.path
                                                        : e.md5);
  if (role != Qt::DisplayRole)
    return {};
  switch (i.column()) {
  case 0:
    return e.name;
  case 1:
    return e.path;
  case 2:
    return e.md5;
  case 3:
    return QString::number(e.size / 1048576.0, 'f', 1) + " MiB";
  default:
    return {};
  }
}
QVariant FavoriteModel::headerData(int i, Qt::Orientation orientation,
                                   int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    return {};
  return PrismLanguage::text(QStringList{"Zdjęcie", "Ścieżka", "MD5", "Rozmiar"}.value(i));
}
Favorite FavoritesDialog::selected() const {
  return model.at(proxy.mapToSource(table->currentIndex()).row());
}
void FavoritesDialog::openSelected() {
  auto e = selected();
  if (e.id.isEmpty())
    return;
  if (!QFileInfo(e.path).isFile()) {
    message->setText(
        "Pliku nie ma w zapisanym miejscu. Użyj „Odnajdź zdjęcie”.");
    return;
  }
  emit openRequested(e.path);
  hide();
}
FavoritesDialog::FavoritesDialog(Favorites *controller, QWidget *parent)
    : UiDialog(parent), favorites(controller), model(this), proxy(this) {
  setWindowTitle("Ulubione zdjęcia");
  resize(1050, 570);
  setMinimumSize(620, 380);
  setModal(false);
  setObjectName("favoritesDialog");
  auto layout = new QVBoxLayout(this);
  auto search = new UiLineEdit;
  search->setObjectName("favoriteFilter");
  uiAccessible(search,"Filtr ulubionych");
  search->setPlaceholderText("Filtruj nazwę, ścieżkę lub MD5…");
  layout->addWidget(search);
  proxy.setSourceModel(&model);
  proxy.setFilterCaseSensitivity(Qt::CaseInsensitive);
  proxy.setFilterKeyColumn(-1);
  proxy.setSortRole(Qt::UserRole + 1);
  table = new QTableView;
  table->setObjectName("favoriteTable");
  uiAccessible(table,"Ulubione zdjęcia");
  table->setTabKeyNavigation(false);
  table->setModel(&proxy);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSortingEnabled(true);
  table->setWordWrap(false);
  table->verticalHeader()->hide();
  table->verticalHeader()->setDefaultSectionSize(32);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table->setColumnWidth(0, 205);
  table->setColumnWidth(2, 248);
  table->setColumnWidth(3, 85);
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
  auto open = button("&Otwórz", [this] { openSelected(); });
  auto find = button("O&dnajdź zdjęcie…", [this] {
    auto e = selected();
    if (e.id.isEmpty())
      return;
    auto directory = UiFileDialog::getExistingDirectory(
        this, "Gdzie szukać zdjęcia?", QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
    if (!directory.isEmpty())
      favorites->find(e.id, directory);
  });
  auto remove = button("&Usuń z ulubionych", [this] {
    auto e = selected();
    if (!e.id.isEmpty())
      favorites->remove(e.id);
  });
  auto refresh = button("Odś&wież", [this] { favorites->reload(); });
  auto cancel = button("&Anuluj operację", [this] { favorites->cancel(); });
  cancel->setVisible(favorites->busy());
  button("&Zamknij", [this] { hide(); });
  message = new UiLabel(
      "Lista przechowuje odwołania do zdjęć — nie kopiuje oryginałów.");
  message->setWordWrap(true);
  message->setTextFormat(Qt::PlainText);
  layout->addWidget(message);
  for (int column = 0; column < 4; ++column) {
    auto key =
        new QShortcut(QKeySequence(QKeyCombination(
                          Qt::AltModifier, Qt::Key(int(Qt::Key_1) + column))),
                      this);
    connect(key, &QShortcut::activated, this, [this, column] {
      auto header = table->horizontalHeader();
      auto order = header->sortIndicatorSection() == column &&
                           header->sortIndicatorOrder() == Qt::AscendingOrder
                       ? Qt::DescendingOrder
                       : Qt::AscendingOrder;
      table->sortByColumn(column, order);
    });
  }
  auto filterKey = new QShortcut(QKeySequence::Find, this);
  connect(filterKey, &QShortcut::activated, this, [search] {
    search->setFocus();
    search->selectAll();
  });
  auto closeKey = new QShortcut(QKeySequence::Close, this);
  connect(closeKey, &QShortcut::activated, this, &QDialog::hide);
  auto refreshKey = new QShortcut(QKeySequence::Refresh, this);
  connect(refreshKey, &QShortcut::activated, favorites, &Favorites::reload);
  connect(search, &UiLineEdit::textChanged, &proxy,
          &QSortFilterProxyModel::setFilterFixedString);
  connect(table, &QTableView::activated, this, [this] { openSelected(); });
  auto update = [this] {
    model.replace(favorites->all());
    setWindowTitle(
        QString("Ulubione zdjęcia · %1").arg(favorites->all().size()));
    if (model.rowCount() > 0)
      table->selectRow(0);
  };
  connect(favorites, &Favorites::changed, this, update);
  update();
  connect(favorites, &Favorites::working, this,
          [this, find, remove, refresh, open, cancel](bool working) {
            find->setEnabled(!working);
            remove->setEnabled(!working);
            refresh->setEnabled(!working);
            open->setEnabled(!working);
            cancel->setVisible(working);
            message->setText(working ? "Operacja w tle — możesz ją anulować."
                                     : "Gotowe. Ścieżkę aktualizuje wyłącznie "
                                       "plik o zgodnym rozmiarze i MD5.");
          });
  for (auto b : {find, remove, refresh, open})
    b->setEnabled(!favorites->busy());
  connect(favorites, &Favorites::error, message, &UiLabel::setText);
  connect(
      favorites, &Favorites::progress, this, [this](qint64 n, qint64 total) {
        message->setText(total < 0 ? QString("Przejrzano %1 wpisów…").arg(n)
                                   : QString("MD5: %1 / %2 MiB")
                                         .arg(n / 1048576.0, 0, 'f', 1)
                                         .arg(total / 1048576.0, 0, 'f', 1));
      });
  connect(favorites, &Favorites::found, this,
          [this](const QString &id, const QStringList &matches, bool limited) {
            if (matches.isEmpty()) {
              message->setText(
                  limited
                      ? "Osiągnięto limit 500 000 wpisów. Wskaż węższy katalog."
                      : "Nie znaleziono zgodnego pliku w dostępnych "
                        "katalogach. Możesz wskazać inne miejsce.");
              return;
            }
            QString match = matches.first();
            if (matches.size() > 1) {
              bool ok = false;
              match = UiInputDialog::getItem(
                  this, "Zgodne kopie zdjęcia",
                  limited ? "Znaleziono do 20 kopii. Wybierz ścieżkę:"
                          : "Wybierz ścieżkę:",
                  matches, 0, false, &ok);
              if (!ok)
                return;
            }
            favorites->relocate(id, match);
          });
}
