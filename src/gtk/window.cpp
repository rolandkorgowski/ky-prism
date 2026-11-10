#include "window.h"
#include "browser_history.h"
#include "dialogs.h"
#include "favorites.h"
#include "icons.h"
#include "language.h"
#include "library_pane.h"
#include "local_path.h"
#include "nvjpeg_backend.h"
#include "photo_view.h"
#include "pixel_simd.h"
#include "prefetch.h"
#include "settings.h"
#include "theme.h"
#include "thumbnail_cache.h"
#include "ui_text.h"
#include "window_control.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
struct TabState {
  QString file, directory, comparison;
  BrowserHistory history;
};
struct TabWidgets {
  GtkWidget *box = nullptr, *label = nullptr, *titleButton = nullptr;
  bool placeholder = true; // still shows the translated "Nowa karta"
};
GtkWidget *group() {
  auto *box = Gx::hbox(1);
  Gx::addClass(box, "toolgroup");
  gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
  return box;
}
GtkWidget *expander() {
  auto *widget = Gx::hbox();
  gtk_widget_set_hexpand(widget, TRUE);
  return widget;
}
} // namespace

struct PrismWindow::Impl : QObject {
  PrismWindow *q;
  GtkWindow *window = nullptr;
  GtkWidget *root = nullptr, *title = nullptr, *tabsBar = nullptr,
            *tabsBox = nullptr, *sidebar = nullptr, *folderPath = nullptr,
            *stage = nullptr, *photoPair = nullptr, *primaryPanel = nullptr,
            *primaryCaption = nullptr, *primaryCaptionBar = nullptr,
            *comparePanel = nullptr, *compareCaption = nullptr,
            *exifPanel = nullptr, *exifText = nullptr, *status = nullptr,
            *rendererLabel = nullptr, *diskSpace = nullptr, *percent = nullptr,
            *cancelFavorite = nullptr, *maximizeButton = nullptr,
            *bookmarkStar = nullptr, *heartButton = nullptr,
            *heartCanvas = nullptr, *filesButton = nullptr,
            *exifButton = nullptr, *backDirectory = nullptr,
            *forwardDirectory = nullptr, *welcomeButton = nullptr,
            *opacityButton = nullptr, *opacityPopover = nullptr,
            *opacityScale = nullptr, *opacityValue = nullptr,
            *languageButton = nullptr, *languageCanvas = nullptr,
            *sortDirection = nullptr, *listMode = nullptr, *gridMode = nullptr,
            *content = nullptr;
  std::vector<GtkWidget *> sortButtons;
  std::vector<TabWidgets> tabWidgets;
  std::unique_ptr<PhotoView> view, compareView;
  PhotoView *selectedView = nullptr;
  std::unique_ptr<LibraryPane> libraryPane;
  ThumbnailCache *thumbnails = nullptr;
  PreviewPrefetch *prefetch = nullptr;
  Favorites *favorites = nullptr;
  std::unique_ptr<FavoritesDialog> favoritesDialog;
  std::unique_ptr<BookmarksDialog> bookmarksDialog;
  std::unique_ptr<AboutDialog> aboutDialog;
  std::unique_ptr<FileOpsDialog> fileDialog;
  std::unique_ptr<UsbAccess> usb;
  QMap<QString, Gx::Callback> actionMap;

  bool leftLoading = false, rightLoading = false, switchingTab = false,
       isShuttingDown = false, storageInFlight = false, scanInFlight = false,
       restoreMaximized = false, heartIsSelected = false, syncingUi = false,
       languageTarget = false, languageCommitted = true;
  double heartPulse = -1;
  int activeTabIndex = 0, sortColumn = 0;
  QString path, profile, comparedPath, leftMetadata, rightMetadata,
      storageDirectory, scanDirectory, prefetchSignature;
  QStringList siblings;
  QList<TabState> tabStates;
  BrowserHistory history;
  QThreadPool storagePool, scanPool;
  std::shared_ptr<std::atomic_bool> scanCancelled =
      std::make_shared<std::atomic_bool>(false);
  QFutureWatcher<QString> storageWatcher;
  QFutureWatcher<QStringList> scan;
  QTimer storageTimer, prefetchDelay, maximizeTimeout, opacitySave;
  QProcess maximizeProcess;
  Animation heartAnimation, languageFlip;
  // Cleared first on destruction: late chooser callbacks then do nothing.
  std::shared_ptr<bool> alive = std::make_shared<bool>(true);

  explicit Impl(PrismWindow *owner) : q(owner) {}

