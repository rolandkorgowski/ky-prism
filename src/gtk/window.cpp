#include "window.h"
#include "browser_history.h"
#include "dialogs.h"
#include "document.h"
#include "favorites.h"
#include "file_ops_core.h"
#include "icons.h"
#include "language.h"
#include "library_pane.h"
#include "local_path.h"
#include "nvjpeg_backend.h"
#include "photo_view.h"
#include "print.h"
#include "pixel_simd.h"
#include "prefetch.h"
#include "settings.h"
#include "theme.h"
#include "thumbnail_cache.h"
#include "ui_text.h"
#include "window_control.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLocale>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QThreadPool>
#include <QDeadlineTimer>
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
// Clipboard copy of the displayed photograph, prepared off the main thread.
struct ClipboardImage {
  QString path, error;
  QByteArray png;
  QSize size, original;
  bool cancelled = false;
};
// Copy into a folder dropped on: the examination result.
struct DropCheck {
  QList<CopyItem> items;
  QString error;
  bool cancelled = false;
};
enum class DropSpot { None, Open, Compare, Folder };
// Files examined for the trash (Delete) before the confirmation.
struct TrashCheck {
  QList<TrashItem> items;
  QString error;
  bool cancelled = false;
};
// Larger photographs are downscaled for the clipboard: 50 Mpx is about
// 200 MB of pixels in the applications that receive it.
constexpr qint64 clipboardPixels = 50'000'000;
GBytes *bytesOf(QByteArray data) {
  auto *owner = new QByteArray(std::move(data));
  return g_bytes_new_with_free_func(
      owner->constData(), gsize(owner->size()),
      +[](gpointer p) { delete static_cast<QByteArray *>(p); }, owner);
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
  std::unique_ptr<PhotoPrint> printJob;
  GtkWidget *smoothToggle = nullptr;
  bool smoothZoom = PrismSettings::storedSmoothZoom(PrismSettings::appearanceFile());
  void setSmoothZoom(bool enabled) {
    smoothZoom = enabled;
    for (auto *v : {view.get(), compareView.get()})
      if (v)
        v->setSmoothZoom(enabled);
    if (smoothToggle && gtk_check_button_get_active(GTK_CHECK_BUTTON(smoothToggle)) != enabled) {
      syncingUi = true;
      gtk_check_button_set_active(GTK_CHECK_BUTTON(smoothToggle), enabled);
      syncingUi = false;
    }
    PrismSettings::saveSmoothZoom(PrismSettings::appearanceFile(), enabled);
    setStatus(enabled ? "Wygładzanie pikseli w powiększeniu: włączone."
                      : "Ostre piksele w powiększeniu (domyślne).");
  }
  GtkWidget *photoMenu = nullptr, *menuCopy = nullptr, *menuExport = nullptr,
            *menuPrint = nullptr, *menuHeart = nullptr, *menuTrash = nullptr,
            *libraryMenu = nullptr;
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
  // Folder whose files `siblings` lists (the last finished scan), and the one
  // being scanned now.
  QString siblingsDirectory, scanningDirectory;
  QStringList siblings;
  // ←/→ pressed while the folder is still being listed, or on a photo the
  // list does not know yet (added after the listing): the step waits for
  // the listing. `relisted` is the photo last relisted for, so a photo that
  // never appears in the list costs one extra listing, not one per key.
  int pendingStep = 0;
  QString pendingStepFrom, relisted;
  QList<TabState> tabStates;
  BrowserHistory history;
  QThreadPool storagePool, scanPool, clipboardPool, exportPool;
  QFutureWatcher<ClipboardImage> clipboardWatcher;
  QFutureWatcher<FileResult> exportWatcher;
  std::shared_ptr<std::atomic_bool> clipboardCancel =
      std::make_shared<std::atomic_bool>(true);
  std::shared_ptr<std::atomic_bool> exportCancel =
      std::make_shared<std::atomic_bool>(true);
  QString exportFolder;
  // Drag and drop onto a folder of the library: examine, ask, copy.
  QThreadPool dropPool;
  QFutureWatcher<DropCheck> dropCheck;
  QFutureWatcher<CopyReport> dropCopy;
  std::shared_ptr<std::atomic_bool> dropCancel =
      std::make_shared<std::atomic_bool>(true);
  QString dropFolder, dropSkipped; // dropSkipped: why files were left out
  bool dropAsking = false;
  // Delete: examine, confirm, move to the system trash. Runs on dropPool, so
  // only one file operation (copy into a folder or trash) runs at a time.
  QFutureWatcher<TrashCheck> trashCheck;
  QFutureWatcher<TrashReport> trashJob;
  std::shared_ptr<std::atomic_bool> trashCancel =
      std::make_shared<std::atomic_bool>(true);
  QString trashFolder, trashNext, trashNextCompared;
  // The trash report, shown again while the photo that took the place of a
  // trashed one loads (its "Gotowe" statuses would hide the report).
  QString trashNotice;
  QDeadlineTimer trashNoticeUntil;
  int trashFolders = 0, trashBlocked = 0;
  bool trashAsking = false;
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
  // focusPhoto: the chooser, a drop, favourites, the command line and a mouse
  // click on a file in the library hand the keyboard to the photograph;
  // Enter in the library list keeps it in the list.
  void open(const QString &name, bool focusPhoto = false) {
    if (isShuttingDown)
      return;
    trashNotice.clear();
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
    trashNotice.clear();
    if (!validLocalPath(name) || !QFileInfo(name).isFile())
      return;
    const bool startingPair = !gtk_widget_get_visible(comparePanel) ||
                              !gtk_widget_get_visible(primaryPanel);
    comparedPath = QDir::cleanPath(QFileInfo(name).absoluteFilePath());
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
    const QString current = activePhoto();
    const int i = siblings.indexOf(current);
    if (delta == 0 || current.isEmpty())
      return;
    if (i < 0) {
      if (!QFileInfo(current).isFile())
        return;
      pendingStep = delta;
      pendingStepFrom = current;
      if (!scanInFlight && relisted != current) {
        relisted = current;
        scanDirectory.clear();
        scanSiblings();
      }
      if (!scanInFlight)
        pendingStep = 0;
      return;
    }
    // A photo moved to the trash (or removed elsewhere) is passed over.
    for (int j = i + delta; j >= 0 && j < siblings.size(); j += delta) {
      const auto next = siblings.at(j);
      if (!QFileInfo(next).isFile())
        continue;
      if (activeView() == compareView.get())
        showComparison(next);
      else
        open(next);
      return;
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
    scanningDirectory = dir;
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
  // ------------------------------------------------- clipboard / export ---
  // The clipboard offers the photograph twice: as a PNG image (editors,
  // browsers, messengers) and as the file itself (file managers paste a copy).
  void copyToClipboard() {
    if (isShuttingDown)
      return;
    const QString photo = activePhoto();
    if (photo.isEmpty()) {
      setStatus("Najpierw otwórz zdjęcie.");
      return;
    }
    clipboardCancel->store(true);
    auto token = std::make_shared<std::atomic_bool>(false);
    clipboardCancel = token;
    setStatus(QString("Kopiowanie do schowka: %1…").arg(QFileInfo(photo).fileName()));
    clipboardWatcher.setFuture(QtConcurrent::run(&clipboardPool, [photo, token] {
      ClipboardImage result;
      result.path = photo;
      try {
        // Same decoder as the canvas (EXIF orientation, ICC), sRGB output.
        Document document;
        result.original = document.open(photo, {}, token, {}, 256).size;
        result.png = document.encodePng(clipboardPixels, &result.size);
      } catch (const std::exception &e) {
        result.error = QString::fromUtf8(e.what());
      }
      result.cancelled = token->load();
      return result;
    }));
  }
  void clipboardReady() {
    if (isShuttingDown)
      return;
    auto result = clipboardWatcher.result();
    if (result.cancelled)
      return;
    const QString name = QFileInfo(result.path).fileName();
    if (!result.error.isEmpty() || result.png.isEmpty()) {
      setStatus(QString("Nie skopiowano do schowka: %1").arg(result.error));
      return;
    }
    g_autoptr(GFile) file = g_file_new_for_path(Gx::u8(result.path).constData());
    g_autofree char *uri = g_file_get_uri(file);
    GBytes *image = bytesOf(std::move(result.png));
    GBytes *uris = bytesOf(QByteArray(uri) + "\r\n");
    GBytes *gnome = bytesOf("copy\n" + QByteArray(uri));
    GdkContentProvider *providers[] = {
        gdk_content_provider_new_for_bytes("image/png", image),
        gdk_content_provider_new_for_bytes("text/uri-list", uris),
        gdk_content_provider_new_for_bytes("x-special/gnome-copied-files", gnome)};
    g_bytes_unref(image);
    g_bytes_unref(uris);
    g_bytes_unref(gnome);
    auto *content = gdk_content_provider_new_union(providers, 3);
    gdk_clipboard_set_content(gtk_widget_get_clipboard(GTK_WIDGET(window)),
                              content);
    g_object_unref(content);
    if (result.size != result.original)
      setStatus(QString("Skopiowano do schowka: %1 (%2 × %3 px, zmniejszone z "
                        "%4 × %5 px)")
                    .arg(name)
                    .arg(result.size.width())
                    .arg(result.size.height())
                    .arg(result.original.width())
                    .arg(result.original.height()));
    else
      setStatus(QString("Skopiowano do schowka: %1 (%2 × %3 px)")
                    .arg(name)
                    .arg(result.size.width())
                    .arg(result.size.height()));
  }
  // Export = a byte-identical copy (EXIF included) under a chosen name.
  void exportPhoto() {
    if (isShuttingDown)
      return;
    const QString photo = activePhoto();
    if (photo.isEmpty()) {
      setStatus("Najpierw otwórz zdjęcie.");
      return;
    }
    if (exportWatcher.isRunning()) {
      setStatus("Eksport już trwa.");
      return;
    }
    const QFileInfo info(photo);
    const QString folder =
        QFileInfo(exportFolder).isDir() ? exportFolder : info.absolutePath();
    Dialogs::chooseSaveFile(window, "Eksportuj kopię zdjęcia", folder,
                            info.fileName(),
                            [this, photo, live = alive](QString target) {
                              if (*live && !target.isEmpty() && !isShuttingDown)
                                exportTo(photo, target);
                            });
  }
  void exportTo(const QString &photo, QString target) {
    if (isShuttingDown || photo.isEmpty() || exportWatcher.isRunning())
      return;
    const QString suffix = QFileInfo(photo).suffix();
    if (QFileInfo(target).suffix().isEmpty() && !suffix.isEmpty())
      target += "." + suffix;
    exportFolder = QFileInfo(target).absolutePath();
    const QFileInfo existing(target);
    if (existing.exists() || existing.isSymLink()) {
      Dialogs::message(window, "Plik już istnieje",
                       "ky-prism nie nadpisuje istniejących plików.", target,
                       {"&Anuluj", "Wybierz &inną nazwę"}, 1, 0,
                       [this, live = alive](int choice) {
                         if (*live && choice == 1 && !isShuttingDown)
                           exportPhoto();
                       });
      setStatus(QString("Nie wyeksportowano: %1 już istnieje.").arg(target));
      return;
    }
    exportCancel->store(true);
    auto token = std::make_shared<std::atomic_bool>(false);
    exportCancel = token;
    setStatus(QString("Eksport: %1…").arg(target));
    auto report = [this, token](qint64 done, qint64 total) {
      QMetaObject::invokeMethod(
          this,
          [this, token, done, total] {
            if (token == exportCancel && !token->load() && !isShuttingDown &&
                total > 0)
              setStatus(QString("Eksport: %1 / %2 MiB")
                            .arg(done / 1048576.0, 0, 'f', 1)
                            .arg(total / 1048576.0, 0, 'f', 1));
          },
          Qt::QueuedConnection);
    };
    exportWatcher.setFuture(
        QtConcurrent::run(&exportPool, [photo, target, token, report] {
          try {
            FileOps::copy(photo, target, token, report);
            return FileResult{{}, {}, target, false};
          } catch (const std::exception &e) {
            return FileResult{{}, QString::fromUtf8(e.what()), target,
                              token->load()};
          }
        }));
  }
  void exportFinished() {
    if (isShuttingDown)
      return;
    const auto result = exportWatcher.result();
    if (result.cancelled)
      setStatus("Eksport anulowany.");
    else if (!result.error.isEmpty())
      setStatus(QString("Eksport nieudany: %1").arg(result.error));
    else
      setStatus(QString("Wyeksportowano kopię: %1").arg(result.message));
  }
  // Printing: the photos selected in the library, else the displayed one.
  QStringList photosToPrint() const {
    QStringList result;
    for (const auto &path : selectedPhotos())
      if (QFileInfo(path).isFile())
        result.append(path);
    return result;
  }
  bool startPrint(const QString &pdf = {}, const PrintOptions *options = nullptr) {
    if (isShuttingDown)
      return false;
    if (printJob && printJob->running()) {
      setStatus("Drukowanie już trwa.");
      return false;
    }
    const auto photos = photosToPrint();
    if (photos.isEmpty()) {
      setStatus("Najpierw otwórz zdjęcie.");
      return false;
    }
    printJob = std::make_unique<PhotoPrint>(window, photos);
    printJob->status = [this, live = alive](const QString &text) {
      if (*live && !isShuttingDown)
        setStatus(text);
    };
    printJob->finished = [this, live = alive] {
      if (*live)
        Gx::later([this, live] {
          if (*live && printJob && !printJob->running())
            printJob.reset();
        });
    };
    const int count = int(photos.size());
    const int last = count % 10, lastTwo = count % 100;
    setStatus(count == 1 ? QString("Drukowanie: %1").arg(QFileInfo(photos.first()).fileName())
              : last >= 2 && last <= 4 && !(lastTwo >= 12 && lastTwo <= 14)
                  ? QString("Drukowanie: %1 zdjęcia").arg(count)
                  : QString("Drukowanie: %1 zdjęć").arg(count));
    if (pdf.isEmpty())
      printJob->run();
    else
      printJob->exportPdf(pdf, options ? *options : PrintOptions());
    return true;
  }
  // Right-click menu on a photograph.
  void buildPhotoMenu() {
    photoMenu = gtk_popover_new();
    Gx::addClass(photoMenu, "prism-popover");
    Gx::addClass(photoMenu, "prism-menu");
    gtk_popover_set_has_arrow(GTK_POPOVER(photoMenu), FALSE);
    gtk_popover_set_position(GTK_POPOVER(photoMenu), GTK_POS_BOTTOM);
    gtk_widget_set_halign(photoMenu, GTK_ALIGN_START);
    auto *box = Gx::vbox(2);
    auto item = [&](const QString &symbol, const QString &caption,
                    const QString &tip, const QString &action) {
      auto *button = Ui::button(symbol, caption, tip, [this, action] {
        gtk_popover_popdown(GTK_POPOVER(photoMenu));
        Gx::later([this, action, live = alive] {
          if (*live && !isShuttingDown)
            q->trigger(action);
        });
      });
      Gx::addClass(button, "flat");
      if (auto *child = gtk_button_get_child(GTK_BUTTON(button)))
        gtk_widget_set_halign(child, GTK_ALIGN_START);
      gtk_box_append(GTK_BOX(box), button);
      return button;
    };
    menuCopy = item("clipboard", "Kopiuj do schowka", "Ctrl+C", "copyImage");
    menuExport = item("export", "Eksportuj kopię pliku…", "Ctrl+E", "export");
    menuPrint = item("print", "Drukuj…", "Ctrl+P", "print");
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    menuHeart = item("heart", "Dodaj do ulubionych", "Ctrl+Shift+H", "heart");
    item("exif", "Informacje EXIF", "Ctrl+I", "exif");
    item("fullscreen", "Pełny ekran", "F11", "fullscreen");
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    menuTrash = item("trash", "Przenieś do kosza…", "Delete", "trashPhoto");
    gtk_popover_set_child(GTK_POPOVER(photoMenu), box);
    gtk_widget_set_parent(photoMenu, root);
  }
  // Right-click menu on a file of the library: acts on the selection.
  void buildLibraryMenu() {
    libraryMenu = gtk_popover_new();
    Gx::addClass(libraryMenu, "prism-popover");
    Gx::addClass(libraryMenu, "prism-menu");
    gtk_popover_set_has_arrow(GTK_POPOVER(libraryMenu), FALSE);
    gtk_popover_set_position(GTK_POPOVER(libraryMenu), GTK_POS_BOTTOM);
    gtk_widget_set_halign(libraryMenu, GTK_ALIGN_START);
    auto *box = Gx::vbox(2);
    auto *button = Ui::button("trash", "Przenieś do kosza…", "Delete", [this] {
      gtk_popover_popdown(GTK_POPOVER(libraryMenu));
      Gx::later([this, live = alive] {
        if (*live && !isShuttingDown)
          q->trigger("trashSelection");
      });
    });
    Gx::addClass(button, "flat");
    if (auto *child = gtk_button_get_child(GTK_BUTTON(button)))
      gtk_widget_set_halign(child, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), button);
    gtk_popover_set_child(GTK_POPOVER(libraryMenu), box);
    gtk_widget_set_parent(libraryMenu, root);
  }
  // (x, y) in the coordinates of the library's visible view.
  void showLibraryMenu(double x, double y) {
    if (isShuttingDown || !libraryMenu || !libraryPane)
      return;
    graphene_point_t point;
    const graphene_point_t local = Gx::point(float(x), float(y));
    if (!gtk_widget_compute_point(libraryPane->focusWidget(), root, &local,
                                  &point))
      return;
    const GdkRectangle at{int(point.x), int(point.y), 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(libraryMenu), &at);
    gtk_popover_popup(GTK_POPOVER(libraryMenu));
  }
  void showPhotoMenu(PhotoView *target, double x, double y) {
    if (isShuttingDown || !photoMenu)
      return;
    viewFocused(target);
    const bool photo = !activePhoto().isEmpty();
    gtk_widget_set_sensitive(menuCopy, photo);
    gtk_widget_set_sensitive(menuExport, photo);
    gtk_widget_set_sensitive(menuPrint, photo);
    gtk_widget_set_sensitive(menuHeart, photo && !favorites->busy());
    gtk_widget_set_sensitive(menuTrash, photo);
    Ui::text(Ui::buttonLabel(menuHeart), heartIsSelected ? "Usuń z ulubionych"
                                                         : "Dodaj do ulubionych");
    graphene_point_t point;
    const graphene_point_t local = Gx::point(float(x), float(y));
    if (!gtk_widget_compute_point(target->widget(), root, &local, &point))
      return;
    const GdkRectangle at{int(point.x), int(point.y), 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(photoMenu), &at);
    gtk_popover_popup(GTK_POPOVER(photoMenu));
  }
  // Keyboard (Menu key, Shift+F10): the menu opens near the photo's centre.
  void showPhotoMenuFromKeyboard() {
    auto *target = activeView();
    if (!target || welcomeShown())
      return;
    showPhotoMenu(target, gtk_widget_get_width(target->widget()) / 2.0,
                  gtk_widget_get_height(target->widget()) / 3.0);
  }
  void attachPhotoMenu(PhotoView *target) {
    auto *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
    // Capture phase: the right button reaches the menu before any control on
    // or inside the canvas can consume the press.
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click),
                                               GTK_PHASE_CAPTURE);
    struct Context {
      Impl *self;
      PhotoView *view;
    };
    g_signal_connect_data(
        click, "pressed",
        reinterpret_cast<GCallback>(
            +[](GtkGestureClick *gesture, int, double x, double y,
                gpointer data) {
              gtk_gesture_set_state(GTK_GESTURE(gesture),
                                    GTK_EVENT_SEQUENCE_CLAIMED);
              auto *context = static_cast<Context *>(data);
              context->self->showPhotoMenu(context->view, x, y);
            }),
        new Context{this, target},
        +[](gpointer data, GClosure *) { delete static_cast<Context *>(data); },
        GConnectFlags(0));
    gtk_widget_add_controller(target->widget(), GTK_EVENT_CONTROLLER(click));
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
    auto *defaults = Ui::button({}, "Domyślne", {}, [this] {
                     setOpacity(PrismSettings::defaultOpacity(
                                    Theme::compositorBlurs()),
                                false);
                   });
    gtk_box_append(GTK_BOX(row), defaults);
    gtk_box_append(GTK_BOX(row), Ui::button({}, "W pełni kryjący", {},
                                            [this] { setOpacity(100, false); }));
    gtk_box_append(GTK_BOX(box), row);
    // Photo magnification: sharp pixels by default, smoothing on request.
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), Ui::label("Powiększenie zdjęcia", "prism-title"));
    smoothToggle = gtk_check_button_new();
    gtk_check_button_set_child(GTK_CHECK_BUTTON(smoothToggle),
                               Ui::label("Wygładzaj piksele w powiększeniu"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(smoothToggle), smoothZoom);
    Gx::on(smoothToggle, "toggled", [this] {
      if (!syncingUi)
        setSmoothZoom(gtk_check_button_get_active(GTK_CHECK_BUTTON(smoothToggle)));
    });
    gtk_box_append(GTK_BOX(box), smoothToggle);
    gtk_box_append(GTK_BOX(box),
                   Ui::label("Wyłączone (domyślnie): od 100 % każdy piksel zdjęcia jest\n"
                             "ostrym kwadratem. Włączone: przejścia między pikselami\n"
                             "są wygładzane. Same piksele zdjęcia się nie zmieniają.",
                             "prism-dim"));
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
    Ui::tip(opacityButton, "Wygląd: krycie interfejsu i wygładzanie powiększenia • Ctrl+Alt+O");
    Ui::accessible(opacityButton, "Wygląd: krycie interfejsu i powiększenie");
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
    auto *clipboard = tool(work, "clipboard", "",
                           "Kopiuj zdjęcie do schowka • Ctrl+C", "copyImage");
    Ui::accessible(clipboard, "Kopiuj zdjęcie do schowka");
    auto *exportButton =
        tool(work, "export", "",
             "Eksportuj kopię pliku — wybierz miejsce i nazwę • Ctrl+E",
             "export");
    Ui::accessible(exportButton, "Eksportuj kopię pliku");
    auto *printButton = tool(
        work, "print", "",
        "Drukuj — drukarka, papier, rozmiar, kolor i jakość • Ctrl+P", "print");
    Ui::accessible(printButton, "Drukuj zdjęcia");
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
    // A mouse click on a file hands the keyboard to the photo, so ←/→ go to
    // the neighbours at once (the list itself swallows ←/→). Enter keeps
    // the keyboard in the list.
    libraryPane->fileClicked = [this](const QString &name) { open(name, true); };
    libraryPane->deleteRequested = [this] { trashSelection(); };
    libraryPane->menuRequested = [this](double x, double y) {
      showLibraryMenu(x, y);
    };
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
    attachPhotoMenu(view.get());
    attachPhotoMenu(compareView.get());
    view->setSmoothZoom(smoothZoom);
    compareView->setSmoothZoom(smoothZoom);
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
    a["copyImage"] = [this] { copyToClipboard(); };
    a["export"] = [this] { exportPhoto(); };
    a["print"] = [this] { startPrint(); };
    a["smoothZoom"] = [this] { setSmoothZoom(!smoothZoom); };
    a["photoMenu"] = [this] { showPhotoMenuFromKeyboard(); };
    a["duplicates"] = [this] { showFileDialog(true); };
    a["trashPhoto"] = [this] { trashPhoto(); };
    a["trashSelection"] = [this] { trashSelection(); };
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
    // Bubble phase: a focused text field or selectable text copies its own
    // selection first; elsewhere Ctrl+C copies the displayed photograph.
    key(GDK_KEY_c, Gx::Ctrl, "copyImage", false);
    key(GDK_KEY_e, Gx::Ctrl, "export");
    key(GDK_KEY_p, Gx::Ctrl, "print");
    // Bubble phase: text fields keep their own context menu on these keys.
    key(GDK_KEY_Menu, Gx::NoModifier, "photoMenu", false);
    key(GDK_KEY_F10, Gx::Shift, "photoMenu", false);
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
      for (guint remove : {GDK_KEY_Delete, GDK_KEY_KP_Delete})
        image(remove, Gx::NoModifier, [this, target] { trashPhoto(target); });
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
    clipboardPool.setMaxThreadCount(1);
    exportPool.setMaxThreadCount(1);
    dropPool.setMaxThreadCount(1);
    QObject::connect(&dropCheck, &QFutureWatcher<DropCheck>::finished, this,
                     [this] { dropChecked(); });
    QObject::connect(&dropCopy, &QFutureWatcher<CopyReport>::finished, this,
                     [this] { dropCopied(); });
    QObject::connect(&trashCheck, &QFutureWatcher<TrashCheck>::finished, this,
                     [this] { trashChecked(); });
    QObject::connect(&trashJob, &QFutureWatcher<TrashReport>::finished, this,
                     [this] { trashFinished(); });
    QObject::connect(&clipboardWatcher,
                     &QFutureWatcher<ClipboardImage>::finished, this,
                     [this] { clipboardReady(); });
    QObject::connect(&exportWatcher, &QFutureWatcher<FileResult>::finished,
                     this, [this] { exportFinished(); });
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
      if (restoreTrashNotice(text))
        return;
      setStatus(text);
    };
    compareView->status = [this](const QString &text) {
      if (!comparing())
        return;
      if (restoreTrashNotice(text))
        return;
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
                       siblingsDirectory = scanningDirectory;
                       prefetchDelay.start();
                       if (!activePhoto().isEmpty() &&
                           QFileInfo(activePhoto()).absolutePath() !=
                               scanDirectory)
                         scanSiblings();
                       if (scanInFlight)
                         return;
                       // A step pressed during the listing, for the photo
                       // still shown.
                       const int step = pendingStep;
                       pendingStep = 0;
                       if (step && pendingStepFrom == activePhoto() &&
                           siblings.contains(activePhoto()))
                         navigate(step);
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
    buildPhotoMenu();
    buildLibraryMenu();

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

    // Files dropped on the window: open them, compare them, or (dragged
    // from the library onto one of its folders) copy them there.
    auto *drop = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    auto hover = +[](GtkDropTarget *target, double x, double y,
                     gpointer data) -> GdkDragAction {
      auto *self = static_cast<Impl *>(data);
      QString folder;
      const auto spot = self->dropSpot(x, y, fromLibrary(target), &folder);
      if (spot == DropSpot::Folder) {
        graphene_point_t in = Gx::point(float(x), float(y)), out;
        if (gtk_widget_compute_point(self->root,
                                     self->libraryPane->focusWidget(), &in,
                                     &out))
          self->libraryPane->showDropFolder(out.x, out.y);
      } else
        self->libraryPane->clearDropFolder();
      return spot == DropSpot::None ? GdkDragAction(0) : GDK_ACTION_COPY;
    };
    Gx::connect(drop, "enter", hover, this);
    Gx::connect(drop, "motion", hover, this);
    Gx::connect(
        drop, "leave",
        +[](GtkDropTarget *, gpointer data) {
          static_cast<Impl *>(data)->libraryPane->clearDropFolder();
        },
        this);
    Gx::connect(
        drop, "drop",
        +[](GtkDropTarget *target, const GValue *value, double x, double y,
            gpointer data) -> gboolean {
          auto *self = static_cast<Impl *>(data);
          self->libraryPane->clearDropFolder();
          auto *list = static_cast<GdkFileList *>(g_value_get_boxed(value));
          if (!list)
            return FALSE;
          QStringList paths;
          GSList *files = gdk_file_list_get_files(list);
          for (auto *l = files; l; l = l->next) {
            if (!G_IS_FILE(l->data))
              continue;
            g_autofree char *name = g_file_get_path(G_FILE(l->data));
            if (name)
              paths.append(QFile::decodeName(name));
          }
          g_slist_free(files);
          return self->dropFiles(paths, x, y, fromLibrary(target));
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
  // ------------------------------------------------------ drag and drop ---
  static bool fromLibrary(GtkDropTarget *target) {
    auto *current = gtk_drop_target_get_current_drop(target);
    return current && LibraryPane::ownsDrag(gdk_drop_get_drag(current));
  }
  // What dropping at (x, y) of the window does. Photos dragged from the
  // library are copied only onto one of its folders; dropped anywhere else
  // on the list they do nothing. Folders of other programs' drops are not
  // copy targets: such drops open the file, as before.
  DropSpot dropSpot(double x, double y, bool library, QString *folder) const {
    auto *hit = gtk_widget_pick(root, x, y, GTK_PICK_DEFAULT);
    auto inside = [hit](GtkWidget *area) {
      return hit && area && (hit == area || gtk_widget_is_ancestor(hit, area));
    };
    if (libraryPane && inside(libraryPane->widget())) {
      if (!library)
        return DropSpot::Open;
      graphene_point_t in = Gx::point(float(x), float(y)), out;
      bool isDirectory = false;
      QString path;
      if (gtk_widget_compute_point(root, libraryPane->focusWidget(), &in, &out))
        path = libraryPane->pathAt(out.x, out.y, &isDirectory);
      if (path.isEmpty() || !isDirectory)
        return DropSpot::None;
      if (folder)
        *folder = path;
      return DropSpot::Folder;
    }
    if (comparing() && inside(comparePanel))
      return DropSpot::Compare;
    return DropSpot::Open;
  }
  bool dropFiles(const QStringList &files, double x, double y, bool library) {
    if (isShuttingDown || files.isEmpty())
      return false;
    QString folder;
    switch (dropSpot(x, y, library, &folder)) {
    case DropSpot::None:
      return false;
    case DropSpot::Folder:
      copyToFolder(files, folder);
      return true;
    case DropSpot::Compare:
      if (QFileInfo(files.first()).isFile()) {
        showComparison(files.first());
        return true;
      }
      break;
    case DropSpot::Open:
      break;
    }
    open(files.first(), true);
    return true;
  }
  bool dropBusy() const {
    return dropCheck.isRunning() || dropCopy.isRunning() || dropAsking;
  }
  QString dropFolderName() const { return QFileInfo(dropFolder).fileName(); }
  // Step 1: examine every file in the background. Nothing is written yet.
  void copyToFolder(const QStringList &files, const QString &folder) {
    if (isShuttingDown)
      return;
    if (dropBusy()) {
      setStatus("Poczekaj: poprzednie kopiowanie do folderu jeszcze trwa.");
      return;
    }
    if (trashBusy()) {
      setStatus("Poczekaj: przenoszenie do kosza jeszcze trwa.");
      return;
    }
    if (!validLocalPath(folder) || !QFileInfo(folder).isDir())
      return;
    dropCancel->store(true);
    auto token = std::make_shared<std::atomic_bool>(false);
    dropCancel = token;
    dropFolder = QFileInfo(folder).absoluteFilePath();
    setStatus(QString("Sprawdzam pliki w folderze „%1”…").arg(dropFolderName()));
    const QString target = dropFolder;
    dropCheck.setFuture(QtConcurrent::run(&dropPool, [files, target, token] {
      DropCheck result;
      try {
        result.items = FileOps::examineCopy(files, target, token);
      } catch (const std::exception &e) {
        result.error = QString::fromUtf8(e.what());
        result.cancelled = token->load();
      }
      return result;
    }));
  }
  // Step 2: names that already exist are never overwritten without an
  // explicit answer.
  void dropChecked() {
    if (isShuttingDown)
      return;
    const auto result = dropCheck.result();
    if (result.cancelled) {
      setStatus("Kopiowanie anulowane. Nic nie zmieniono.");
      return;
    }
    if (!result.error.isEmpty()) {
      setStatus(Ui::tr("Nie skopiowano: %1").arg(Ui::tr(result.error)));
      return;
    }
    // Only different files of the same name are a question: identical ones
    // would not change, links, folders and special files are never touched.
    int fresh = 0, existing = 0, identical = 0, blocked = 0;
    dropSkipped.clear();
    for (const auto &item : result.items) {
      fresh += item.state == CopyItem::Fresh;
      existing += item.state == CopyItem::Exists;
      identical += item.state == CopyItem::Identical;
      if (item.state == CopyItem::Blocked && blocked++ == 0)
        dropSkipped = Ui::tr("%1 — pominięty: %2")
                          .arg(QFileInfo(item.target).fileName(),
                               Ui::tr(item.note));
    }
    if (blocked > 1)
      dropSkipped += " " + Ui::tr(Ui::counted(blocked - 1, "…i jeszcze %1 plik",
                                              "…i jeszcze %1 pliki",
                                              "…i jeszcze %1 plików"));
    if (existing)
      askOverwrite(result.items, fresh, existing);
    else if (identical)
      tellIdentical(result.items, fresh, identical);
    else if (fresh)
      startDropCopy(result.items, false);
    else
      dropCopiedReport(FileOps::copyItems(result.items, false, dropCancel));
  }
  static QString fileStamp(qint64 size, qint64 modified) {
    return QString("%1, %2").arg(
        QLocale().formattedDataSize(size, 1),
        QLocale().toString(QDateTime::fromMSecsSinceEpoch(modified),
                           QLocale::ShortFormat));
  }
  static QString dropDetails(const QList<CopyItem> &items) {
    // Files that would change first, then the ones that stay as they are.
    QStringList replaced, same, skipped;
    for (const auto &item : items) {
      const QString file = QFileInfo(item.target).fileName();
      if (item.state == CopyItem::Exists)
        replaced << Ui::tr("%1 — w folderze: %2; przeciągany: %3")
                        .arg(file,
                             fileStamp(item.targetSize, item.targetModified),
                             fileStamp(item.sourceSize, item.sourceModified));
      else if (item.state == CopyItem::Identical)
        same << Ui::tr("%1 — identyczna zawartość, nic się nie zmieni").arg(file);
      else if (item.state == CopyItem::Blocked)
        skipped << Ui::tr("%1 — pominięty: %2").arg(file, Ui::tr(item.note));
    }
    const QStringList all = replaced + same + skipped;
    QStringList lines = all.mid(0, 12);
    const int hidden = int(all.size() - lines.size());
    if (hidden)
      lines.append(Ui::tr(Ui::counted(hidden, "…i jeszcze %1 plik",
                                      "…i jeszcze %1 pliki",
                                      "…i jeszcze %1 plików")));
    return lines.join('\n');
  }
  // The same photo is already in the folder: say so, although nothing would
  // change. There is nothing to overwrite, so no overwrite button.
  void tellIdentical(const QList<CopyItem> &items, int fresh, int identical) {
    const QString name = dropFolderName();
    QString text =
        identical == 1
            ? Ui::tr("W folderze „%1” jest już ten sam plik, identyczny co do "
                     "bajtu. Nie trzeba go nadpisywać.")
                  .arg(name)
            : Ui::tr(Ui::counted(identical, // n ≥ 2: the first form is never used
                                 "W folderze „%2” są już te same %1 pliki, "
                                 "identyczne co do bajtu. Nie trzeba ich "
                                 "nadpisywać.",
                                 "W folderze „%2” są już te same %1 pliki, "
                                 "identyczne co do bajtu. Nie trzeba ich "
                                 "nadpisywać.",
                                 "W folderze „%2” jest już tych samych %1 "
                                 "plików, identycznych co do bajtu. Nie trzeba "
                                 "ich nadpisywać."))
                  .arg(name);
    if (fresh)
      text += " " + Ui::tr(Ui::counted(fresh, "Pozostały %1 plik można skopiować.",
                                       "Pozostałe %1 pliki można skopiować.",
                                       "Pozostałe %1 plików można skopiować."));
    const QStringList buttons = fresh ? QStringList{"&Anuluj", "&Kopiuj pozostałe"}
                                      : QStringList{"&Zamknij"};
    dropAsking = true;
    Dialogs::message(
        window, identical == 1 ? "Plik już istnieje" : "Pliki już istnieją",
        text, dropDetails(items), buttons, fresh ? 1 : 0, 0,
        [this, live = alive, items, fresh](int choice) {
          if (!*live)
            return;
          dropAsking = false;
          if (isShuttingDown)
            return;
          if (fresh && choice == 1)
            startDropCopy(items, false);
          else if (fresh)
            setStatus("Kopiowanie anulowane. Nic nie zmieniono.");
          else
            dropCopiedReport(FileOps::copyItems(items, false, dropCancel));
        });
  }
  void askOverwrite(const QList<CopyItem> &items, int fresh, int existing) {
    const QString name = dropFolderName();
    const QString text =
        existing == 1
            ? QString("W folderze „%1” jest już plik o tej nazwie. Nadpisać "
                      "go kopią? Bez Twojej zgody nic nie zostanie nadpisane.")
                  .arg(name)
            : Ui::counted(existing, // n ≥ 2: the first form is never used
                          "W folderze „%2” są już %1 pliki o tych samych "
                          "nazwach. Nadpisać je kopiami? Bez Twojej zgody nic "
                          "nie zostanie nadpisane.",
                          "W folderze „%2” są już %1 pliki o tych samych "
                          "nazwach. Nadpisać je kopiami? Bez Twojej zgody nic "
                          "nie zostanie nadpisane.",
                          "W folderze „%2” jest już %1 plików o tych samych "
                          "nazwach. Nadpisać je kopiami? Bez Twojej zgody nic "
                          "nie zostanie nadpisane.")
                  .arg(name);
    const QString overwrite =
        existing == 1 ? QString("&Nadpisz")
                      : Ui::counted(existing, "&Nadpisz %1 pliki",
                                    "&Nadpisz %1 pliki", "&Nadpisz %1 plików");
    // Without new names "skip" would copy nothing: only cancel or overwrite.
    const bool canSkip = fresh > 0;
    const QStringList buttons =
        canSkip ? QStringList{"&Anuluj", "&Pomiń istniejące", overwrite}
                : QStringList{"&Anuluj", overwrite};
    dropAsking = true;
    Dialogs::message(
        window, existing == 1 ? "Plik już istnieje" : "Pliki już istnieją",
        text, dropDetails(items), buttons, canSkip ? 1 : 0, 0,
        [this, live = alive, items, canSkip](int choice) {
          if (!*live)
            return;
          dropAsking = false;
          if (isShuttingDown)
            return;
          const int overwriteButton = canSkip ? 2 : 1;
          if (choice == overwriteButton)
            startDropCopy(items, true);
          else if (canSkip && choice == 1)
            startDropCopy(items, false);
          else
            setStatus("Kopiowanie anulowane. Nic nie zmieniono.");
        });
  }
  // Step 3: copy in the background (new names never replace anything;
  // replacements are atomic and only of the files that were examined).
  void startDropCopy(const QList<CopyItem> &items, bool overwrite) {
    if (isShuttingDown || dropCheck.isRunning() || dropCopy.isRunning())
      return;
    dropCancel->store(true);
    auto token = std::make_shared<std::atomic_bool>(false);
    dropCancel = token;
    const QString name = dropFolderName();
    setStatus(QString("Kopiuję do „%1”…").arg(name));
    auto report = [this, token, name](qint64 done, qint64 total) {
      QMetaObject::invokeMethod(
          this,
          [this, token, name, done, total] {
            if (token == dropCancel && !token->load() && !isShuttingDown &&
                total > 0)
              setStatus(QString("Kopiuję do „%1”: %2 / %3 MiB")
                            .arg(name, QString::number(done / 1048576.0, 'f', 1),
                                 QString::number(total / 1048576.0, 'f', 1)));
          },
          Qt::QueuedConnection);
    };
    dropCopy.setFuture(
        QtConcurrent::run(&dropPool, [items, overwrite, token, report] {
          return FileOps::copyItems(items, overwrite, token, report);
        }));
  }
  void dropCopied() {
    if (!isShuttingDown)
      dropCopiedReport(dropCopy.result());
  }
  void dropCopiedReport(const CopyReport &r) {
    QStringList parts;
    if (r.copied)
      parts << Ui::tr(Ui::counted(r.copied, "%1 nowy plik", "%1 nowe pliki",
                                  "%1 nowych plików"));
    if (r.replaced)
      parts << Ui::tr(Ui::counted(r.replaced, "%1 nadpisany", "%1 nadpisane",
                                  "%1 nadpisanych"));
    if (r.unchanged)
      parts << Ui::tr(Ui::counted(r.unchanged, "%1 bez zmian (ten sam plik)",
                                  "%1 bez zmian (te same pliki)",
                                  "%1 bez zmian (te same pliki)"));
    if (r.skipped)
      parts << Ui::tr(Ui::counted(r.skipped, "%1 pominięty", "%1 pominięte",
                                  "%1 pominiętych"));
    QString done =
        parts.isEmpty() ? Ui::tr("nic nie zapisano") : parts.join(", ");
    if (!dropSkipped.isEmpty())
      done += " (" + dropSkipped + ")";
    const QString name = dropFolderName();
    if (r.cancelled)
      setStatus(Ui::tr("Przerwano kopiowanie do „%1”. Wcześniej: %2.")
                    .arg(name, done));
    else if (!r.error.isEmpty())
      setStatus(Ui::tr("Przerwano kopiowanie do „%1” na pliku %2: %3 "
                       "Wcześniej: %4.")
                    .arg(name, r.failedFile, Ui::tr(r.error), done));
    else if (r.copied + r.replaced == 0)
      setStatus(Ui::tr("Nic nie skopiowano do „%1”: %2.").arg(name, done));
    else
      setStatus(Ui::tr("Skopiowano do „%1”: %2.").arg(name, done));
  }

  // ------------------------------------------------------------ trash ---
  // Delete never removes a file permanently: the files the user chose are
  // examined, listed in a question whose default answer is Anuluj, and only
  // then moved to the system trash, where they can be restored.
  bool trashBusy() const {
    return trashCheck.isRunning() || trashJob.isRunning() || trashAsking;
  }
  // Delete in the library (or its menu): the selected files, never folders.
  void trashSelection() {
    if (isShuttingDown || !libraryPane)
      return;
    const QStringList files = libraryPane->selectedFiles();
    const int folders = libraryPane->selectedFolderCount();
    if (files.isEmpty()) {
      setStatus(folders ? "Foldery nie trafiają do kosza. Zaznacz zdjęcia."
                        : "Zaznacz zdjęcia w bibliotece, aby przenieść je do "
                          "kosza.");
      return;
    }
    askTrash(files, folders);
  }
  // Delete on a photo (or its menu): that displayed photo only.
  void trashPhoto(PhotoView *target = nullptr) {
    if (isShuttingDown)
      return;
    const QString photo = !target                        ? activePhoto()
                          : target == compareView.get() ? comparedPath
                                                        : path;
    if (photo.isEmpty()) {
      setStatus("Najpierw otwórz zdjęcie.");
      return;
    }
    askTrash({photo}, 0);
  }
  // Step 1: examine in the background. Nothing changes yet.
  void askTrash(const QStringList &files, int folders) {
    if (isShuttingDown || files.isEmpty())
      return;
    if (dropBusy()) {
      setStatus("Poczekaj: kopiowanie do folderu jeszcze trwa.");
      return;
    }
    if (trashBusy()) {
      setStatus("Poczekaj: przenoszenie do kosza jeszcze trwa.");
      return;
    }
    const QString folder = QFileInfo(files.first()).absolutePath();
    for (const auto &file : files)
      if (!validLocalPath(file) || QFileInfo(file).absolutePath() != folder) {
        setStatus("Zaznacz pliki z jednego folderu.");
        return;
      }
    trashCancel->store(true);
    auto token = std::make_shared<std::atomic_bool>(false);
    trashCancel = token;
    trashFolder = folder;
    trashFolders = folders;
    setStatus("Sprawdzam pliki do przeniesienia do kosza…");
    trashCheck.setFuture(QtConcurrent::run(&dropPool, [files, token] {
      TrashCheck result;
      try {
        result.items = FileOps::examineTrash(files, token);
      } catch (const std::exception &e) {
        result.error = QString::fromUtf8(e.what());
        result.cancelled = token->load();
      }
      return result;
    }));
  }
  static QString trashDetails(const QList<TrashItem> &items) {
    QStringList ready, blocked;
    for (const auto &item : items) {
      const QString name = QFileInfo(item.path).fileName();
      if (item.blocked)
        blocked << Ui::tr("%1 — pominięty: %2").arg(name, Ui::tr(item.note));
      else
        ready << QString("%1 — %2").arg(name, fileStamp(item.size, item.modified));
    }
    const QStringList all = ready + blocked;
    QStringList lines = all.mid(0, 12);
    const int hidden = int(all.size() - lines.size());
    if (hidden)
      lines.append(Ui::tr(Ui::counted(hidden, "…i jeszcze %1 plik",
                                      "…i jeszcze %1 pliki",
                                      "…i jeszcze %1 plików")));
    return lines.join('\n');
  }
  // Step 2: the question. Anuluj is the default answer (Enter) and Esc.
  void trashChecked() {
    if (isShuttingDown)
      return;
    const auto result = trashCheck.result();
    if (result.cancelled) {
      setStatus("Anulowano. Nic nie przeniesiono do kosza.");
      return;
    }
    if (!result.error.isEmpty()) {
      setStatus(Ui::tr("Nie przeniesiono do kosza: %1").arg(Ui::tr(result.error)));
      return;
    }
    int ready = 0;
    qint64 bytes = 0;
    trashBlocked = 0;
    const TrashItem *firstReady = nullptr, *firstBlocked = nullptr;
    for (const auto &item : result.items) {
      if (item.blocked) {
        ++trashBlocked;
        if (!firstBlocked)
          firstBlocked = &item;
      } else {
        ++ready;
        bytes += item.size;
        if (!firstReady)
          firstReady = &item;
      }
    }
    if (!ready) {
      // Nothing can go to the trash: say why, ask nothing.
      QString text = Ui::tr("Nie przeniesiono do kosza: %1 — %2")
                         .arg(QFileInfo(firstBlocked->path).fileName(),
                              Ui::tr(firstBlocked->note));
      if (trashBlocked > 1)
        text += " " + Ui::tr(Ui::counted(trashBlocked - 1, "…i jeszcze %1 plik",
                                         "…i jeszcze %1 pliki",
                                         "…i jeszcze %1 plików"));
      setStatus(text);
      return;
    }
    const QString folder = QFileInfo(trashFolder).fileName().isEmpty()
                               ? trashFolder
                               : QFileInfo(trashFolder).fileName();
    QString text =
        ready == 1
            ? Ui::tr("Plik „%1” zostanie przeniesiony z folderu „%2” do kosza "
                     "systemowego. Można go stamtąd przywrócić.")
                  .arg(QFileInfo(firstReady->path).fileName(), folder)
            : Ui::tr(Ui::counted(ready, // n ≥ 2: the first form is never used
                                 "%1 pliki z folderu „%2” zostaną przeniesione "
                                 "do kosza systemowego (razem %3). Można je "
                                 "stamtąd przywrócić.",
                                 "%1 pliki z folderu „%2” zostaną przeniesione "
                                 "do kosza systemowego (razem %3). Można je "
                                 "stamtąd przywrócić.",
                                 "%1 plików z folderu „%2” zostanie "
                                 "przeniesionych do kosza systemowego (razem "
                                 "%3). Można je stamtąd przywrócić."))
                  .arg(folder, QLocale().formattedDataSize(bytes, 1));
    if (trashBlocked)
      text += " " + Ui::tr(Ui::counted(trashBlocked,
                                       "%1 plik zostanie na miejscu.",
                                       "%1 pliki zostaną na miejscu.",
                                       "%1 plików zostanie na miejscu."));
    if (trashFolders)
      text += " " + Ui::tr(Ui::counted(trashFolders,
                                       "%1 zaznaczony folder pozostaje bez "
                                       "zmian.",
                                       "%1 zaznaczone foldery pozostają bez "
                                       "zmian.",
                                       "%1 zaznaczonych folderów pozostaje bez "
                                       "zmian."));
    // No accelerator on the move button (as in the duplicates confirmation):
    // only a click, or Tab to it and Enter, moves anything.
    const QString move =
        ready == 1 ? QString("Przenieś do kosza")
                   : Ui::counted(ready, "Przenieś %1 plik do kosza",
                                 "Przenieś %1 pliki do kosza",
                                 "Przenieś %1 plików do kosza");
    trashAsking = true;
    const auto items = result.items;
    Dialogs::message(
        window, ready == 1 ? "Przenieść plik do kosza?" : "Przenieść pliki do kosza?",
        text, trashDetails(items), {"&Anuluj", move}, 0, 0,
        [this, live = alive, items](int choice) {
          if (!*live)
            return;
          trashAsking = false;
          if (isShuttingDown)
            return;
          if (choice == 1)
            startTrash(items);
          else
            setStatus("Anulowano. Nic nie przeniesiono do kosza.");
        });
  }
  // Next photo of the folder that stays, else the previous one; never `other`
  // (the photo of the other pane).
  QString survivor(const QString &photo, const QSet<QString> &going,
                   const QString &other) const {
    if (photo.isEmpty() || !going.contains(photo) ||
        QFileInfo(photo).absolutePath() != siblingsDirectory)
      return {};
    const int i = int(siblings.indexOf(photo));
    if (i < 0)
      return {};
    for (int j = i + 1; j < siblings.size(); ++j)
      if (!going.contains(siblings.at(j)) && siblings.at(j) != other)
        return siblings.at(j);
    for (int j = i - 1; j >= 0; --j)
      if (!going.contains(siblings.at(j)) && siblings.at(j) != other)
        return siblings.at(j);
    return {};
  }
  // Step 3: move exactly the files listed in the question.
  void startTrash(const QList<TrashItem> &items) {
    if (isShuttingDown || dropBusy() || trashBusy())
      return;
    QSet<QString> going;
    for (const auto &item : items)
      if (!item.blocked)
        going.insert(item.path);
    trashNext = survivor(path, going, comparedPath);
    trashNextCompared = survivor(comparedPath, going, path);
    trashCancel->store(true);
    auto token = std::make_shared<std::atomic_bool>(false);
    trashCancel = token;
    setStatus("Przenoszę do kosza…");
    auto report = [this, token](qint64 done, qint64 total) {
      QMetaObject::invokeMethod(
          this,
          [this, token, done, total] {
            if (token == trashCancel && !token->load() && !isShuttingDown &&
                total > 1)
              setStatus(Ui::tr("Przenoszę do kosza: %1 / %2…")
                            .arg(done)
                            .arg(total));
          },
          Qt::QueuedConnection);
    };
    trashJob.setFuture(QtConcurrent::run(&dropPool, [items, token, report] {
      try {
        return FileOps::trashItems(items, token, report);
      } catch (const std::exception &e) {
        TrashReport failed;
        failed.error = QString::fromUtf8(e.what());
        return failed;
      }
    }));
  }
  // Step 4: report, and let nothing keep showing a photo that left.
  void trashFinished() {
    if (isShuttingDown)
      return;
    const TrashReport r = trashJob.result();
    const QSet<QString> gone(r.trashedPaths.cbegin(), r.trashedPaths.cend());
    bool replaced = false; // another photo took the place of a trashed one
    if (!gone.isEmpty()) {
      for (const auto &moved : r.trashedPaths)
        siblings.removeAll(moved);
      // The next photo opened lists the folder again (a listing in flight
      // may still name the moved files, and new files may have appeared).
      scanDirectory.clear();
      prefetchDelay.stop();
      prefetch->cancel();
      prefetchSignature.clear();
      for (int i = 0; i < tabStates.size(); ++i) {
        if (i == activeTabIndex)
          continue;
        if (gone.contains(tabStates[i].file))
          tabStates[i].file.clear();
        if (gone.contains(tabStates[i].comparison))
          tabStates[i].comparison.clear();
      }
      const bool leftGone = gone.contains(path),
                 rightGone = gone.contains(comparedPath);
      if (rightGone) {
        if (!trashNextCompared.isEmpty() && QFileInfo(trashNextCompared).isFile()) {
          showComparison(trashNextCompared);
          replaced = true;
        } else
          closePhotoPane(true);
      }
      if (leftGone) {
        if (!trashNext.isEmpty() && QFileInfo(trashNext).isFile()) {
          open(trashNext);
          replaced = true;
        } else
          closePhotoPane(false);
      }
      libraryPane->refresh();
    }
    QString text;
    if (r.cancelled)
      text = Ui::tr("Przerwano przenoszenie do kosza.");
    else if (!r.error.isEmpty())
      text = Ui::tr("Przerwano przenoszenie do kosza na pliku %1: %2")
                 .arg(r.failedFile, Ui::tr(r.error));
    const bool stopped = !text.isEmpty();
    if (r.trashed)
      text += (stopped ? " " : "") +
              (stopped ? Ui::tr(Ui::counted(
                             r.trashed, "Wcześniej przeniesiono do kosza %1 plik.",
                             "Wcześniej przeniesiono do kosza %1 pliki.",
                             "Wcześniej przeniesiono do kosza %1 plików."))
                       : Ui::tr(Ui::counted(r.trashed,
                                            "Przeniesiono do kosza %1 plik.",
                                            "Przeniesiono do kosza %1 pliki.",
                                            "Przeniesiono do kosza %1 plików.")));
    else
      text += (stopped ? " " : "") + Ui::tr("Nic nie przeniesiono do kosza.");
    if (r.skipped) {
      QStringList notes;
      for (const auto &note : r.skippedNotes.mid(0, 3))
        notes << Ui::tr(note);
      if (r.skipped > 3)
        notes << Ui::tr(Ui::counted(r.skipped - 3, "…i jeszcze %1 plik",
                                    "…i jeszcze %1 pliki",
                                    "…i jeszcze %1 plików"));
      text += " " + Ui::tr("Pominięto: %1.").arg(notes.join("; "));
    }
    if (trashBlocked)
      text += " " + Ui::tr(Ui::counted(trashBlocked, "%1 plik został na miejscu.",
                                       "%1 pliki zostały na miejscu.",
                                       "%1 plików zostało na miejscu."));
    setStatus(text);
    if (replaced) {
      trashNotice = text;
      trashNoticeUntil.setRemainingTime(5000);
    }
  }
  // The photo that replaced a trashed one reports "Gotowe" (after the preview
  // and again after the full picture): keep showing the trash report instead,
  // for a few seconds after the move. Opening another photo ends it.
  bool restoreTrashNotice(const QString &text) {
    if (trashNotice.isEmpty() || !text.startsWith("Gotowe"))
      return false;
    if (trashNoticeUntil.hasExpired()) {
      trashNotice.clear();
      return false;
    }
    setStatus(trashNotice);
    return true;
  }
  void beginShutdown() {
    if (isShuttingDown)
      return;
    isShuttingDown = true;
    // Cancel every producer BEFORE any destructor waits for a worker. No
    // completion callback may restart scans/prefetch while another pool drains.
    scanCancelled->store(true);
    scan.disconnect(this);
    clipboardCancel->store(true);
    clipboardWatcher.disconnect(this);
    exportCancel->store(true);
    exportWatcher.disconnect(this);
    // A copy into a folder stops between two reads; its unfinished file is
    // a hidden temporary that is removed, never a half-written photo.
    dropCancel->store(true);
    dropCheck.disconnect(this);
    dropCopy.disconnect(this);
    // A move to the trash stops before the next file; the one in progress is
    // a single rename, so each file is either in its folder or in the trash.
    trashCancel->store(true);
    trashCheck.disconnect(this);
    trashJob.disconnect(this);
    if (printJob)
      printJob->cancel();
    if (photoMenu)
      gtk_popover_popdown(GTK_POPOVER(photoMenu));
    if (libraryMenu)
      gtk_popover_popdown(GTK_POPOVER(libraryMenu));
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
  d->printJob.reset();
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
  d->clipboardPool.waitForDone();
  d->exportPool.waitForDone();
  d->dropPool.waitForDone();
  if (d->photoMenu)
    gtk_widget_unparent(d->photoMenu);
  d->photoMenu = nullptr;
  if (d->libraryMenu)
    gtk_widget_unparent(d->libraryMenu);
  d->libraryMenu = nullptr;
  d->favorites->shutdown();
  d->compareView.reset();
  d->view.reset();
  if (d->window)
    gtk_window_destroy(d->window);
  d->window = nullptr;
}
GtkWindow *PrismWindow::window() const { return d->window; }
void PrismWindow::exportTo(const QString &target) {
  d->exportTo(d->activePhoto(), target);
}
bool PrismWindow::exporting() const { return d->exportWatcher.isRunning(); }
bool PrismWindow::printToPdf(const QString &file, const PrintOptions &options) {
  return d->startPrint(file, &options);
}
bool PrismWindow::printing() const { return d->printJob && d->printJob->running(); }
bool PrismWindow::copyingToClipboard() const {
  return d->clipboardWatcher.isRunning();
}
void PrismWindow::showPhotoMenu(double x, double y) {
  d->showPhotoMenu(d->view.get(), x, y);
}
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
QString PrismWindow::dropTargetAt(double x, double y, bool fromLibrary) const {
  QString folder;
  switch (d->dropSpot(x, y, fromLibrary, &folder)) {
  case DropSpot::None:
    return "none";
  case DropSpot::Open:
    return "open";
  case DropSpot::Compare:
    return "compare";
  case DropSpot::Folder:
    return "folder:" + folder;
  }
  return {};
}
bool PrismWindow::dropFiles(const QStringList &files, double x, double y,
                            bool fromLibrary) {
  return d->dropFiles(files, x, y, fromLibrary);
}
bool PrismWindow::copyingToFolder() const {
  return d->dropCheck.isRunning() || d->dropCopy.isRunning();
}
bool PrismWindow::movingToTrash() const {
  return d->trashCheck.isRunning() || d->trashJob.isRunning();
}
void PrismWindow::showLibraryMenu(double x, double y) {
  d->showLibraryMenu(x, y);
}
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
