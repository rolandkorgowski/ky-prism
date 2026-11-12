#include "localized_widgets.h"
#include "file_ops.h"
#include "glass_widgets.h"
#include "ui_material.h"
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QGraphicsOpacityEffect>
#include <QHeaderView>
#include <QMap>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QSet>
#include <QShortcut>
#include <QSizeGrip>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTemporaryFile>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#ifdef __linux__
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
class DuplicateModel final : public QAbstractItemModel {
  QList<DuplicateGroup> rows;

public:
  explicit DuplicateModel(QObject *parent) : QAbstractItemModel(parent) {}
  void setGroups(const QList<DuplicateGroup> &value) {
    beginResetModel();
    rows = value;
    endResetModel();
  }
  int columnCount(const QModelIndex & = {}) const override { return 2; }
  int rowCount(const QModelIndex &parent = {}) const override {
    if (!parent.isValid())
      return rows.size();
    return parent.column() == 0 && parent.internalId() == 0
               ? rows.at(parent.row()).paths.size()
               : 0;
  }
  QModelIndex index(int row, int column,
                    const QModelIndex &parent = {}) const override {
    if (row < 0 || column < 0 || column >= 2 || row >= rowCount(parent))
      return {};
    return createIndex(row, column,
                       parent.isValid() ? quintptr(parent.row() + 1)
                                        : quintptr(0));
  }
  QModelIndex parent(const QModelIndex &child) const override {
    return child.isValid() && child.internalId()
               ? createIndex(int(child.internalId() - 1), 0, quintptr(0))
               : QModelIndex();
  }
  Qt::ItemFlags flags(const QModelIndex &index) const override {
    return !index.isValid()     ? Qt::NoItemFlags
           : index.internalId() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
                                : Qt::ItemIsEnabled;
  }
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
      return {};
    return PrismLanguage::text(section == 0 ? QString("Plik / pełna ścieżka")
                        : QString("Rozmiar / MD5"));
  }
  QVariant data(const QModelIndex &index, int role) const override {
    if (!index.isValid())
      return {};
    int group = index.internalId() ? int(index.internalId() - 1) : index.row();
    if (group < 0 || group >= rows.size())
      return {};
    const auto &g = rows.at(group);
    if (role == Qt::UserRole)
      return group;
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole)
      return {};
    if (index.internalId())
      return index.column() == 0 || role == Qt::ToolTipRole
                 ? QVariant(g.paths.at(index.row()))
                 : QVariant();
    return index.column() == 0 ? g.name
                               : QString::number(g.size) + " B · " + g.md5;
  }
};
FileDialog::FileDialog(const QString &photo, bool duplicates,
                       const QString &directory, QWidget *parent)
    : GlassDialog(parent), source(photo), root(directory),
      searching(duplicates) {
  if (!photo.isEmpty())
    sources.append(photo);
  setObjectName("fileOperationDialog");
  setWindowTitle(searching ? "Duplikaty · ky-prism"
                           : "Kopia zdjęcia · ky-prism");
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowModality(Qt::WindowModal);
  resize(searching ? 880 : 720, searching ? 560 : 460);
  setMinimumSize(searching ? 620 : 560, 380);
  pool.setMaxThreadCount(1);
  pool.setThreadPriority(QThread::LowPriority);
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 22, 24, 20);
  layout->setSpacing(14);
  auto header = new QHBoxLayout;
  auto identity = new QVBoxLayout;
  auto eyebrow = new UiLabel(searching ? "BIBLIOTEKA  /  PORZĄDKOWANIE"
                                      : "BIBLIOTEKA  /  KOPIOWANIE");
  eyebrow->setObjectName("dialogEyebrow");
  identity->addWidget(eyebrow);
  auto heading =
      new UiLabel(searching ? "Znajdź duplikaty" : "Utwórz kopię zdjęć");
  heading->setObjectName("copyHeading");
  auto headingFont = heading->font();
  headingFont.setPointSizeF(21);
  headingFont.setWeight(QFont::DemiBold);
  heading->setFont(headingFont);
  identity->addWidget(heading);
  header->addLayout(identity, 1);
  auto close = new GlassButton;
  close->setObjectName("closeFileDialog");
  close->setSymbol("close", "Zamknij okno operacji");
  close->setFixedSize(36, 36);
  close->setToolTip("Zamknij • Esc");
  connect(close, &QPushButton::clicked, this, [this] { reject(); });
  header->addWidget(close, 0, Qt::AlignTop);
  layout->addLayout(header);
  if (!searching) {
    copyList = new UiFileListEdit;
    copyList->setObjectName("copySourceList");
    copyList->setReadOnly(true);
    copyList->setTabChangesFocus(true);
    copyList->setMaximumHeight(105);
    uiAccessible(copyList,"Pliki do skopiowania");
    copyList->setPlainText(sources.join("\n"));
    layout->addWidget(copyList);
  }

  message = new UiLabel(
      searching
          ? "Identyczność: nazwa + wielkość + MD5. Wskaż katalog. Przenoszenie "
            "do kosza wymaga dodatkowego potwierdzenia."
          : "Wybierz miejsce zapisania kopii. Oryginał pozostanie bez zmian.");
  message->setTextFormat(Qt::PlainText);
  message->setWordWrap(true);
  layout->addWidget(message);
  destinationLabel =
      new UiLabel(searching ? "Katalog do przeszukania nie został wybrany"
                           : "Miejsce zapisu nie zostało wybrane");
  destinationLabel->setObjectName("destinationPath");
  destinationLabel->setTextFormat(Qt::PlainText);
  destinationLabel->setWordWrap(true);
  destinationLabel->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                            Qt::TextSelectableByKeyboard);
  layout->addWidget(destinationLabel);
  tree = new QTreeView;
  tree->setTabKeyNavigation(false);
  uiAccessible(tree,"Grupy duplikatów i ścieżki plików");
  model = new DuplicateModel(this);
  tree->setModel(model);
  tree->setHeader(new GlassHeader(tree));
  tree->header()->setStretchLastSection(false);
  tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree->setColumnWidth(1, 280);
  tree->setUniformRowHeights(true);
  tree->setSelectionMode(QAbstractItemView::SingleSelection);
  tree->setVisible(searching);
  layout->addWidget(tree, 1);
  progress = new QProgressBar;
  progress->setRange(0, 1000);
  progress->setValue(0);
  progress->setFixedHeight(7);
  progress->setTextVisible(false);
  progress->hide();
  layout->addWidget(progress);
  auto row = new QHBoxLayout;
  auto chooseGlass =
      new GlassButton(searching ? "&Wybierz katalog" : "&Wybierz miejsce");
  chooseGlass->setSymbol("folder");
  choose = chooseGlass;
  choose->setObjectName("chooseDestination");
  row->addWidget(choose);
  row->addStretch();
  auto actionGlass = new GlassButton(searching ? "Do &kosza" : "&Zapisz kopię");
  actionGlass->setSymbol(searching ? "trash" : "copyFiles");
  action = actionGlass;
  action->setObjectName("fileAction");
  action->setEnabled(false);
  row->addWidget(action);
  cancelButton = new GlassButton("&Anuluj");
  row->addWidget(cancelButton);
  layout->addLayout(row);
  auto grip = new QSizeGrip(this);
  layout->addWidget(grip, 0, Qt::AlignRight);
  for (auto button : {choose, action, cancelButton})
    button->setAutoDefault(false);
  auto closeKey = new QShortcut(QKeySequence::Close, this);
  connect(closeKey, &QShortcut::activated, this, [this] { reject(); });
  choose->setFocus();
  connect(choose, &QPushButton::clicked, this, [this] {
    if (searching)
      search();
    else
      chooseDestination();
  });
  connect(tree->selectionModel(), &QItemSelectionModel::selectionChanged, this,
          [this] { updateSelection(); });
  connect(action, &QPushButton::clicked, this, [this] {
    if (searching)
      removeSelected();
    else {
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
  });
  connect(cancelButton, &QPushButton::clicked, this, [this] {
    if (active) {
      token->store(true);
      cancelButton->setEnabled(false);
      message->setText("Anulowanie…");
    } else
      reject();
  });
  connect(&watcher, &QFutureWatcher<FileResult>::finished, this, [this] {
    auto result = watcher.result();
    active = false;
    choose->setEnabled(true);
    cancelButton->setEnabled(true);
    cancelButton->setText("Z&amknij");
    progress->setRange(0, 1000);
    progress->setValue(result.error.isEmpty() && !result.cancelled ? 1000 : 0);
    if (result.cancelled)
      message->setText(result.error.isEmpty()
                           ? QString("Anulowano. Wcześniej ukończone kopie "
                                     "pozostają w katalogu.")
                           : result.error);
    else if (!result.error.isEmpty())
      message->setText(result.error);
    else {
      message->setText(result.message);
      if (searching) {
        groups = result.groups;
        model->setGroups(groups);
        if (model->rowCount() > 0)
          tree->expand(model->index(0, 0));
      }
    }
    updateSelection();
  });
}
FileDialog::~FileDialog() {
  cancelForShutdown();
  pool.waitForDone();
}
void FileDialog::cancelForShutdown() {
  shuttingDown = true;
  token->store(true);
  watcher.disconnect(this);
  active = false;
}
void FileDialog::reject() {
  if (active) {
    token->store(true);
    message->setText("Anulowanie…");
    return;
  }
  QDialog::reject();
}
void FileDialog::showEvent(QShowEvent *event) {
  QDialog::showEvent(event);
  auto effect = new QGraphicsOpacityEffect(this);
  setGraphicsEffect(effect);
  auto fade = new QPropertyAnimation(effect, "opacity", effect);
  fade->setDuration(180);
  fade->setStartValue(0.0);
  fade->setEndValue(1.0);
  connect(fade, &QPropertyAnimation::finished, this,
          [this] { setGraphicsEffect(nullptr); });
  fade->start();
}
void FileDialog::start(
    std::function<FileResult(const std::shared_ptr<std::atomic_bool> &,
                             const Progress &)>
        job) {
  if (active || shuttingDown)
    return;
  active = true;
  token = std::make_shared<std::atomic_bool>(false);
  choose->setEnabled(false);
  action->setEnabled(false);
  cancelButton->setEnabled(true);
  cancelButton->setText("&Anuluj");
  progress->setRange(0, 0);
  progress->show();
  auto t = token;
  auto report = [this, t](qint64 n, qint64 total) {
    QMetaObject::invokeMethod(
        this,
        [this, t, n, total] {
          if (!active || t != token)
            return;
          if (total > 0) {
            progress->setRange(0, 1000);
            progress->setValue(int(double(n) / total * 1000));
          } else
            progress->setRange(0, 0);
          message->setText(
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
void FileDialog::setSources(const QStringList &value) {
  if (active || searching)
    return;
  sources = value;
  sources.removeDuplicates();
  source = sources.value(0);
  if (copyList)
    copyList->setPlainText(
        QString("%1 plików • Ctrl/Shift w bibliotece: zaznacz wiele\n")
            .arg(sources.size()) +
        sources.mid(0, 256).join("\n") +
        (sources.size() > 256
             ? QString("\n… i %1 dalszych plików").arg(sources.size() - 256)
             : QString()));
  destination.clear();
  if (sources.isEmpty() || sources.size() > 4096) {
    message->setText("Zaznacz od 1 do 4096 plików.");
    choose->setEnabled(false);
  } else
    choose->setEnabled(true);
  if (action)
    action->setEnabled(false);
}
void FileDialog::chooseDestination() {
  if (sources.size() > 1) {
    auto directory = UiFileDialog::getExistingDirectory(
        this, "Gdzie zapisać kopie zdjęć?", root,
        QFileDialog::DontUseNativeDialog | QFileDialog::ShowDirsOnly);
    if (directory.isEmpty())
      return;
    destination = directory;
    destinationLabel->setText(directory);
    message->setText(
        QString("%1 plików → %2\nIstniejące pliki nie będą nadpisane.")
            .arg(sources.size())
            .arg(directory));
    action->setEnabled(true);
    return;
  }
  auto name = UiFileDialog::getSaveFileName(
      this, "Gdzie zapisać kopię zdjęcia?",
      QFileInfo(source).absolutePath() + "/kopia-" +
          QFileInfo(source).fileName(),
      "Wszystkie pliki (*)", nullptr,
      QFileDialog::DontUseNativeDialog | QFileDialog::DontConfirmOverwrite);
  if (name.isEmpty())
    return;
  if (QFileInfo::exists(name) || QFileInfo(name).isSymLink()) {
    message->setText("Cel już istnieje. Wybierz inną nazwę; istniejących "
                     "plików nie nadpisujemy.");
    return;
  }
  destination = name;
  destinationLabel->setText(name);
  message->setText("Kopia: " + destination);
  action->setEnabled(true);
}
void FileDialog::search() {
  auto directory = UiFileDialog::getExistingDirectory(
      this, "Gdzie szukać duplikatów?", root,
      QFileDialog::DontUseNativeDialog | QFileDialog::ShowDirsOnly);
  if (directory.isEmpty())
    return;
  root = directory;
  destinationLabel->setText(directory);
  groups.clear();
  model->setGroups({});
  start([directory](auto t, auto p) {
    auto groups = FileOps::duplicates(directory, t, p);
    return FileResult{groups,
                      {},
                      QString("Znaleziono %1 grup duplikatów. Wybierz "
                              "konkretną ścieżkę do przeniesienia do kosza.")
                          .arg(groups.size()),
                      false};
  });
}
void FileDialog::updateSelection() {
  bool selected = !tree->selectionModel()->selectedRows().isEmpty();
  action->setEnabled(!active &&
                     (searching ? selected : !destination.isEmpty()));
  if (searching && selected && !active)
    message->setText(
        "Wybrany plik: " +
        tree->selectionModel()->selectedRows().first().data().toString() +
        "\nAlt+K: przenieś do kosza po osobnym potwierdzeniu.");
}
void FileDialog::removeSelected() {
  auto selected = tree->selectionModel()->selectedRows();
  if (active || selected.size() != 1 || !selected.first().parent().isValid())
    return;
  auto item = selected.first();
  int index = item.data(Qt::UserRole).toInt();
  if (index < 0 || index >= groups.size())
    return;
  auto group = groups.at(index);
  QString path = item.data(Qt::DisplayRole).toString();
  QMessageBox confirmation(QMessageBox::Warning,
                           PrismLanguage::text("Potwierdź przeniesienie do kosza"),
                           PrismLanguage::text("Przenieść ten plik do kosza?"),
                           QMessageBox::Yes | QMessageBox::Cancel, this);
  confirmation.setTextFormat(Qt::PlainText);
  confirmation.setInformativeText(
      path +
      PrismLanguage::text("\n\nPrzed przeniesieniem ponownie sprawdzę zawartość i drugą kopię."));
  confirmation.setDefaultButton(QMessageBox::Cancel);
  confirmation.button(QMessageBox::Yes)->setText(PrismLanguage::text("Przenieś do kosza"));
  confirmation.button(QMessageBox::Cancel)->setText(PrismLanguage::text("&Anuluj"));
  if (confirmation.exec() != QMessageBox::Yes)
    return;
  auto existing = groups;
  start([group, path, existing](auto t, auto p) mutable {
    FileOps::trash(group, path, t, p);
    for (auto &g : existing)
      g.paths.removeAll(path);
    existing.removeIf([](const auto &g) { return g.paths.size() < 2; });
    return FileResult{existing, {}, "Przeniesiono do kosza: " + path, false};
  });
}