  // ----------------------------------------------------------- helpers ---
  void setStatus(const QString &text) {
    if (status)
      Ui::text(status, text);
  }
  bool visibleNow() const {
    return window && gtk_widget_get_mapped(GTK_WIDGET(window)) &&
           !gtk_window_is_suspended(window);
  }
  bool comparing() const {
    return comparePanel && gtk_widget_get_visible(comparePanel);
  }
  PhotoView *activeView() const {
    return selectedView == compareView.get() && comparing() ? compareView.get()
                                                            : view.get();
  }
  QString activePhoto() const {
    return activeView() == compareView.get() ? comparedPath : path;
  }
  void showPage(bool welcome) {
    gtk_stack_set_visible_child_name(GTK_STACK(stage),
                                     welcome ? "welcome" : "photos");
  }
  bool welcomeShown() const {
    return Gx::qs(gtk_stack_get_visible_child_name(GTK_STACK(stage))) ==
           "welcome";
  }
  void setTitles(const QString &name) {
    if (name.isEmpty()) {
      Ui::text(title, "ky-prism");
      Ui::title(window, "ky-prism • Zdjęcia");
    } else {
      Ui::literal(title, name);
      Ui::literalTitle(window, name + " — ky-prism");
    }
  }
  void setHeart(bool selected, bool animate = false) {
    heartIsSelected = selected;
    if (!selected)
      heartAnimation.stop();
    heartPulse = -1;
    const QString tip =
        selected ? "Usuń zdjęcie z ulubionych"
                 : "Dodaj do ulubionych — zapisz nazwę, ścieżkę i MD5";
    Ui::tip(heartButton, tip);
    Ui::accessible(heartButton, tip);
    if (animate && selected)
      heartAnimation.start(
          heartCanvas, 550,
          [this](double t) {
            heartPulse = t;
            gtk_widget_queue_draw(heartCanvas);
          },
          [this] {
            heartPulse = -1;
            gtk_widget_queue_draw(heartCanvas);
          });
    gtk_widget_queue_draw(heartCanvas);
  }
  void setPercent(double value) {
    gtk_label_set_text(
        GTK_LABEL(percent),
        Gx::u8(QString::number(value * 100, 'f', 1) + "%").constData());
  }
  void setChecked(GtkWidget *toggle, bool value) {
    syncingUi = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), value);
    syncingUi = false;
  }

  // --------------------------------------------------------- directory ---
  void browse(const QString &dir, bool remember = true) {
    if (isShuttingDown)
      return;
    if (!validLocalPath(dir) || !QFileInfo(dir).isDir())
      return;
    const QString absolute = QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
    if (remember)
      history.visit(absolute);
    gtk_editable_set_text(GTK_EDITABLE(folderPath), Gx::u8(absolute).constData());
    libraryPane->setDirectory(absolute);
    thumbnails->discardRequests();
    if (!switchingTab && activeTabIndex < int(tabWidgets.size())) {
      gtk_widget_set_tooltip_text(tabWidgets[size_t(activeTabIndex)].box,
                                  Gx::u8(absolute).constData());
      if (path.isEmpty())
        setTabLabel(activeTabIndex, QFileInfo(absolute).fileName());
    }
    if (diskSpace)
      updateStorage();
    updateBookmark();
    gtk_widget_set_sensitive(backDirectory, history.backAvailable());
    gtk_widget_set_sensitive(forwardDirectory, history.forwardAvailable());
  }
  void directoryHistory(int direction) {
    const QString target = history.move(direction);
    if (target.isEmpty())
      return;
    if (!QFileInfo(target).isDir()) {
      history.move(-direction);
      setStatus("Katalog z historii już nie istnieje: " + target);
      return;
    }
    browse(target, false);
  }
  void parentDirectory() {
    QDir dir(Gx::qs(gtk_editable_get_text(GTK_EDITABLE(folderPath))));
    if (dir.cdUp())
      browse(dir.absolutePath());
  }
  void updateBookmark() {
    bool saved = false;
    QString tip;
    try {
      saved = FolderBookmarks::read().contains(history.current());
      tip = (saved ? "Usuń zakładkę katalogu" : "Zapisz zakładkę katalogu") +
            QString(" • Ctrl+D\n") + history.current();
    } catch (const std::exception &) {
      tip = "Nie można odczytać zakładek — otwórz przeglądarkę zakładek.";
    }
    setChecked(bookmarkStar, saved);
    Icons::rename(Ui::buttonIcon(bookmarkStar), saved ? "starFilled" : "star");
    Ui::tip(bookmarkStar, tip);
  }
  void toggleBookmark() {
    try {
      const bool saved = FolderBookmarks::read().contains(history.current());
      FolderBookmarks::change(history.current(), !saved);
      updateBookmark();
      if (bookmarksDialog)
        bookmarksDialog->refresh();
      setStatus(saved ? "Usunięto zakładkę. Katalog pozostaje bez zmian."
                      : "Zapisano zakładkę katalogu.");
    } catch (const std::exception &e) {
      updateBookmark();
      setStatus(QString::fromUtf8(e.what()));
    }
  }
  void focusDirectory() {
    if (!gtk_widget_get_visible(sidebar))
      setLibraryVisible(true);
    gtk_widget_grab_focus(folderPath);
    gtk_editable_select_region(GTK_EDITABLE(folderPath), 0, -1);
  }
  void setLibraryVisible(bool visible) {
    if (!sidebar)
      return; // toolbar is built before the library panel
    gtk_widget_set_visible(sidebar, visible);
    setChecked(filesButton, visible);
    updateThumbnailActivity();
  }
  void cycleFocus(int delta) {
    QList<GtkWidget *> regions;
    if (!welcomeShown()) {
      if (gtk_widget_get_visible(primaryPanel))
        regions.append(view->focusWidget());
      if (comparing())
        regions.append(compareView->focusWidget());
    } else
      regions.append(welcomeButton);
    if (gtk_widget_get_visible(sidebar)) {
      regions.append(libraryPane->focusWidget());
      regions.append(folderPath);
    }
    if (gtk_widget_get_visible(exifPanel))
      regions.append(exifText);
    if (regions.isEmpty())
      return;
    auto *focus = gtk_window_get_focus(window);
    int current = -1;
    for (int i = 0; i < regions.size() && focus; ++i)
      if (focus == regions.at(i) || gtk_widget_is_ancestor(focus, regions.at(i)))
        current = i;
    const int next = current < 0 ? 0
                                 : (current + delta + regions.size()) %
                                       regions.size();
    gtk_widget_grab_focus(regions.at(next));
  }

  // ------------------------------------------------------------- photos ---
  // focusPhoto: requests that did not come from the library list (chooser,
  // drop, favourites, command line) hand the keyboard to the photograph.
  void open(const QString &name, bool focusPhoto = false) {
    if (isShuttingDown)
      return;
    if (!validLocalPath(name)) {
      setStatus("Nieprawidłowa lub zbyt długa ścieżka.");
      return;
    }
    if (QFileInfo(name).isDir()) {
      browse(name);
      return;
    }
    browse(QFileInfo(name).absolutePath());
    path = QDir::cleanPath(QFileInfo(name).absoluteFilePath());
    const bool restoringPrimary = !gtk_widget_get_visible(primaryPanel);
    gtk_widget_set_visible(primaryPanel, TRUE);
    if (restoringPrimary && !comparedPath.isEmpty())
      balancePhotoPanes();
    gtk_widget_set_visible(primaryCaptionBar, !comparedPath.isEmpty());
    setHeart(favorites->contains(path));
    gtk_widget_set_sensitive(heartButton, !favorites->busy());
    showPage(false);
    selectedView = view.get();
    const QString name8 = QFileInfo(path).fileName();
    setTitles(name8);
    gtk_label_set_text(GTK_LABEL(primaryCaption), Gx::u8(name8).constData());
    gtk_widget_set_tooltip_text(primaryCaption, Gx::u8(path).constData());
    Ui::metadata(exifText, "Odczyt metadanych…");
    view->open(path, profile);
    setTabLabel(activeTabIndex, name8);
    if (activeTabIndex < int(tabWidgets.size()))
      gtk_widget_set_tooltip_text(tabWidgets[size_t(activeTabIndex)].box,
                                  Gx::u8(path).constData());
    libraryPane->select(path);
    if (focusPhoto)
      gtk_widget_grab_focus(view->focusWidget());
    updateStorage();
    scanSiblings();
  }
  QStringList selectedPhotos() const {
    QStringList result = libraryPane->selectedFiles();
    if (result.isEmpty() && !activePhoto().isEmpty())
      result.append(activePhoto());
    return result;
  }
  void balancePhotoPanes() {
    const int width = gtk_widget_get_width(photoPair);
    if (width > 0)
      gtk_paned_set_position(GTK_PANED(photoPair), std::max(100, width / 2));
    else
      Gx::later([this, live = alive] {
        if (*live && photoPair && gtk_widget_get_width(photoPair) > 0)
          gtk_paned_set_position(
              GTK_PANED(photoPair),
              std::max(100, gtk_widget_get_width(photoPair) / 2));
      });
  }
  void showComparison(const QString &name) {
    if (isShuttingDown)
      return;
    if (!validLocalPath(name) || !QFileInfo(name).isFile())
      return;
    const bool startingPair = !gtk_widget_get_visible(comparePanel) ||
                              !gtk_widget_get_visible(primaryPanel);
    comparedPath = QFileInfo(name).absoluteFilePath();
    gtk_label_set_text(GTK_LABEL(compareCaption),
                       Gx::u8(QFileInfo(name).fileName()).constData());
    gtk_widget_set_tooltip_text(compareCaption, Gx::u8(comparedPath).constData());
    gtk_widget_set_visible(primaryCaptionBar, !path.isEmpty());
    gtk_widget_set_visible(primaryPanel, !path.isEmpty());
    gtk_widget_set_visible(comparePanel, TRUE);
    showPage(false);
    if (startingPair && !path.isEmpty())
      balancePhotoPanes();
    compareView->open(comparedPath, profile);
    if (path.isEmpty()) {
      selectedView = compareView.get();
      setTitles(QFileInfo(comparedPath).fileName());
      scanSiblings();
    }
  }
  void toggleCompare() {
    if (!comparedPath.isEmpty()) {
      closePhotoPane(true);
      return;
    }
    const auto selected = selectedPhotos();
    if (selected.size() >= 2) {
      const auto first = selected.first(), second = selected.at(1);
      open(first);
      showComparison(second);
      return;
    }
    if (path.isEmpty()) {
      setStatus("Otwórz pierwsze zdjęcie lub zaznacz dwa pliki w bibliotece.");
      return;
    }
    Dialogs::choosePhoto(window, "Drugie zdjęcie do porównania",
                         history.current(), [this, live = alive](QString name) {
                           if (*live && !name.isEmpty() && !isShuttingDown)
                             showComparison(name);
                         });
  }
  void closePhotoPane(bool right) {
    prefetchDelay.stop();
    prefetch->cancel();
    prefetchSignature.clear();
    if (right) {
      comparedPath.clear();
      compareView->release();
      gtk_widget_set_visible(comparePanel, FALSE);
      gtk_widget_set_visible(primaryPanel, TRUE);
      gtk_widget_set_visible(primaryCaptionBar, FALSE);
      selectedView = view.get();
    } else {
      view->release();
      path.clear();
      leftMetadata.clear();
      gtk_widget_set_visible(primaryCaptionBar, FALSE);
      selectedView = compareView.get();
      if (!comparedPath.isEmpty())
        gtk_widget_set_visible(primaryPanel, FALSE);
    }
    const auto current = right ? path : comparedPath;
    if (current.isEmpty()) {
      showPage(true);
      setTitles({});
      setHeart(false);
      gtk_widget_set_sensitive(heartButton, FALSE);
      Ui::metadata(exifText, {});
    } else {
      showPage(false);
      setTitles(QFileInfo(current).fileName());
      gtk_widget_grab_focus(
          (right ? view.get() : compareView.get())->focusWidget());
      scanSiblings();
    }
  }
  void navigate(int delta) {
    const int i = siblings.indexOf(activePhoto());
    if (i >= 0 && i + delta >= 0 && i + delta < siblings.size()) {
      const auto next = siblings.at(i + delta);
      if (activeView() == compareView.get())
        showComparison(next);
      else
        open(next);
    }
  }
  void viewFocused(PhotoView *target) {
    if (isShuttingDown || !exifText)
      return;
    selectedView = target;
    setHeart(favorites->contains(activePhoto()));
    gtk_widget_set_sensitive(heartButton,
                             !favorites->busy() && !activePhoto().isEmpty());
    Ui::metadata(exifText,
                 target == view.get() ? leftMetadata : rightMetadata);
    scanSiblings();
    if (!target->imageSize().isEmpty())
      setPercent(target->displayedImageRect().width() /
                 target->imageSize().width() * target->devicePixelRatio());
  }

  // -------------------------------------------------------------- tabs ---
  void setTabLabel(int index, const QString &name) {
    if (index < 0 || index >= int(tabWidgets.size()))
      return;
    auto &tab = tabWidgets[size_t(index)];
    tab.placeholder = false;
    gtk_label_set_text(GTK_LABEL(tab.label), Gx::u8(name).constData());
  }
  void highlightTabs() {
    for (size_t i = 0; i < tabWidgets.size(); ++i)
      Gx::setClass(tabWidgets[i].box, "active", int(i) == activeTabIndex);
  }
  int tabIndexOf(GtkWidget *box) const {
    for (size_t i = 0; i < tabWidgets.size(); ++i)
      if (tabWidgets[i].box == box)
        return int(i);
    return -1;
  }
  void addTabWidget() {
    TabWidgets tab;
    tab.box = Gx::hbox(2);
    Gx::addClass(tab.box, "prism-tab");
    tab.label = gtk_label_new(nullptr);
    gtk_label_set_ellipsize(GTK_LABEL(tab.label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(tab.label), 24);
    gtk_label_set_text(GTK_LABEL(tab.label),
                       Gx::u8(Ui::tr("Nowa karta")).constData());
    tab.titleButton = gtk_button_new();
    Gx::addClass(tab.titleButton, "tabtitle");
    gtk_button_set_child(GTK_BUTTON(tab.titleButton), tab.label);
    auto *box = tab.box;
    Gx::on(tab.titleButton, "clicked", [this, box] { selectTab(tabIndexOf(box)); });
    gtk_box_append(GTK_BOX(tab.box), tab.titleButton);
    auto *close = Ui::button("tabClose", "", "Zamknij kartę • Ctrl+Shift+W",
                             [this, box] { closeTab(tabIndexOf(box)); });
    Gx::addClass(close, "tabclose");
    gtk_box_append(GTK_BOX(tab.box), close);
    gtk_box_append(GTK_BOX(tabsBox), tab.box);
    tabWidgets.push_back(tab);
  }
  void saveTab() {
    if (activeTabIndex >= 0 && activeTabIndex < tabStates.size())
      tabStates[activeTabIndex] = {path, history.current(), comparedPath,
                                   history};
  }
  void selectTab(int index) {
    if (index < 0 || index >= tabStates.size() || index == activeTabIndex)
      return;
    switchTab(index);
    highlightTabs();
  }
  void switchTab(int index) {
    if (switchingTab || index < 0 || index >= tabStates.size() ||
        index == activeTabIndex)
      return;
    saveTab();
    activeTabIndex = index;
    const auto state = tabStates.at(index);
    switchingTab = true;
    gtk_widget_set_visible(comparePanel, FALSE);
    gtk_widget_set_visible(primaryCaptionBar, FALSE);
    gtk_widget_set_visible(primaryPanel, TRUE);
    compareView->release();
    comparedPath.clear();
    view->stop();
    path.clear();
    history = state.history;
    if (!state.directory.isEmpty())
      browse(state.directory, false);
    if (!state.file.isEmpty())
      open(state.file);
    else {
      view->release();
      showPage(true);
      setTitles({});
      setHeart(false);
      gtk_widget_set_sensitive(heartButton, FALSE);
      Ui::metadata(exifText, {});
    }
    if (!state.comparison.isEmpty())
      showComparison(state.comparison);
    switchingTab = false;
    updateStorage();
  }
  void newTab() {
    if (tabStates.size() >= 24) {
      setStatus("Maksymalnie 24 karty.");
      return;
    }
    saveTab();
    TabState state;
    state.directory = history.current();
    state.history.visit(state.directory);
    tabStates.append(state);
    addTabWidget();
    gtk_widget_set_visible(tabsBar, TRUE);
    selectTab(int(tabStates.size()) - 1);
  }
  void closeTab(int index) {
    if (index < 0 || index >= tabStates.size())
      return;
    if (tabStates.size() == 1) {
      newTab();
      index = 0;
    }
    if (index == activeTabIndex)
      switchTab(index ? index - 1 : 1);
    tabStates.removeAt(index);
    gtk_box_remove(GTK_BOX(tabsBox), tabWidgets[size_t(index)].box);
    tabWidgets.erase(tabWidgets.begin() + index);
    if (activeTabIndex > index)
      --activeTabIndex;
    highlightTabs();
    gtk_widget_set_visible(tabsBar, tabStates.size() > 1);
  }

  // -------------------------------------------------- background work ---
  void updateStorage() {
    if (isShuttingDown || !diskSpace || storageInFlight)
      return;
    const auto dir = history.current();
    storageDirectory = dir;
    storageInFlight = true;
    storageWatcher.setFuture(QtConcurrent::run(&storagePool, [dir] {
      QStorageInfo storage(dir);
      if (!storage.isValid() || !storage.isReady())
        return QString("Dysk: niedostępny");
      return QString("Wolne: %1 GiB / %2 GiB")
          .arg(storage.bytesAvailable() / 1073741824.0, 0, 'f', 1)
          .arg(storage.bytesTotal() / 1073741824.0, 0, 'f', 1);
    }));
  }
  void scanSiblings() {
    if (isShuttingDown || activePhoto().isEmpty())
      return;
    const QString dir = QFileInfo(activePhoto()).absolutePath();
    if (scanInFlight || dir == scanDirectory)
      return;
    scanDirectory = dir;
    siblings.clear();
    scanInFlight = true;
    const auto cancelled = scanCancelled;
    scan.setFuture(QtConcurrent::run(&scanPool, [dir, cancelled] {
      QStringList list;
      QDirIterator it(dir, QDir::Files | QDir::Readable);
      QRegularExpression supported(
          "\\.(jpe?g|tiff?|png|raw|cr[23]|fff|nef|nrw|arw|dng|raf|rw2|orf|pef|"
          "srw|3fr|iiq|kdc|mos)$",
          QRegularExpression::CaseInsensitiveOption);
      int entries = 0;
      while (!cancelled->load() && it.hasNext() && entries++ < 50000) {
        const QString p = it.next();
        if (supported.match(p).hasMatch())
          list.append(p);
      }
      if (!cancelled->load())
        list.sort(Qt::CaseInsensitive);
      return list;
    }));
  }
  void prefetchNeighbors() {
    if (isShuttingDown || !visibleNow() || path.isEmpty() || leftLoading ||
        rightLoading)
      return;
    if (view->busy() || compareView->busy()) {
      prefetchDelay.start();
      return;
    }
    const auto signature =
        path + "/" + QString::number(view->previewViewport().width()) + "x" +
        QString::number(view->previewViewport().height());
    if (signature == prefetchSignature)
      return;
    const int index = siblings.indexOf(path);
    if (index < 0)
      return;
    QStringList adjacent;
    if (index + 1 < siblings.size())
      adjacent.append(siblings.at(index + 1));
    if (index > 0)
      adjacent.append(siblings.at(index - 1));
    prefetchSignature = signature;
    prefetch->prepare(adjacent, profile, view->previewEdge(),
                      view->previewViewport());
  }
  void updateThumbnailActivity() {
    if (thumbnails && libraryPane)
      thumbnails->setActive(!isShuttingDown && visibleNow() &&
                            gtk_widget_get_visible(sidebar) &&
                            libraryPane->mode() == LibraryPane::Thumbnails);
  }
  void loadingChanged(bool left, bool value) {
    if (isShuttingDown)
      return;
    (left ? leftLoading : rightLoading) = value;
    if (value) {
      prefetchDelay.stop();
      prefetch->cancel();
      prefetchSignature.clear();
    }
    thumbnails->setPaused(leftLoading || rightLoading);
  }

  // ------------------------------------------------------------ window ---
  void maximize() {
    if (!qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")) {
      // Omarchy suppresses client maximize requests; ask Hyprland directly.
      if (maximizeProcess.state() != QProcess::NotRunning)
        return;
      gtk_widget_set_sensitive(maximizeButton, FALSE);
      maximizeProcess.start(
          "hyprctl",
          {"eval", prismMaximizeCommand(QCoreApplication::applicationPid())});
      maximizeTimeout.start();
    } else if (gtk_window_is_maximized(window))
      gtk_window_unmaximize(window);
    else
      gtk_window_maximize(window);
  }
  void fullscreen() {
    if (gtk_window_is_fullscreen(window)) {
      gtk_window_unfullscreen(window);
      if (restoreMaximized && !gtk_window_is_maximized(window) &&
          qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
        gtk_window_maximize(window);
    } else {
      restoreMaximized = gtk_window_is_maximized(window);
      gtk_window_fullscreen(window);
    }
  }
  void showFileDialog(bool duplicates) {
    if (isShuttingDown)
      return;
    if (!duplicates && selectedPhotos().isEmpty()) {
      setStatus("Najpierw otwórz zdjęcie do skopiowania.");
      return;
    }
    if (fileDialog) {
      fileDialog->present();
      return;
    }
    fileDialog = std::make_unique<FileOpsDialog>(
        activePhoto(), duplicates,
        Gx::qs(gtk_editable_get_text(GTK_EDITABLE(folderPath))), window);
    fileDialog->closed = [this] { fileDialog.reset(); };
    if (!duplicates)
      fileDialog->setSources(selectedPhotos());
    fileDialog->present();
  }
  void showFavorites() {
    if (!favoritesDialog) {
      favoritesDialog = std::make_unique<FavoritesDialog>(favorites, window);
      favoritesDialog->openRequested = [this](const QString &name) {
        open(name, true);
      };
    }
    favoritesDialog->present();
  }
  void showBookmarks() {
    if (!bookmarksDialog) {
      bookmarksDialog = std::make_unique<BookmarksDialog>(
          [this] { return history.current(); }, window);
      bookmarksDialog->openRequested = [this](const QString &directory) {
        browse(directory);
      };
      bookmarksDialog->changed = [this] { updateBookmark(); };
    }
    bookmarksDialog->present();
  }
  void showAbout(int page) {
    if (!aboutDialog)
      aboutDialog = std::make_unique<AboutDialog>(window);
    aboutDialog->showPage(page);
  }
  void openUsb() {
    if (!usb) {
      usb = std::make_unique<UsbAccess>(window);
      usb->opened = [this](const QString &directory) {
        browse(directory);
        focusDirectory();
      };
      usb->failed = [this](const QString &text) { setStatus(text); };
    }
    usb->open();
  }
  void choosePhoto() {
    Dialogs::choosePhoto(window, "Załaduj zdjęcie", history.current(),
                         [this, live = alive](QString name) {
                           if (*live && !name.isEmpty() && !isShuttingDown)
                             open(name, true);
                         });
  }
  void toggleHeart() {
    const auto photo = activePhoto();
    if (photo.isEmpty() || !gtk_widget_get_sensitive(heartButton))
      return;
    if (favorites->contains(photo)) {
      for (const auto &e : favorites->all())
        if (e.path == photo) {
          favorites->remove(e.id);
          break;
        }
    } else
      favorites->add(photo);
  }
  void applySort() {
    const bool descending =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sortDirection));
    Icons::rename(Ui::buttonIcon(sortDirection),
                  descending ? "arrowDown" : "arrowUp");
    libraryPane->setSort(sortColumn, descending);
  }
  void commitLanguage() {
    if (languageCommitted)
      return;
    languageCommitted = true;
    const bool saved = PrismLanguage::setEnglish(languageTarget);
    Ui::retranslate();
    updateLanguageButton(saved);
  }
  void updateLanguageButton(bool saved = true) {
    const bool english = PrismLanguage::english();
    Ui::tip(languageButton,
            !saved ? "Nie można zapamiętać języka."
                   : english ? "Przełącz na polski • Ctrl+Alt+L"
                             : "Przełącz na angielski • Ctrl+Alt+L");
    Ui::accessible(languageButton, "Język interfejsu");
    gtk_widget_queue_draw(languageCanvas);
  }
  void toggleLanguage() {
    if (languageFlip.running())
      return;
    languageTarget = !PrismLanguage::english();
    languageCommitted = false;
    if (!gtk_widget_get_mapped(languageCanvas)) {
      commitLanguage();
      return;
    }
    languageFlip.start(
        languageCanvas, 340,
        [this](double t) {
          if (t >= 0.5)
            commitLanguage();
          gtk_widget_queue_draw(languageCanvas);
        },
        [this] {
          commitLanguage();
          gtk_widget_queue_draw(languageCanvas);
        });
  }
  void paintLanguage(cairo_t *cr, int w, int h, const GdkRGBA &ink) {
    const double phase = languageFlip.running() ? languageFlip.progress() : 0;
    const double flip = languageFlip.running()
                            ? std::abs(std::cos(phase * std::acos(-1.0)))
                            : 1.0;
    cairo_translate(cr, w / 2.0, h / 2.0);
    cairo_scale(cr, std::max(.015, flip), 1);
    cairo_translate(cr, -w / 2.0, -h / 2.0);
    Painter p(cr);
    const double x = 1, y = 3, fw = w - 2, fh = h - 6;
    cairo_save(cr);
    cairo_new_sub_path(cr);
    const double r = 6, pi = std::acos(-1.0);
    cairo_arc(cr, x + fw - r, y + r, r, -pi / 2, 0);
    cairo_arc(cr, x + fw - r, y + fh - r, r, 0, pi / 2);
    cairo_arc(cr, x + r, y + fh - r, r, pi / 2, pi);
    cairo_arc(cr, x + r, y + r, r, pi, 3 * pi / 2);
    cairo_close_path(cr);
    cairo_clip(cr);
    cairo_push_group(cr);
    Icons::flag(cr, x, y, fw, fh, PrismLanguage::english());
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, 0.5);
    const auto veil = Theme::rgba(Theme::palette().background, 0.38);
    cairo_set_source_rgba(cr, veil.red, veil.green, veil.blue, veil.alpha);
    cairo_paint(cr);
    cairo_restore(cr);
    auto *layout = gtk_widget_create_pango_layout(
        languageCanvas, PrismLanguage::english() ? "EN" : "PL");
    auto *font = pango_font_description_copy(pango_context_get_font_description(
        gtk_widget_get_pango_context(languageCanvas)));
    pango_font_description_set_weight(font, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_move_to(cr, (w - tw) / 2.0, (h - th) / 2.0);
    pango_cairo_layout_path(cr, layout);
    const auto shadow = Theme::rgba(Theme::palette().background, 0.9);
    cairo_set_source_rgba(cr, shadow.red, shadow.green, shadow.blue,
                          shadow.alpha);
    cairo_set_line_width(cr, 3);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgba(cr, ink.red, ink.green, ink.blue, ink.alpha);
    cairo_fill(cr);
    g_object_unref(layout);
  }

  // ----------------------------------------------------------- opacity ---
  void showOpacityValue(int percent) {
    gtk_label_set_text(GTK_LABEL(opacityValue),
                       Gx::u8(QString::number(percent) + "%").constData());
  }
  void setOpacity(int percent, bool fromSlider) {
    percent = std::clamp(percent, PrismSettings::minimumSliderOpacity, 100);
    Theme::setOpacity(percent, false);
    showOpacityValue(percent);
    if (!fromSlider) {
      syncingUi = true;
      gtk_range_set_value(GTK_RANGE(opacityScale), percent);
      syncingUi = false;
    }
    opacitySave.start();
  }
  GtkWidget *buildOpacityPopover() {
    opacityPopover = gtk_popover_new();
    Gx::addClass(opacityPopover, "prism-popover");
    auto *box = Gx::vbox(8);
    auto *head = Gx::hbox(10);
    auto *caption = Ui::label("Krycie interfejsu", "prism-title");
    gtk_widget_set_hexpand(caption, TRUE);
    gtk_box_append(GTK_BOX(head), caption);
    opacityValue = gtk_label_new(nullptr);
    Gx::addClass(opacityValue, "prism-zoom");
    gtk_label_set_xalign(GTK_LABEL(opacityValue), 1.0f);
    gtk_box_append(GTK_BOX(head), opacityValue);
    gtk_box_append(GTK_BOX(box), head);
    opacityScale = gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, PrismSettings::minimumSliderOpacity, 100, 1);
    Gx::addClass(opacityScale, "prism-scale");
    gtk_scale_set_draw_value(GTK_SCALE(opacityScale), FALSE);
    gtk_range_set_increments(GTK_RANGE(opacityScale), 1, 5);
    gtk_widget_set_size_request(opacityScale, 250, -1);
    Ui::accessible(opacityScale, "Krycie interfejsu");
    const int current =
        std::clamp(Theme::opacity(), PrismSettings::minimumSliderOpacity, 100);
    gtk_range_set_value(GTK_RANGE(opacityScale), current);
    showOpacityValue(Theme::opacity());
    Gx::on(opacityScale, "value-changed", [this] {
      if (!syncingUi)
        setOpacity(int(std::lround(gtk_range_get_value(GTK_RANGE(opacityScale)))),
                   true);
    });
    gtk_box_append(GTK_BOX(box), opacityScale);
    auto *ends = Gx::hbox();
    auto *low = Ui::label("Przezroczysty", "prism-dim");
    gtk_widget_set_hexpand(low, TRUE);
    gtk_box_append(GTK_BOX(ends), low);
    gtk_box_append(GTK_BOX(ends), Ui::label("Kryjący", "prism-dim"));
    gtk_box_append(GTK_BOX(box), ends);
    auto *note = Ui::label(
        "Dotyczy pasków, paneli i okien programu.\nZdjęcie zawsze pozostaje "
        "w pełni kryjące.",
        "prism-dim");
    gtk_box_append(GTK_BOX(box), note);
    auto *row = Gx::hbox(8);
    gtk_box_append(GTK_BOX(row), expander());
    gtk_box_append(GTK_BOX(row), Ui::button({}, "Domyślne", {}, [this] {
                     setOpacity(PrismSettings::defaultOpacity(
                                    Theme::compositorBlurs()),
                                false);
                   }));
    gtk_box_append(GTK_BOX(row), Ui::button({}, "W pełni kryjący", {},
                                            [this] { setOpacity(100, false); }));
    gtk_box_append(GTK_BOX(box), row);
    gtk_popover_set_child(GTK_POPOVER(opacityPopover), box);
    return opacityPopover;
  }

  // -------------------------------------------------------------- build ---
  GtkWidget *tool(GtkWidget *container, const QString &symbol,
                  const QString &caption, const QString &tip,
                  const QString &action) {
    auto *button = Ui::button(symbol, caption, tip, [this, action] {
      q->trigger(action);
    });
    gtk_box_append(GTK_BOX(container), button);
    return button;
  }
  GtkWidget *buildHeader() {
    auto *handle = gtk_window_handle_new();
    auto *row = Gx::hbox(4);
    Gx::addClass(row, "prism-header");
    gtk_window_handle_set_child(GTK_WINDOW_HANDLE(handle), row);
    auto *mark = prism_cairo_canvas_new(
        20, 20, [](cairo_t *cr, int w, int h, const GdkRGBA &ink) {
          Icons::productMark(cr, w, h, 1.0, ink,
                             Theme::rgba(Theme::palette().accent));
        });
    gtk_widget_set_valign(mark, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_end(mark, 6);
    gtk_box_append(GTK_BOX(row), mark);
    title = Ui::label("ky-prism", "prism-title");
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_box_append(GTK_BOX(row), title);

    languageButton = gtk_button_new();
    Gx::addClass(languageButton, "windowcontrol");
    languageCanvas = prism_cairo_canvas_new(
        44, 26, [this](cairo_t *cr, int w, int h, const GdkRGBA &ink) {
          paintLanguage(cr, w, h, ink);
        });
    gtk_button_set_child(GTK_BUTTON(languageButton), languageCanvas);
    Gx::on(languageButton, "clicked", [this] { q->trigger("language"); });
    gtk_box_append(GTK_BOX(row), languageButton);
    updateLanguageButton();

    opacityButton = gtk_menu_button_new();
    Gx::addClass(opacityButton, "windowcontrol");
    gtk_menu_button_set_child(GTK_MENU_BUTTON(opacityButton),
                              Icons::create("opacity"));
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(opacityButton),
                                buildOpacityPopover());
    Ui::tip(opacityButton, "Krycie interfejsu • Ctrl+Alt+O");
    Ui::accessible(opacityButton, "Krycie interfejsu");
    gtk_box_append(GTK_BOX(row), opacityButton);

    auto control = [&](const QString &symbol, const QString &tip,
                       const QString &action) {
      auto *button = tool(row, symbol, "", tip, action);
      Gx::addClass(button, "windowcontrol");
      return button;
    };
    control("about", "O programie • Ctrl+F1 • Pomoc: F1", "about");
    control("fullscreen", "Pełny ekran • F11 / Esc", "fullscreen");
    if (qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
      control("minimize", "Minimalizuj", "minimize");
    maximizeButton =
        control("maximize", "Maksymalizuj / przywróć • Ctrl+M", "maximize");
    Ui::accessible(maximizeButton, "Maksymalizuj lub przywróć");
    auto *close = control("close", "Zamknij • Ctrl+W", "close");
    Gx::addClass(close, "close");
    Ui::accessible(close, "Zamknij program");
    return handle;
  }
  GtkWidget *buildToolbar() {
    auto *handle = gtk_window_handle_new();
    auto *bar = Gx::hbox(8);
    Gx::addClass(bar, "prism-toolbar");
    gtk_window_handle_set_child(GTK_WINDOW_HANDLE(handle), bar);

    auto *files = group();
    auto *open = tool(files, "openPhoto", "Otwórz", "Otwórz zdjęcie • Ctrl+O",
                      "open");
    Ui::accessible(open, "Otwórz zdjęcie");
    filesButton = Ui::toggle("sidebar", "Pliki", "Biblioteka • Ctrl+B", [this] {
      if (!syncingUi)
        setLibraryVisible(
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(filesButton)));
    });
    Ui::accessible(filesButton, "Pokaż lub schowaj bibliotekę");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(filesButton), TRUE);
    gtk_box_append(GTK_BOX(files), filesButton);
    gtk_box_append(GTK_BOX(bar), files);

    auto *step = group();
    tool(step, "previous", "", "Poprzednie zdjęcie • Lewo", "previous");
    tool(step, "next", "", "Następne zdjęcie • Prawo", "next");
    gtk_box_append(GTK_BOX(bar), step);

    auto *work = group();
    tool(work, "newTab", "", "Nowa karta • Ctrl+T", "newTab");
    auto *copy = tool(
        work, "copyFiles", "",
        "Kopiuj zaznaczone zdjęcia • Ctrl+Shift+C • Ctrl/Shift: wiele plików",
        "copy");
    Ui::accessible(copy, "Kopiuj zaznaczone zdjęcia");
    tool(work, "openUsb", "", "Otwórz pendrive • Ctrl+U", "usb");
    auto *compare = tool(work, "comparePhotos", "",
                         "Porównaj dwa zdjęcia obok siebie • Ctrl+Shift+P",
                         "compare");
    Ui::accessible(compare, "Porównaj dwa zdjęcia");
    tool(work, "duplicates", "", "Znajdź duplikaty • Ctrl+Shift+D",
         "duplicates");
    gtk_box_append(GTK_BOX(bar), work);

    gtk_box_append(GTK_BOX(bar), expander());

    auto *zoom = group();
    tool(zoom, "zoomOut", "", "Pomniejsz • −", "zoomOut");
    percent = gtk_label_new(nullptr);
    Gx::addClass(percent, "prism-zoom");
    gtk_box_append(GTK_BOX(zoom), percent);
    tool(zoom, "zoomIn", "", "Powiększ • +", "zoomIn");
    tool(zoom, "fit", "Widok", "Wycentruj i dopasuj całe zdjęcie • Ctrl+0",
         "fit");
    auto *actual =
        tool(zoom, {}, "1:1",
             "Rozdzielczość 1:1 • Ctrl+1 • przeciągnij, aby przesuwać",
             "actual");
    Ui::accessible(actual, "Oryginalna rozdzielczość 1:1");
    gtk_box_append(GTK_BOX(bar), zoom);

    auto *marks = group();
    heartButton = gtk_button_new();
    Gx::addClass(heartButton, "icon");
    heartCanvas = prism_cairo_canvas_new(
        24, 22, [this](cairo_t *cr, int w, int h, const GdkRGBA &ink) {
          Icons::heart(cr, w, h, heartIsSelected, heartPulse, ink);
        });
    gtk_widget_set_halign(heartCanvas, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(heartCanvas, GTK_ALIGN_CENTER);
    gtk_button_set_child(GTK_BUTTON(heartButton), heartCanvas);
    Gx::on(heartButton, "clicked", [this] { q->trigger("heart"); });
    gtk_box_append(GTK_BOX(marks), heartButton);
    bookmarkStar = Ui::toggle("star", "", "Zapisz zakładkę katalogu • Ctrl+D",
                              [this] {
                                if (!syncingUi)
                                  toggleBookmark();
                              });
    Ui::accessible(bookmarkStar, "Zakładka bieżącego katalogu");
    gtk_box_append(GTK_BOX(marks), bookmarkStar);
    auto *bookmarks = tool(marks, "bookmarks", "",
                           "Zakładki katalogów • Ctrl+Shift+B", "bookmarks");
    Ui::accessible(bookmarks, "Przeglądarka zakładek");
    auto *favoriteList = tool(marks, "favorites", "",
                              "Ulubione zdjęcia • Ctrl+Shift+F", "favorites");
    Ui::accessible(favoriteList, "Przeglądarka ulubionych zdjęć");
    gtk_box_append(GTK_BOX(bar), marks);

    auto *meta = group();
    exifButton = Ui::toggle("exif", "EXIF", "Metadane • Ctrl+I", [this] {
      if (!syncingUi)
        gtk_widget_set_visible(
            exifPanel,
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(exifButton)));
    });
    gtk_box_append(GTK_BOX(meta), exifButton);
    gtk_box_append(GTK_BOX(bar), meta);
    return handle;
  }
  GtkWidget *buildTabs() {
    tabsBar = Gx::hbox(6);
    Gx::addClass(tabsBar, "prism-tabs");
    auto *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
    gtk_widget_set_hexpand(scroll, TRUE);
    tabsBox = Gx::hbox(2);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), tabsBox);
    gtk_box_append(GTK_BOX(tabsBar), scroll);
    auto *plus = Ui::button("addPhoto", "", "Nowa karta • Ctrl+T",
                            [this] { q->trigger("newTab"); });
    Gx::addClass(plus, "flat");
    Gx::addClass(plus, "small");
    gtk_widget_set_valign(plus, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(tabsBar), plus);
    tabStates.append(TabState{});
    addTabWidget();
    highlightTabs();
    gtk_widget_set_visible(tabsBar, FALSE);
    Ui::dynamic(tabsBar, [this] {
      for (auto &tab : tabWidgets)
        if (tab.placeholder)
          gtk_label_set_text(GTK_LABEL(tab.label),
                             Gx::u8(Ui::tr("Nowa karta")).constData());
    });
    return tabsBar;
  }
  GtkWidget *buildSidebar() {
    sidebar = Gx::vbox(8);
    Gx::addClass(sidebar, "prism-sidebar");
    gtk_widget_set_size_request(sidebar, 236, -1);
    gtk_box_append(GTK_BOX(sidebar), Ui::label("BIBLIOTEKA", "prism-section"));

    auto *navigation = Gx::hbox(6);
    auto *arrows = group();
    backDirectory = tool(arrows, "arrowLeft", "",
                         "Poprzedni katalog • Alt+Lewo", "directoryBack");
    forwardDirectory = tool(arrows, "arrowRight", "",
                            "Następny katalog • Alt+Prawo", "directoryForward");
    tool(arrows, "arrowUp", "", "Katalog nadrzędny • Alt+↑", "directoryUp");
    gtk_box_append(GTK_BOX(navigation), arrows);
    folderPath = gtk_entry_new();
    gtk_widget_set_hexpand(folderPath, TRUE);
    gtk_editable_set_width_chars(GTK_EDITABLE(folderPath), 8);
    Ui::placeholder(folderPath, "Katalog zdjęć…");
    Ui::accessible(folderPath, "Ścieżka katalogu");
    Gx::on(folderPath, "activate", [this] {
      browse(Gx::qs(gtk_editable_get_text(GTK_EDITABLE(folderPath))));
    });
    gtk_box_append(GTK_BOX(navigation), folderPath);
    gtk_box_append(GTK_BOX(sidebar), navigation);

    auto *sortRow = group();
    gtk_widget_set_halign(sortRow, GTK_ALIGN_FILL);
    const QStringList sortLabels{"A–Z", "MB", "Utw.", "Zmi."};
    const QStringList sortNames{"Nazwa", "Wielkość pliku", "Czas utworzenia",
                                "Data zmiany"};
    for (int i = 0; i < sortLabels.size(); ++i) {
      auto *choice = Ui::toggle({}, sortLabels.at(i), {});
      Ui::tip(choice, "Sortuj: " + sortNames.at(i));
      Ui::accessible(choice, "Sortuj: " + sortNames.at(i));
      gtk_widget_set_hexpand(choice, TRUE);
      if (i)
        gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(choice),
                                    GTK_TOGGLE_BUTTON(sortButtons.front()));
      Gx::on(choice, "toggled", [this, choice, i] {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(choice))) {
          sortColumn = i;
          applySort();
        }
      });
      sortButtons.push_back(choice);
      gtk_box_append(GTK_BOX(sortRow), choice);
    }
    sortDirection = Ui::toggle("arrowUp", "", "Odwróć kolejność sortowania",
                               [this] { applySort(); });
    gtk_box_append(GTK_BOX(sortRow), sortDirection);
    gtk_box_append(GTK_BOX(sidebar), sortRow);

    auto *modeRow = group();
    gtk_widget_set_halign(modeRow, GTK_ALIGN_FILL);
    listMode = Ui::toggle("list", "Lista", {});
    gridMode = Ui::toggle("grid", "Miniatury", {});
    gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(gridMode),
                                GTK_TOGGLE_BUTTON(listMode));
    for (auto *mode : {listMode, gridMode}) {
      gtk_widget_set_hexpand(mode, TRUE);
      gtk_box_append(GTK_BOX(modeRow), mode);
    }
    Gx::on(listMode, "toggled", [this] {
      libraryPane->setMode(
          gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(listMode))
              ? LibraryPane::List
              : LibraryPane::Thumbnails);
      updateThumbnailActivity();
    });
    gtk_box_append(GTK_BOX(sidebar), modeRow);

    libraryPane = std::make_unique<LibraryPane>(thumbnails, true);
    gtk_box_append(GTK_BOX(sidebar), libraryPane->widget());
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sortButtons.front()), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(listMode), TRUE);
    libraryPane->fileClicked = [this](const QString &name) { open(name); };
    libraryPane->activated = [this](const QString &name, bool isDirectory) {
      if (isDirectory)
        browse(name);
      else if (name != path)
        open(name);
    };
    return sidebar;
  }
  GtkWidget *captionBar(GtkWidget **label, bool right) {
    auto *bar = Gx::hbox(4);
    Gx::addClass(bar, "prism-caption");
    *label = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(*label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(*label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(*label, TRUE);
    gtk_box_append(GTK_BOX(bar), *label);
    const QString tip = right ? "Zamknij prawe zdjęcie" : "Zamknij lewe zdjęcie";
    auto *close = Ui::button("tabClose", "", tip,
                             [this, right] { closePhotoPane(right); });
    Gx::addClass(close, "flat");
    Gx::addClass(close, "small");
    gtk_box_append(GTK_BOX(bar), close);
    return bar;
  }
  GtkWidget *buildStage() {
    stage = gtk_stack_new();
    gtk_widget_set_hexpand(stage, TRUE);
    gtk_widget_set_vexpand(stage, TRUE);
    photoPair = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_wide_handle(GTK_PANED(photoPair), FALSE);
    view = std::make_unique<PhotoView>(
        "Zdjęcie: strzałki przełączają zdjęcia, Shift+strzałki przesuwają "
        "widok");
    compareView = std::make_unique<PhotoView>(
        "Drugie zdjęcie — niezależny zoom i przesuwanie");
    selectedView = view.get();
    primaryPanel = Gx::vbox();
    primaryCaptionBar = captionBar(&primaryCaption, false);
    gtk_widget_set_visible(primaryCaptionBar, FALSE);
    gtk_box_append(GTK_BOX(primaryPanel), primaryCaptionBar);
    gtk_box_append(GTK_BOX(primaryPanel), view->widget());
    gtk_paned_set_start_child(GTK_PANED(photoPair), primaryPanel);
    gtk_paned_set_shrink_start_child(GTK_PANED(photoPair), FALSE);
    comparePanel = Gx::vbox();
    gtk_box_append(GTK_BOX(comparePanel), captionBar(&compareCaption, true));
    gtk_box_append(GTK_BOX(comparePanel), compareView->widget());
    gtk_paned_set_end_child(GTK_PANED(photoPair), comparePanel);
    gtk_paned_set_shrink_end_child(GTK_PANED(photoPair), FALSE);
    gtk_widget_set_visible(comparePanel, FALSE);
    gtk_stack_add_named(GTK_STACK(stage), photoPair, "photos");

    auto *welcome = Gx::vbox(10);
    Gx::addClass(welcome, "prism-welcome");
    gtk_widget_set_valign(welcome, GTK_ALIGN_FILL);
    auto *top = Gx::vbox();
    gtk_widget_set_vexpand(top, TRUE);
    gtk_box_append(GTK_BOX(welcome), top);
    auto *camera = prism_cairo_canvas_new(
        220, 160, [](cairo_t *cr, int w, int h, const GdkRGBA &ink) {
          Icons::camera(cr, w, h, ink);
        });
    gtk_widget_set_halign(camera, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(welcome), camera);
    auto *hello = Ui::label("Mam oko do zdjęć. I dobry humor.", "prism-hello");
    gtk_label_set_xalign(GTK_LABEL(hello), 0.5f);
    gtk_box_append(GTK_BOX(welcome), hello);
    auto *hint = Ui::label("Wybierz zdjęcie z lewej lub upuść je tutaj.\nPokażę "
                           "je bez filtrów — obiecuję nie mrugać.",
                           "prism-dim");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.5f);
    gtk_label_set_justify(GTK_LABEL(hint), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(welcome), hint);
    welcomeButton = Ui::button("addPhoto", "Załaduj zdjęcie", {},
                               [this] { q->trigger("open"); });
    Gx::addClass(welcomeButton, "suggested");
    gtk_widget_set_halign(welcomeButton, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(welcomeButton, 8);
    gtk_box_append(GTK_BOX(welcome), welcomeButton);
    auto *bottom = Gx::vbox();
    gtk_widget_set_vexpand(bottom, TRUE);
    gtk_box_append(GTK_BOX(welcome), bottom);
    gtk_stack_add_named(GTK_STACK(stage), welcome, "welcome");
    showPage(true);
    return stage;
  }
  GtkWidget *buildExif() {
    exifPanel = Gx::vbox();
    Gx::addClass(exifPanel, "prism-exif");
    gtk_widget_set_size_request(exifPanel, 300, -1);
    exifText = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(exifText), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(exifText), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(exifText), GTK_WRAP_WORD_CHAR);
    Ui::accessible(exifText, "Metadane EXIF");
    auto *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), exifText);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(exifPanel), scroll);
    gtk_widget_set_visible(exifPanel, FALSE);
    return exifPanel;
  }
  GtkWidget *buildFooter() {
    auto *footer = Gx::vbox(2);
    Gx::addClass(footer, "prism-footer");
    auto *first = Gx::hbox(8);
    status = Ui::label("Upuść zdjęcie • kółko: zoom • przeciągnięcie: "
                       "przesuwanie • F11 / Esc: pełny ekran");
    gtk_label_set_wrap(GTK_LABEL(status), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(status), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_lines(GTK_LABEL(status), 3);
    gtk_label_set_ellipsize(GTK_LABEL(status), PANGO_ELLIPSIZE_END);
    gtk_label_set_selectable(GTK_LABEL(status), FALSE);
    gtk_widget_set_hexpand(status, TRUE);
    gtk_box_append(GTK_BOX(first), status);
    cancelFavorite = Ui::button({}, "Anuluj ulubione", {},
                                [this] { favorites->cancel(); });
    Gx::addClass(cancelFavorite, "small");
    gtk_widget_set_valign(cancelFavorite, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(cancelFavorite, FALSE);
    gtk_box_append(GTK_BOX(first), cancelFavorite);
    gtk_box_append(GTK_BOX(footer), first);
    auto *second = Gx::hbox(12);
    rendererLabel = Ui::label("GPU: inicjalizacja…", "prism-dim");
    gtk_label_set_ellipsize(GTK_LABEL(rendererLabel), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(rendererLabel, TRUE);
    gtk_box_append(GTK_BOX(second), rendererLabel);
    diskSpace = Ui::label("Miejsce: sprawdzanie…", "prism-dim");
    gtk_box_append(GTK_BOX(second), diskSpace);
    gtk_box_append(GTK_BOX(footer), second);
    return footer;
  }
  void registerActions() {
    auto &a = actionMap;
    a["open"] = [this] { choosePhoto(); };
    a["toggleLibrary"] = [this] {
      setLibraryVisible(!gtk_widget_get_visible(sidebar));
    };
    a["previous"] = [this] { navigate(-1); };
    a["next"] = [this] { navigate(1); };
    a["newTab"] = [this] { newTab(); };
    a["closeTab"] = [this] { closeTab(activeTabIndex); };
    a["nextTab"] = [this] {
      selectTab((activeTabIndex + 1) % int(tabStates.size()));
    };
    a["previousTab"] = [this] {
      selectTab((activeTabIndex + int(tabStates.size()) - 1) %
                int(tabStates.size()));
    };
    a["copy"] = [this] { showFileDialog(false); };
    a["duplicates"] = [this] { showFileDialog(true); };
    a["usb"] = [this] { openUsb(); };
    a["compare"] = [this] { toggleCompare(); };
    a["zoomOut"] = [this] { activeView()->scaleCentered(0.8); };
    a["zoomIn"] = [this] { activeView()->scaleCentered(1.25); };
    a["fit"] = [this] { activeView()->fit(); };
    a["actual"] = [this] { activeView()->actual(); };
    a["heart"] = [this] { toggleHeart(); };
    a["star"] = [this] { toggleBookmark(); };
    a["bookmarks"] = [this] { showBookmarks(); };
    a["favorites"] = [this] { showFavorites(); };
    a["exif"] = [this] {
      const bool show = !gtk_widget_get_visible(exifPanel);
      gtk_widget_set_visible(exifPanel, show);
      setChecked(exifButton, show);
    };
    a["language"] = [this] { toggleLanguage(); };
    a["about"] = [this] { showAbout(0); };
    a["help"] = [this] { showAbout(1); };
    a["fullscreen"] = [this] { fullscreen(); };
    a["maximize"] = [this] { maximize(); };
    a["minimize"] = [this] { gtk_window_minimize(window); };
    a["close"] = [this] { q->close(); };
    a["escape"] = [this] {
      if (gtk_window_is_fullscreen(window))
        fullscreen();
      else {
        gtk_widget_set_visible(exifPanel, FALSE);
        setChecked(exifButton, false);
      }
    };
    a["directoryBack"] = [this] { directoryHistory(-1); };
    a["directoryForward"] = [this] { directoryHistory(1); };
    a["directoryUp"] = [this] { parentDirectory(); };
    a["focusDirectory"] = [this] { focusDirectory(); };
    a["focusNext"] = [this] { cycleFocus(1); };
    a["focusPrevious"] = [this] { cycleFocus(-1); };
    a["cancelFavorites"] = [this] { favorites->cancel(); };
    a["opacity"] = [this] {
      gtk_menu_button_popup(GTK_MENU_BUTTON(opacityButton));
      gtk_widget_grab_focus(opacityScale);
    };
    a["opacityUp"] = [this] { setOpacity(Theme::opacity() + 5, false); };
    a["opacityDown"] = [this] { setOpacity(Theme::opacity() - 5, false); };
  }
  void registerShortcuts() {
    auto *w = GTK_WIDGET(window);
    auto key = [&](guint keyval, GdkModifierType modifiers,
                   const QString &action, bool capture = true) {
      Gx::shortcut(w, keyval, modifiers, [this, action] { q->trigger(action); },
                   capture);
    };
    key(GDK_KEY_o, Gx::Ctrl, "open");
    key(GDK_KEY_b, Gx::Ctrl, "toggleLibrary");
    key(GDK_KEY_l, Gx::Ctrl, "focusDirectory");
    key(GDK_KEY_d, Gx::Alt, "focusDirectory");
    key(GDK_KEY_F6, Gx::NoModifier, "focusNext");
    key(GDK_KEY_F6, Gx::Shift, "focusPrevious");
    key(GDK_KEY_Left, Gx::Alt, "directoryBack");
    key(GDK_KEY_Right, Gx::Alt, "directoryForward");
    key(GDK_KEY_Up, Gx::Alt, "directoryUp");
    key(GDK_KEY_d, Gx::Ctrl, "star");
    key(GDK_KEY_b, Gx::CtrlShift, "bookmarks");
    key(GDK_KEY_h, Gx::CtrlShift, "heart");
    key(GDK_KEY_f, Gx::CtrlShift, "favorites");
    key(GDK_KEY_c, Gx::CtrlShift, "copy");
    key(GDK_KEY_d, Gx::CtrlShift, "duplicates");
    key(GDK_KEY_i, Gx::Ctrl, "exif");
    key(GDK_KEY_m, Gx::Ctrl, "maximize");
    key(GDK_KEY_l, Gx::CtrlAlt, "language");
    key(GDK_KEY_o, Gx::CtrlAlt, "opacity");
    key(GDK_KEY_F1, Gx::NoModifier, "help");
    key(GDK_KEY_F1, Gx::Ctrl, "about");
    key(GDK_KEY_F11, Gx::NoModifier, "fullscreen");
    // Escape stays in the bubble phase so an open popover or an editing
    // control handles it first.
    key(GDK_KEY_Escape, Gx::NoModifier, "escape", false);
    key(GDK_KEY_w, Gx::Ctrl, "close");
    key(GDK_KEY_F4, Gx::Ctrl, "close");
    key(GDK_KEY_Escape, Gx::Ctrl, "cancelFavorites");
    key(GDK_KEY_0, Gx::Ctrl, "fit");
    key(GDK_KEY_1, Gx::Ctrl, "actual");
    for (guint zoomIn : {GDK_KEY_plus, GDK_KEY_equal, GDK_KEY_KP_Add})
      key(zoomIn, Gx::Ctrl, "zoomIn");
    key(GDK_KEY_plus, Gx::CtrlShift, "zoomIn");
    for (guint zoomOut : {GDK_KEY_minus, GDK_KEY_KP_Subtract})
      key(zoomOut, Gx::Ctrl, "zoomOut");
    key(GDK_KEY_t, Gx::Ctrl, "newTab");
    key(GDK_KEY_Tab, Gx::Ctrl, "nextTab");
    key(GDK_KEY_Tab, Gx::CtrlShift, "previousTab");
    key(GDK_KEY_ISO_Left_Tab, Gx::CtrlShift, "previousTab");
    key(GDK_KEY_w, Gx::CtrlShift, "closeTab");
    key(GDK_KEY_u, Gx::Ctrl, "usb");
    key(GDK_KEY_p, Gx::CtrlShift, "compare");
    // Plain keys act only while a photograph has keyboard focus, so typing in
    // the path or a filter is never intercepted.
    for (auto *target : {view.get(), compareView.get()}) {
      auto *canvas = target->focusWidget();
      auto image = [&](guint keyval, GdkModifierType modifiers,
                       Gx::Callback run) {
        Gx::shortcut(canvas, keyval, modifiers, std::move(run));
      };
      image(GDK_KEY_1, Gx::NoModifier, [this] { activeView()->actual(); });
      image(GDK_KEY_KP_1, Gx::NoModifier, [this] { activeView()->actual(); });
      image(GDK_KEY_f, Gx::NoModifier, [this] { activeView()->fit(); });
      for (guint previous : {GDK_KEY_Left, GDK_KEY_Page_Up})
        image(previous, Gx::NoModifier, [this] { navigate(-1); });
      for (guint next : {GDK_KEY_Right, GDK_KEY_Page_Down})
        image(next, Gx::NoModifier, [this] { navigate(1); });
      for (guint in : {GDK_KEY_plus, GDK_KEY_equal, GDK_KEY_KP_Add})
        image(in, Gx::NoModifier, [this] { activeView()->scaleCentered(1.25); });
      image(GDK_KEY_plus, Gx::Shift,
            [this] { activeView()->scaleCentered(1.25); });
      for (guint out : {GDK_KEY_minus, GDK_KEY_KP_Subtract})
        image(out, Gx::NoModifier, [this] { activeView()->scaleCentered(0.8); });
      const struct {
        guint key;
        double dx, dy;
      } moves[] = {{GDK_KEY_Left, 64, 0},
                   {GDK_KEY_Right, -64, 0},
                   {GDK_KEY_Up, 0, 64},
                   {GDK_KEY_Down, 0, -64}};
      for (const auto &move : moves)
        for (bool large : {false, true}) {
          const QPointF delta(move.dx * (large ? 4 : 1),
                              move.dy * (large ? 4 : 1));
          image(move.key, large ? Gx::CtrlShift : Gx::Shift,
                [this, delta] { activeView()->pan(delta); });
        }
    }
  }
  void connectEngines() {
    favorites = new Favorites(this);
    QObject::connect(favorites, &Favorites::changed, this, [this] {
      setHeart(favorites->contains(activePhoto()));
    });
    QObject::connect(favorites, &Favorites::added, this,
                     [this](const QString &name) {
                       if (name == activePhoto()) {
                         setHeart(true, true);
                         setStatus(
                             "Dodano do ulubionych • ścieżka i MD5 zapisane.");
                       }
                     });
    QObject::connect(favorites, &Favorites::error, this,
                     [this](const QString &text) { setStatus(text); });
    QObject::connect(favorites, &Favorites::working, this, [this](bool busy) {
      gtk_widget_set_sensitive(heartButton, !busy && !activePhoto().isEmpty());
      gtk_widget_set_visible(cancelFavorite, busy);
    });
    QObject::connect(
        favorites, &Favorites::progress, this, [this](qint64 n, qint64 total) {
          setStatus(total < 0
                        ? QString("Ulubione: przeszukano %1 wpisów…").arg(n)
                        : QString("Ulubione · MD5: %1 / %2 MiB")
                              .arg(n / 1048576.0, 0, 'f', 1)
                              .arg(total / 1048576.0, 0, 'f', 1));
        });
    gtk_widget_set_sensitive(heartButton, FALSE);
    setHeart(false);
    favorites->reload();

    storagePool.setMaxThreadCount(1);
    storagePool.setThreadPriority(QThread::LowPriority);
    scanPool.setMaxThreadCount(1);
    scanPool.setThreadPriority(QThread::LowPriority);
    QObject::connect(&storageWatcher, &QFutureWatcher<QString>::finished, this,
                     [this] {
                       storageInFlight = false;
                       if (storageDirectory == history.current())
                         Ui::text(diskSpace, storageWatcher.result());
                       else
                         updateStorage();
                     });
    storageTimer.setInterval(30000);
    QObject::connect(&storageTimer, &QTimer::timeout, this, [this] {
      if (visibleNow())
        updateStorage();
    });
    storageTimer.start();

    maximizeTimeout.setSingleShot(true);
    maximizeTimeout.setInterval(3000);
    QObject::connect(&maximizeTimeout, &QTimer::timeout, this, [this] {
      maximizeProcess.kill();
      setStatus("Hyprland nie odpowiedział na żądanie maksymalizacji.");
    });
    QObject::connect(
        &maximizeProcess,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus) {
          maximizeTimeout.stop();
          gtk_widget_set_sensitive(maximizeButton, TRUE);
          const QString detail =
              QString::fromUtf8(maximizeProcess.readAllStandardOutput() +
                                maximizeProcess.readAllStandardError())
                  .left(512);
          if (code != 0 || detail.trimmed() != "ok")
            setStatus("Nie udało się zmienić maksymalizacji: " +
                      (detail.isEmpty() ? QString("Brak potwierdzenia Hyprland.")
                                        : detail));
        });
    QObject::connect(&maximizeProcess, &QProcess::errorOccurred, this,
                     [this](QProcess::ProcessError) {
                       maximizeTimeout.stop();
                       gtk_widget_set_sensitive(maximizeButton, TRUE);
                       setStatus("Nie można wywołać sterowania oknem Hyprland.");
                     });

    prefetch = new PreviewPrefetch(this);
    prefetchDelay.setSingleShot(true);
    prefetchDelay.setInterval(400);
    QObject::connect(&prefetchDelay, &QTimer::timeout, this,
                     [this] { prefetchNeighbors(); });
    opacitySave.setSingleShot(true);
    opacitySave.setInterval(350);
    QObject::connect(&opacitySave, &QTimer::timeout, this, [this] {
      Theme::setOpacity(Theme::opacity(), true);
      if (!Theme::opacityPersisted())
        setStatus("Nie można zapamiętać krycia interfejsu.");
    });

    view->status = [this](const QString &text) {
      if (!isShuttingDown && text.startsWith("Gotowe"))
        prefetchDelay.start();
      setStatus(text);
    };
    compareView->status = [this](const QString &text) {
      if (comparing())
        setStatus("Porównanie • " + text);
    };
    view->loadingChanged = [this](bool value) { loadingChanged(true, value); };
    compareView->loadingChanged = [this](bool value) {
      loadingChanged(false, value);
    };
    view->metadata = [this](const QString &text) {
      leftMetadata = text;
      if (activeView() == view.get())
        Ui::metadata(exifText, text);
    };
    compareView->metadata = [this](const QString &text) {
      rightMetadata = text;
      if (activeView() == compareView.get())
        Ui::metadata(exifText, text);
    };
    view->zoomChanged = [this](double value) {
      if (activeView() == view.get())
        setPercent(value);
    };
    compareView->zoomChanged = [this](double value) {
      if (activeView() == compareView.get())
        setPercent(value);
    };
    view->gpuReady = [this](const QString &gpu) {
      Ui::text(rendererLabel,
               "Renderer: " + gpu +
                   (gpu.contains("NVIDIA", Qt::CaseInsensitive)
                        ? " • RTX aktywna"
                        : " • sprawdź wybór GPU, jeśli oczekujesz RTX"));
      std::cout << "OpenGL renderer: " << gpu.toStdString() << std::endl;
    };
    view->focused = [this] { viewFocused(view.get()); };
    compareView->focused = [this] { viewFocused(compareView.get()); };
    QObject::connect(thumbnails, &ThumbnailCache::changed, this, [this] {
      Ui::tip(rendererLabel,
              QString("Miniatury JPEG: nvJPEG/CUDA %1 • CPU fallback %2 • SIMD "
                      "%3\n%4")
                  .arg(NvjpegBackend::decoded())
                  .arg(NvjpegBackend::fallbacks())
                  .arg(PixelSimd::backend())
                  .arg(NvjpegBackend::diagnostic()));
    });
    QObject::connect(&scan, &QFutureWatcher<QStringList>::finished, this,
                     [this] {
                       if (isShuttingDown)
                         return;
                       scanInFlight = false;
                       siblings = scan.result();
                       prefetchDelay.start();
                       if (!activePhoto().isEmpty() &&
                           QFileInfo(activePhoto()).absolutePath() !=
                               scanDirectory)
                         scanSiblings();
                     });
  }
  void build() {
    window = GTK_WINDOW(gtk_window_new());
    auto *w = GTK_WIDGET(window);
    Gx::addClass(w, "prism");
    gtk_window_set_default_size(window, 1200, 820);
    gtk_widget_set_size_request(w, 760, 480);
    gtk_window_set_icon_name(window, "ky-prism");
    // Client-side frame with an invisible title bar: GTK supplies shadows and
    // resize borders, the toolbar below is the visible header on every desktop.
    auto *hidden = Gx::hbox();
    gtk_widget_set_visible(hidden, FALSE);
    gtk_window_set_titlebar(window, hidden);

    root = Gx::vbox();
    Gx::addClass(root, "prism-root");
    gtk_widget_set_overflow(root, GTK_OVERFLOW_HIDDEN);
    gtk_window_set_child(window, root);
    // Focus rings appear with keyboard navigation, not on a freshly opened
    // window that the user has not touched yet.
    gtk_window_set_focus_visible(window, FALSE);
    thumbnails = new ThumbnailCache(this);
    gtk_box_append(GTK_BOX(root), buildHeader());
    gtk_box_append(GTK_BOX(root), buildToolbar());
    gtk_box_append(GTK_BOX(root), buildTabs());

    content = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(content), buildSidebar());
    gtk_paned_set_resize_start_child(GTK_PANED(content), FALSE);
    gtk_paned_set_shrink_start_child(GTK_PANED(content), FALSE);
    auto *main = Gx::hbox();
    gtk_box_append(GTK_BOX(main), buildStage());
    gtk_box_append(GTK_BOX(main), buildExif());
    gtk_paned_set_end_child(GTK_PANED(content), main);
    gtk_paned_set_resize_end_child(GTK_PANED(content), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(content), FALSE);
    gtk_paned_set_position(GTK_PANED(content), 292);
    gtk_widget_set_vexpand(content, TRUE);
    gtk_box_append(GTK_BOX(root), content);
    gtk_box_append(GTK_BOX(root), buildFooter());
    setTitles({});

    registerActions();
    connectEngines();
    registerShortcuts();

    auto *drop = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    Gx::connect(
        drop, "drop",
        +[](GtkDropTarget *, const GValue *value, double, double,
            gpointer data) -> gboolean {
          auto *files = static_cast<GSList *>(g_value_get_boxed(value));
          if (!files || !G_IS_FILE(files->data))
            return FALSE;
          g_autofree char *name = g_file_get_path(G_FILE(files->data));
          if (!name)
            return FALSE;
          static_cast<Impl *>(data)->open(QString::fromLocal8Bit(name), true);
          return TRUE;
        },
        this);
    gtk_widget_add_controller(root, GTK_EVENT_CONTROLLER(drop));

    Gx::connect(
        window, "close-request",
        +[](GtkWindow *, gpointer data) -> gboolean {
          static_cast<Impl *>(data)->q->close();
          return TRUE;
        },
        this);
    Gx::on(window, "notify::maximized", [this] {
      Icons::rename(Ui::buttonIcon(maximizeButton),
                    gtk_window_is_maximized(window) ? "restore" : "maximize");
    });
    auto visibility = [this] {
      if (isShuttingDown)
        return;
      updateThumbnailActivity();
      if (!visibleNow()) {
        prefetchDelay.stop();
        if (prefetch)
          prefetch->cancel();
      }
    };
    Gx::on(window, "notify::suspended", visibility);
    Gx::on(window, "map", visibility, true);
    Gx::on(window, "unmap", visibility, true);

    const QString pictures = QDir::homePath() + "/Pictures";
    browse(QDir(pictures).exists() ? pictures : QDir::homePath());
    updateStorage();
    gtk_window_set_focus(window, welcomeButton);
  }
  void beginShutdown() {
    if (isShuttingDown)
      return;
    isShuttingDown = true;
    // Cancel every producer BEFORE any destructor waits for a worker. No
    // completion callback may restart scans/prefetch while another pool drains.
    scanCancelled->store(true);
    scan.disconnect(this);
    storageTimer.stop();
    storageWatcher.disconnect(this);
    opacitySave.stop();
    if (libraryPane)
      libraryPane->stop();
    if (fileDialog)
      fileDialog->cancelForShutdown();
    if (usb)
      usb->shutdown();
    prefetchDelay.stop();
    if (prefetch)
      prefetch->cancel();
    if (thumbnails)
      thumbnails->setActive(false);
    heartAnimation.stop();
    languageFlip.stop();
    if (compareView)
      compareView->stop();
    if (view)
      view->stop();
    if (favorites)
      favorites->stopWork();
    maximizeTimeout.stop();
    maximizeProcess.disconnect(this);
    maximizeProcess.kill();
  }
};

PrismWindow::PrismWindow(const QString &displayProfile) : d(new Impl(this)) {
  d->profile = displayProfile;
  d->build();
}
PrismWindow::~PrismWindow() {
  closed = {};
  *d->alive = false;
  Dialogs::dismissAll(d->window);
  d->beginShutdown();
  d->favoritesDialog.reset();
  d->bookmarksDialog.reset();
  d->aboutDialog.reset();
  d->fileDialog.reset();
  d->usb.reset();
  d->prefetchDelay.stop();
  delete d->prefetch;
  d->prefetch = nullptr;
  d->libraryPane.reset();
  delete d->thumbnails;
  d->thumbnails = nullptr;
  d->maximizeProcess.waitForFinished(500);
  d->storagePool.waitForDone();
  d->scanPool.waitForDone();
  d->favorites->shutdown();
  d->compareView.reset();
  d->view.reset();
  if (d->window)
    gtk_window_destroy(d->window);
  d->window = nullptr;
}
GtkWindow *PrismWindow::window() const { return d->window; }
void PrismWindow::show(bool fullscreen) {
  if (fullscreen)
    gtk_window_fullscreen(d->window);
  gtk_window_present(d->window);
  Gx::later([this, live = d->alive] {
    if (*live && !d->isShuttingDown)
      d->updateThumbnailActivity();
  });
}
void PrismWindow::open(const QString &path) { d->open(path, true); }
void PrismWindow::browse(const QString &directory) { d->browse(directory); }
void PrismWindow::close() {
  if (d->isShuttingDown)
    return;
  // A slider change still waiting for its debounce is written now.
  if (d->opacitySave.isActive()) {
    d->opacitySave.stop();
    Theme::setOpacity(Theme::opacity(), true);
  }
  d->beginShutdown();
  Dialogs::dismissAll(d->window);
  if (d->favoritesDialog)
    d->favoritesDialog->hide();
  if (d->bookmarksDialog)
    d->bookmarksDialog->hide();
  if (d->aboutDialog)
    d->aboutDialog->hide();
  if (d->fileDialog)
    d->fileDialog->hide();
  gtk_widget_set_visible(GTK_WIDGET(d->window), FALSE);
  if (closed)
    closed();
}
bool PrismWindow::trigger(const QString &action) {
  if (d->isShuttingDown && action != "close")
    return false;
  const auto it = d->actionMap.constFind(action);
  if (it == d->actionMap.cend())
    return false;
  const auto run = *it;
  run();
  return true;
}
QStringList PrismWindow::actions() const { return d->actionMap.keys(); }
PhotoView *PrismWindow::primaryView() const { return d->view.get(); }
PhotoView *PrismWindow::secondaryView() const { return d->compareView.get(); }
LibraryPane *PrismWindow::library() const { return d->libraryPane.get(); }
QString PrismWindow::statusText() const { return Ui::sourceText(d->status); }
QString PrismWindow::titleText() const {
  return Gx::qs(gtk_window_get_title(d->window));
}
QString PrismWindow::currentPhoto() const { return d->path; }
QString PrismWindow::comparedPhoto() const { return d->comparedPath; }
QString PrismWindow::currentDirectory() const { return d->history.current(); }
QString PrismWindow::metadataText() const {
  auto *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->exifText));
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  g_autofree char *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
  return Gx::qs(text);
}
QString PrismWindow::zoomText() const {
  return Gx::qs(gtk_label_get_text(GTK_LABEL(d->percent)));
}
int PrismWindow::tabCount() const { return int(d->tabStates.size()); }
int PrismWindow::activeTab() const { return d->activeTabIndex; }
bool PrismWindow::libraryVisible() const {
  return gtk_widget_get_visible(d->sidebar);
}
bool PrismWindow::exifVisible() const {
  return gtk_widget_get_visible(d->exifPanel);
}
bool PrismWindow::compareVisible() const { return d->comparing(); }
bool PrismWindow::welcomeVisible() const { return d->welcomeShown(); }
bool PrismWindow::heartSelected() const { return d->heartIsSelected; }
bool PrismWindow::bookmarkChecked() const {
  return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->bookmarkStar));
}
bool PrismWindow::shuttingDown() const { return d->isShuttingDown; }
void PrismWindow::showComparison(const QString &path) {
  d->showComparison(path);
}
void PrismWindow::setSort(int column, bool descending) {
  if (column < 0 || column >= int(d->sortButtons.size()))
    return;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->sortDirection), descending);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->sortButtons[size_t(column)]),
                               TRUE);
}
void PrismWindow::setLibraryMode(bool thumbnails) {
  gtk_toggle_button_set_active(
      GTK_TOGGLE_BUTTON(thumbnails ? d->gridMode : d->listMode), TRUE);
}
void PrismWindow::selectTab(int index) { d->selectTab(index); }
