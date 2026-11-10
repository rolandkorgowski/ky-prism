#include "localized_widgets.h"
#include "language_button.h"
#include "browser_history.h"
#include "favorites_ui.h"
#include "file_ops.h"
#include "glass_widgets.h"
#include "dialog_theme.h"
#include "library.h"
#include "menu_glass.h"
#include "pointer_gesture.h"
#include "photo_dialog.h"
#include "product_icon.h"
#include "prefetch.h"
#include "thumbnails.h"
#include "nvjpeg_backend.h"
#include "pixel_simd.h"
#include "ui_extensions.h"
#include "ui_icons.h"
#include "ui_material.h"
#include "usb_access.h"
#include "view.h"
#include "window_control.h"
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDirIterator>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedLayout>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWindow>
#include <QtConcurrent>
#include <iostream>
class SafeIcons final : public QFileIconProvider {
  QIcon folder, file;

public:
  SafeIcons() {
    for (int kind = 0; kind < 2; ++kind) {
      QPixmap pix(48, 48);
      pix.fill(Qt::transparent);
      QPainter p(&pix);
      p.setRenderHint(QPainter::Antialiasing);
      p.setPen(QPen(QColor("#91a7c5"), 2));
      p.setBrush(QColor(kind ? "#263c54" : "#354b67"));
      if (kind) {
        p.drawRoundedRect(QRectF(12, 6, 25, 36), 4, 4);
        p.drawLine(18, 21, 31, 21);
        p.drawLine(18, 28, 31, 28);
      } else {
        p.drawRoundedRect(QRectF(5, 12, 19, 12), 3, 3);
        p.drawRoundedRect(QRectF(5, 19, 38, 22), 4, 4);
      }
      if (kind)
        file = QIcon(pix);
      else
        folder = QIcon(pix);
    }
  }
  QIcon icon(const QFileInfo &info) const override {
    return info.isDir() ? folder : file;
  }
  QIcon icon(IconType type) const override {
    return type == Folder || type == Drive ? folder : file;
  }
};
class CameraGreeting final : public QWidget {
public:
  CameraGreeting() {
    setMinimumHeight(160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(width() / 2.0, height() / 2.0);
    p.scale(0.8, 0.8);
    QColor ink = palette().color(QPalette::WindowText);
    p.setPen(QPen(ink, 2.5));
    QColor glass = ink;
    glass.setAlpha(16);
    p.setBrush(glass);
    p.drawRoundedRect(QRectF(-90, -48, 180, 108), 18, 18);
    p.drawRoundedRect(QRectF(-30, -65, 60, 17), 5, 5);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(0, 5), 40, 40);
    p.drawEllipse(QPointF(0, 5), 31, 31);
    p.setBrush(ink);
    p.drawEllipse(QPointF(-12, 0), 3, 5);
    p.drawEllipse(QPointF(12, 0), 3, 5);
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(-14, 4, 28, 20), 200 * 16, 140 * 16);
    p.drawRoundedRect(QRectF(56, -31, 20, 10), 3, 3);
    p.drawLine(-70, -24, -51, -24);
    p.drawLine(-61, -33, -61, -15);
  }
};
class Window final : public UiWidget {
  View *view = nullptr;
  View *selectedView = nullptr;
  View *compareView = nullptr;
  QWidget *comparePanel = nullptr;
  QWidget *primaryPanel = nullptr, *primaryCaptionBar = nullptr,
          *tabsChrome = nullptr;
  QStackedWidget *libraryViews = nullptr;
  ThumbnailGrid *thumbnailGrid = nullptr;
  ThumbnailCache *thumbnails = nullptr;
  bool leftLoading = false, rightLoading = false;
  PreviewPrefetch *prefetch = nullptr;
  QTimer prefetchDelay;
  QString prefetchSignature;
  QString comparedPath, leftMetadata, rightMetadata, storageDirectory;
  LibraryProxy *library = nullptr;
  QTabBar *tabs = nullptr;
  UsbAccess *usb = nullptr;
  struct TabState {
    QString file, directory, comparison;
    BrowserHistory history;
  };
  QList<TabState> tabStates;
  int activeTab = 0;
  bool switchingTab = false;
  UiLabel *diskSpace = nullptr;
  QThreadPool storagePool, scanPool;
  std::shared_ptr<std::atomic_bool> scanCancelled = std::make_shared<std::atomic_bool>(false);
  bool shuttingDown = false;
  QFutureWatcher<QString> storageWatcher;
  bool storageInFlight = false, scanInFlight = false;
  QTimer storageTimer;
  Favorites *favorites = nullptr;
  HeartButton *heart;
  FavoritesDialog *favoritesDialog = nullptr;
  UiPushButton *maximizeButton = nullptr;
  BookmarksDialog *bookmarksDialog = nullptr;
  AboutDialog *aboutDialog = nullptr;
  UiPushButton *bookmarkStar = nullptr;
  bool restoreMaximized = false;
  QProcess maximizeProcess;
  QTimer maximizeTimeout;
  QWidget *welcome;
  QStackedLayout *pages;
  QWidget *browser;
  QFileSystemModel *fileModel;
  QTreeView *fileTree = nullptr;
  UiLineEdit *folderPath;
  SafeIcons safeIcons;
  BrowserHistory history;
  UiPushButton *backDirectory, *forwardDirectory;
  QPointer<FileDialog> fileDialog;
  UiLabel *status, *percent, *renderer;
  QLabel *title;
  UiMetadataEdit *info = nullptr;
  QString path, profile, scanDirectory;
  QStringList siblings;
  QFutureWatcher<QStringList> scan;
  QFileSystemWatcher theme;
  PointerGesture pointerGesture;

public:
  explicit Window(QString displayProfile) : profile(std::move(displayProfile)) {
    setWindowTitle("ky-prism • Zdjęcia");
    setObjectName("prism");
    resize(1200, 820);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAcceptDrops(true);
    qApp->installEventFilter(this);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    auto chrome = new QWidget;
    chrome->setObjectName("chrome");
    auto chromeLayout = new QVBoxLayout(chrome);
    chromeLayout->setContentsMargins(10, 4, 10, 5);
    chromeLayout->setSpacing(3);
    auto heading = new QHBoxLayout;
    title = new QLabel("ky-prism");
    title->setObjectName("documentTitle");
    title->setTextFormat(Qt::PlainText);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    heading->addWidget(title, 1);
    chromeLayout->addLayout(heading);
    auto bar = new QHBoxLayout;
    bar->setObjectName("photoToolbar");
    bar->setSpacing(4);
    chromeLayout->addLayout(bar);
    auto button = [&](QString text, auto fn) {
      auto b = new GlassButton(text);
      b->setFocusPolicy(Qt::StrongFocus);
      b->setFixedHeight(34);
      b->setCursor(Qt::PointingHandCursor);
      bar->addWidget(b);
      connect(b, &QPushButton::clicked, this, fn);
      return b;
    };
    auto separator = [&] { bar->addSpacing(8); };
    auto openButton = button("Otwórz", [this] {
      PhotoFileDialog chooser(
          this, history.current(),
          "Zdjęcia (*.jpg *.jpeg *.JPG *.JPEG *.tif *.tiff *.TIF *.TIFF *.png "
          "*.raw *.cr2 *.cr3 *.nef *.arw *.dng *.raf *.rw2 *.orf "
          "*.fff);;Wszystkie pliki (*)");
      if (chooser.exec()==QDialog::Accepted && !chooser.selectedFiles().isEmpty())
        open(chooser.selectedFiles().first());
    });
    openButton->setObjectName("openPhoto");
    openButton->setSymbol("openPhoto", "Otwórz zdjęcie");
    auto filesButton =
        button("Pliki", [this] { browser->setVisible(!browser->isVisible()); });
    filesButton->setObjectName("toggleLibrary");
    filesButton->setSymbol("sidebar", "Pokaż lub schowaj bibliotekę");
    filesButton->setCheckable(true);
    filesButton->setChecked(true);
    filesButton->setToolTip("Biblioteka • Ctrl+B");
    auto previous = button("", [this] { navigate(-1); });
    previous->setSymbol("previous", "Poprzednie zdjęcie");
    previous->setToolTip("Poprzednie zdjęcie • Lewo");
    auto next = button("", [this] { navigate(1); });
    next->setSymbol("next", "Następne zdjęcie");
    next->setToolTip("Następne zdjęcie • Prawo");
    auto newTabButton = button("", [this] { newTab(); });
    newTabButton->setObjectName("newTabAction");
    newTabButton->setSymbol("newTab");
    uiAccessible(newTabButton,"Nowa karta");
    newTabButton->setToolTip("Nowa karta • Ctrl+T");
    newTabButton->setFixedWidth(32);
    auto copyButton = button("", [this] { showFileDialog(false); });
    copyButton->setObjectName("copyFiles");
    copyButton->setSymbol("copyFiles");
    uiAccessible(copyButton,"Kopiuj zaznaczone zdjęcia");
    copyButton->setToolTip(
        "Kopiuj zaznaczone zdjęcia • Ctrl+Shift+C • Ctrl/Shift: wiele plików");
    copyButton->setFixedWidth(36);
    auto usbButton = button("", [this] { openUsb(); });
    usbButton->setObjectName("openUsb");
    usbButton->setSymbol("openUsb");
    uiAccessible(usbButton,"Otwórz pendrive");
    usbButton->setToolTip("Otwórz pendrive • Ctrl+U");
    usbButton->setFixedWidth(36);
    auto compareButton = button("", [this] { toggleCompare(); });
    compareButton->setObjectName("comparePhotos");
    compareButton->setSymbol("comparePhotos");
    uiAccessible(compareButton,"Porównaj dwa zdjęcia");
    compareButton->setToolTip(
        "Porównaj dwa zdjęcia obok siebie • Ctrl+Shift+P");
    compareButton->setFixedWidth(36);
    auto duplicates = button("", [this] { showFileDialog(true); });
    duplicates->setObjectName("findDuplicates");
    duplicates->setSymbol("duplicates", "Znajdź duplikaty");
    duplicates->setToolTip("Znajdź duplikaty • Ctrl+Shift+D");
    duplicates->setFixedWidth(36);
    separator();
    bar->addStretch();
    auto zoomOut = button("", [this] {
      activeView()->scale(0.8, activeView()->rect().center());
    });
    zoomOut->setSymbol("zoomOut", "Pomniejsz");
    zoomOut->setToolTip("Pomniejsz • −");
    percent = new UiLabel("");
    percent->setObjectName("zoomPercent");
    percent->setMinimumWidth(53);
    percent->setAlignment(Qt::AlignCenter);
    bar->addWidget(percent);
    auto zoomIn = button("", [this] {
      activeView()->scale(1.25, activeView()->rect().center());
    });
    zoomIn->setSymbol("zoomIn", "Powiększ");
    zoomIn->setToolTip("Powiększ • +");
    auto reset = button("Widok", [this] { activeView()->fit(); });
    reset->setObjectName("fitPhoto");
    reset->setSymbol("fit");
    reset->setToolTip("Wycentruj i dopasuj całe zdjęcie • Ctrl+0");
    auto original = button("1:1", [this] { activeView()->actual(); });
    original->setObjectName("actualPhoto");
    uiAccessible(original,"Oryginalna rozdzielczość 1:1");
    original->setToolTip(
        "Rozdzielczość 1:1 • Ctrl+1 • przeciągnij, aby przesuwać");
    separator();
    heart = new HeartButton;
    bar->addWidget(heart);
    auto star = button("", [this] { toggleBookmark(); });
    star->setSymbol("star");
    bookmarkStar = star;
    bookmarkStar->setObjectName("bookmarkStar");
    bookmarkStar->setCheckable(true);
    bookmarkStar->setFixedSize(36, 36);
    uiAccessible(bookmarkStar,"Zakładka bieżącego katalogu");
    auto bookmarks = button("", [this] { showBookmarks(); });
    bookmarks->setSymbol("bookmarks", "Przeglądarka zakładek");
    bookmarks->setToolTip("Zakładki katalogów • Ctrl+Shift+B");
    auto favoriteList = button("", [this] { showFavorites(); });
    favoriteList->setSymbol("favorites", "Przeglądarka ulubionych zdjęć");
    favoriteList->setToolTip("Ulubione zdjęcia • Ctrl+Shift+F");
    auto exifButton =
        button("EXIF", [this] { info->setVisible(!info->isVisible()); });
    exifButton->setObjectName("toggleExif");
    exifButton->setToolTip("Metadane • Ctrl+I");
    auto headingButton = [&](QString text, auto fn) {
      auto b = button(text, fn);
      bar->removeWidget(b);
      heading->addWidget(b);
      return b;
    };
    auto languageButton = new LanguageButton;
    heading->addWidget(languageButton);
    auto aboutButton = headingButton("", [this] { showAbout(0); });
    aboutButton->setObjectName("about");
    aboutButton->setSymbol("about");
    uiAccessible(aboutButton,"O programie");
    aboutButton->setToolTip("O programie • Ctrl+F1 • Pomoc: F1");
    auto fullscreenButton = headingButton("", [this] { fullscreen(); });
    fullscreenButton->setSymbol("fullscreen", "Pełny ekran");
    fullscreenButton->setToolTip("Pełny ekran • F11 / Esc");
    auto maxButton = headingButton("", [this] { maximize(); });
    maxButton->setSymbol("maximize", "Maksymalizuj lub przywróć");
    maximizeButton = maxButton;
    maximizeButton->setObjectName("maximize");
    maximizeButton->setToolTip("Maksymalizuj / przywróć • Ctrl+M");
    auto closeButton = headingButton("", [this] { close(); });
    closeButton->setSymbol("close", "Zamknij program");
    closeButton->setToolTip("Zamknij • Ctrl+W");
    layout->addWidget(chrome);
    tabsChrome = new QWidget;
    tabsChrome->setObjectName("tabsChrome");
    auto tabRow = new QHBoxLayout(tabsChrome);
    tabRow->setContentsMargins(10, 0, 10, 0);
    tabRow->setSpacing(4);
    tabs = new QTabBar;
    tabs->setObjectName("photoTabs");
    tabs->setExpanding(false);
    tabs->setTabsClosable(true);
    tabs->setElideMode(Qt::ElideMiddle);
    tabs->setUsesScrollButtons(true);
    tabStates.append(TabState{});
    tabs->addTab(PrismLanguage::text("Nowa karta"));
    tabs->setTabData(tabs->count()-1,"prismNewTab");
    focusTabControls();
    tabRow->addWidget(tabs, 1);
    auto plusTab = new GlassButton("＋");
    plusTab->setObjectName("newTab");
    uiAccessible(plusTab,"Nowa karta");
    plusTab->setToolTip("Nowa karta • Ctrl+T");
    plusTab->setFixedWidth(34);
    tabRow->addWidget(plusTab);
    layout->addWidget(tabsChrome);
    tabsChrome->hide();
    connect(plusTab, &QPushButton::clicked, this, [this] { newTab(); });
    connect(tabs, &QTabBar::currentChanged, this,
            [this](int i) { switchTab(i); });
    connect(tabs, &QTabBar::tabCloseRequested, this,
            [this](int i) { closeTab(i); });
    auto content = new QSplitter(Qt::Horizontal);
    content->setObjectName("librarySplitter");
    // The image canvas owns a GPU framebuffer. Commit its size once on
    // release instead of reallocating GPU buffers for every mouse pixel.
    content->setOpaqueResize(false);
    content->setChildrenCollapsible(false);
    content->setHandleWidth(1);
    browser = new QWidget;
    browser->setObjectName("browser");
    browser->setMinimumWidth(210);
    auto browseLayout = new QVBoxLayout(browser);
    browseLayout->setContentsMargins(10, 10, 10, 10);
    auto label = new UiLabel("BIBLIOTEKA");
    label->setObjectName("sectionTitle");
    browseLayout->addWidget(label);
    auto navigation = new QHBoxLayout;
    backDirectory = new GlassButton("←");
    forwardDirectory = new GlassButton("→");
    backDirectory->setObjectName("directoryBack");
    forwardDirectory->setObjectName("directoryForward");
    backDirectory->setToolTip("Poprzedni katalog • Alt+Lewo");
    forwardDirectory->setToolTip("Następny katalog • Alt+Prawo");
    for (auto b : {backDirectory, forwardDirectory}) {
      b->setFixedWidth(30);
      navigation->addWidget(b);
    }
    auto up = new GlassButton("↑");
    up->setFixedWidth(32);
    navigation->addWidget(up);
    folderPath = new UiLineEdit;
    folderPath->setObjectName("folderPath");
    uiAccessible(folderPath,"Ścieżka katalogu");
    folderPath->setPlaceholderText("Katalog zdjęć…");
    navigation->addWidget(folderPath);
    browseLayout->addLayout(navigation);
    connect(backDirectory, &QPushButton::clicked, this,
            [this] { directoryHistory(-1); });
    connect(forwardDirectory, &QPushButton::clicked, this,
            [this] { directoryHistory(1); });
    fileModel = new QFileSystemModel(this);
    fileModel->setReadOnly(true);
    fileModel->setOption(QFileSystemModel::DontUseCustomDirectoryIcons, true);
    fileModel->setIconProvider(&safeIcons);
    fileModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    fileModel->setNameFilters({"*.jpg", "*.jpeg", "*.tif", "*.tiff", "*.png",
                               "*.raw", "*.cr2",  "*.cr3", "*.nef",  "*.nrw",
                               "*.arw", "*.dng",  "*.raf", "*.rw2",  "*.orf",
                               "*.pef", "*.srw",  "*.3fr", "*.fff",  "*.iiq",
                               "*.kdc", "*.mos"});
    fileModel->setNameFilterDisables(false);
    fileTree = new QTreeView;
    fileTree->setObjectName("fileTree");
    uiAccessible(fileTree,"Pliki i katalogi zdjęć");
    fileTree->setTabKeyNavigation(false);
    library = new LibraryProxy(this);
    library->setSourceModel(fileModel);
    fileTree->setModel(library);
    fileTree->viewport()->setAutoFillBackground(false);
    fileTree->setUniformRowHeights(true);
    fileTree->setAlternatingRowColors(false);
    fileTree->setRootIsDecorated(false);
    fileTree->setItemsExpandable(false);
    fileTree->setIconSize(QSize(22, 22));
    fileTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileTree->setHeader(new GlassHeader(fileTree));
    fileTree->hideColumn(2);
    fileTree->hideColumn(3);
    fileTree->header()->setStretchLastSection(false);
    fileTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    fileTree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    fileTree->setColumnWidth(1, 78);
    auto sortRow = new QHBoxLayout;
    sortRow->setSpacing(3);
    QList<QPushButton *> sortButtons;
    auto currentSort = std::make_shared<int>(0);
    const QStringList sortLabels{"A–Z", "MB", "Utw.", "Zmi."};
    const QStringList sortNames{"Nazwa", "Wielkość pliku", "Czas utworzenia",
                                "Data zmiany"};
    for (int i = 0; i < sortLabels.size(); ++i) {
      auto choice = new GlassButton(sortLabels.at(i));
      choice->setObjectName("sortBy" + QString::number(i));
      uiAccessible(choice,"Sortuj: " + sortNames.at(i));
      choice->setToolTip("Sortuj: " + sortNames.at(i));
      choice->setCheckable(true);
      choice->setMinimumWidth(0);
      choice->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      sortButtons.append(choice);
      sortRow->addWidget(choice, 1);
    }
    sortButtons.first()->setChecked(true);
    auto descending = new GlassButton("↑");
    descending->setObjectName("sortDirection");
    descending->setCheckable(true);
    descending->setFixedWidth(30);
    uiAccessible(descending,"Odwróć kolejność sortowania");
    descending->setToolTip("Odwróć kolejność sortowania");
    sortRow->addWidget(descending);
    auto sortLibrary = [this, currentSort, descending] {
      library->sort(*currentSort, descending->isChecked() ? Qt::DescendingOrder
                                                          : Qt::AscendingOrder);
      descending->setText(descending->isChecked() ? "↓" : "↑");
    };
    for (int i = 0; i < sortButtons.size(); ++i)
      connect(sortButtons.at(i), &QPushButton::clicked, this,
              [sortLibrary, sortButtons, currentSort, i] {
                *currentSort = i;
                for (int j = 0; j < sortButtons.size(); ++j)
                  sortButtons.at(j)->setChecked(j == i);
                sortLibrary();
              });
    connect(descending, &QPushButton::clicked, this, sortLibrary);
    sortLibrary();
    browseLayout->addLayout(sortRow);
    auto modeRow = new QHBoxLayout;
    auto *listMode = new GlassButton("Lista"),
         *gridMode = new GlassButton("Miniatury");
    listMode->setSymbol("list");
    gridMode->setSymbol("grid");
    listMode->setObjectName("viewList");
    gridMode->setObjectName("viewThumbnails");
    listMode->setCheckable(true);
    gridMode->setCheckable(true);
    listMode->setChecked(true);
    modeRow->addWidget(listMode);
    modeRow->addWidget(gridMode);
    browseLayout->addLayout(modeRow);
    thumbnails = new ThumbnailCache(this);
    thumbnailGrid = new ThumbnailGrid;
    thumbnailGrid->enableGpu();
    thumbnailGrid->setModel(library);
    thumbnailGrid->setSelectionModel(fileTree->selectionModel());
    thumbnailGrid->setItemDelegate(
        new ThumbnailDelegate(thumbnails, thumbnailGrid));
    libraryViews = new QStackedWidget;
    libraryViews->setObjectName("libraryViews");
    libraryViews->addWidget(fileTree);
    libraryViews->addWidget(thumbnailGrid);
    browseLayout->addWidget(libraryViews);
    connect(listMode, &QPushButton::clicked, this, [this, listMode, gridMode] {
      listMode->setChecked(true);
      gridMode->setChecked(false);
      libraryViews->setCurrentWidget(fileTree);
      updateThumbnailActivity();
    });
    connect(gridMode, &QPushButton::clicked, this, [this, listMode, gridMode] {
      listMode->setChecked(false);
      gridMode->setChecked(true);
      libraryViews->setCurrentWidget(thumbnailGrid);
      updateThumbnailActivity();
    });
    connect(thumbnails, &ThumbnailCache::changed, thumbnailGrid, [this] {
      thumbnailGrid->viewport()->update();
      renderer->setToolTip(QString("Miniatury JPEG: nvJPEG/CUDA %1 • CPU fallback %2 • SIMD %3\n%4")
          .arg(NvjpegBackend::decoded()).arg(NvjpegBackend::fallbacks())
          .arg(PixelSimd::backend()).arg(NvjpegBackend::diagnostic()));
    });
    connect(thumbnailGrid->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] {
              thumbnails->discardRequests();
              thumbnailGrid->viewport()->update();
            });
    connect(thumbnailGrid, &QListView::clicked, this,
            [this](const QModelIndex &index) {
              if (QApplication::keyboardModifiers() &
                  (Qt::ControlModifier | Qt::ShiftModifier))
                return;
              auto source = library->mapToSource(index);
              if (!fileModel->isDir(source))
                open(fileModel->filePath(source));
            });
    connect(thumbnailGrid, &QListView::activated, this,
            [this](const QModelIndex &index) {
              auto source = library->mapToSource(index);
              auto name = fileModel->filePath(source);
              if (fileModel->isDir(source))
                browse(name);
              else if (name != path)
                open(name);
            });
    content->addWidget(browser);
    auto stage = new QWidget;
    pages = new QStackedLayout(stage);
    pages->setContentsMargins(0, 0, 0, 0);
    view = new View;
    view->setObjectName("photoView");
    uiAccessible(view,"Zdjęcie: strzałki przełączają zdjęcia, "
                            "Shift+strzałki przesuwają widok");
    auto photoPair = new QSplitter(Qt::Horizontal);
    photoPair->setObjectName("photoPair");
    photoPair->setHandleWidth(1);
    photoPair->setChildrenCollapsible(false);
    primaryPanel = new QWidget;
    auto primaryLayout = new QVBoxLayout(primaryPanel);
    primaryLayout->setContentsMargins(0, 0, 0, 0);
    primaryLayout->setSpacing(0);
    auto primaryCaption = new QLabel;
    primaryCaption->setObjectName("primaryTitle");
    primaryCaption->setTextFormat(Qt::PlainText);
    primaryCaption->setMargin(8);
    primaryCaption->setFixedHeight(32);
    primaryCaption->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    primaryCaptionBar = new QWidget;
    primaryCaptionBar->setObjectName("primaryBar");
    auto primaryCaptionLayout = new QHBoxLayout(primaryCaptionBar);
    primaryCaptionLayout->setContentsMargins(0, 0, 4, 0);
    primaryCaptionLayout->setSpacing(3);
    primaryCaptionLayout->addWidget(primaryCaption, 1);
    auto closeLeft = new GlassButton("×");
    closeLeft->setObjectName("closePrimaryPhoto");
    uiAccessible(closeLeft,"Zamknij lewe zdjęcie");
    closeLeft->setToolTip("Zamknij lewe zdjęcie");
    closeLeft->setFixedSize(28, 28);
    primaryCaptionLayout->addWidget(closeLeft);
    connect(closeLeft, &QPushButton::clicked, this,
            [this] { closePhotoPane(false); });
    primaryCaptionBar->hide();
    primaryLayout->addWidget(primaryCaptionBar);
    primaryLayout->addWidget(view, 1);
    photoPair->addWidget(primaryPanel);
    comparePanel = new QWidget;
    auto compareLayout = new QVBoxLayout(comparePanel);
    compareLayout->setContentsMargins(0, 0, 0, 0);
    compareLayout->setSpacing(0);
    auto comparisonLabel = new QLabel;
    comparisonLabel->setObjectName("comparisonTitle");
    comparisonLabel->setTextFormat(Qt::PlainText);
    comparisonLabel->setMargin(8);
    comparisonLabel->setFixedHeight(32);
    comparisonLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    auto comparisonBar = new QWidget;
    comparisonBar->setObjectName("comparisonBar");
    auto comparisonRow = new QHBoxLayout(comparisonBar);
    comparisonRow->setContentsMargins(0, 0, 4, 0);
    comparisonRow->setSpacing(3);
    comparisonRow->addWidget(comparisonLabel, 1);
    auto closeRight = new GlassButton("×");
    closeRight->setObjectName("closeComparisonPhoto");
    uiAccessible(closeRight,"Zamknij prawe zdjęcie");
    closeRight->setToolTip("Zamknij prawe zdjęcie");
    closeRight->setFixedSize(28, 28);
    comparisonRow->addWidget(closeRight);
    connect(closeRight, &QPushButton::clicked, this,
            [this] { closePhotoPane(true); });
    compareLayout->addWidget(comparisonBar);
    compareView = new View;
    compareView->setObjectName("compareView");
    uiAccessible(compareView,
        "Drugie zdjęcie — niezależny zoom i przesuwanie");
    compareLayout->addWidget(compareView, 1);
    photoPair->addWidget(comparePanel);
    comparePanel->hide();
    pages->addWidget(photoPair);
    welcome = new QWidget;
    welcome->setObjectName("welcome");
    auto greeting = new QVBoxLayout(welcome);
    greeting->addStretch();
    greeting->addWidget(new CameraGreeting);
    auto hello = new UiLabel("Mam oko do zdjęć. I dobry humor.");
    hello->setAlignment(Qt::AlignCenter);
    hello->setObjectName("hello");
    greeting->addWidget(hello);
    auto hint =
        new UiLabel("Wybierz zdjęcie z lewej lub upuść je tutaj.\nPokażę je bez "
                   "filtrów — obiecuję nie mrugać.");
    hint->setAlignment(Qt::AlignCenter);
    greeting->addWidget(hint);
    auto choose = new GlassButton("Załaduj zdjęcie");
    choose->setObjectName("loadPhoto");
    choose->setSymbol("addPhoto", "Załaduj zdjęcie");
    greeting->addWidget(choose, 0, Qt::AlignHCenter);
    connect(choose, &QPushButton::clicked, this, [chrome] {
      chrome->findChildren<QPushButton *>().first()->click();
    });
    greeting->addStretch();
    pages->addWidget(welcome);
    pages->setCurrentWidget(welcome);
    content->addWidget(stage);
    info = new UiMetadataEdit;
    info->setObjectName("exifPanel");
    info->setReadOnly(true);
    info->setTabChangesFocus(true);
    uiAccessible(info,"Metadane EXIF");
    info->setMaximumWidth(380);
    info->setMinimumWidth(260);
    info->hide();
    content->addWidget(info);
    content->setStretchFactor(1, 1);
    content->setSizes({280, 900, 300});
    layout->addWidget(content, 1);
    connect(up, &QPushButton::clicked, this, [this] {
      QDir dir(folderPath->text());
      if (dir.cdUp())
        browse(dir.absolutePath());
    });
    connect(folderPath, &UiLineEdit::returnPressed, this,
            [this] { browse(folderPath->text()); });
    connect(fileTree, &QTreeView::clicked, this,
            [this](const QModelIndex &index) {
              auto source = library->mapToSource(index);
              if (QApplication::keyboardModifiers() &
                  (Qt::ControlModifier | Qt::ShiftModifier))
                return;
              if (!fileModel->isDir(source))
                open(fileModel->filePath(source));
            });
    connect(fileTree, &QTreeView::activated, this,
            [this](const QModelIndex &index) {
              auto source = library->mapToSource(index);
              QString name = fileModel->filePath(source);
              if (fileModel->isDir(source))
                browse(name);
              else if (name != path)
                open(name);
            });
    QString pictures = QDir::homePath() + "/Pictures";
    browse(QDir(pictures).exists() ? pictures : QDir::homePath());
    auto footer = new QWidget;
    footer->setObjectName("footer");
    auto foot = new QVBoxLayout(footer);
    foot->setContentsMargins(12, 5, 12, 5);
    status = new UiLabel("Upuść zdjęcie • kółko: zoom • przeciągnięcie: "
                        "przesuwanie • F11 / Esc: pełny ekran");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    auto statusLine = new QHBoxLayout;
    statusLine->addWidget(status, 1);
    foot->addLayout(statusLine);
    renderer = new UiLabel("GPU: inicjalizacja…");
    renderer->setTextFormat(Qt::PlainText);
    auto rendererLine = new QHBoxLayout;
    rendererLine->addWidget(renderer, 1);
    diskSpace = new UiLabel("Miejsce: sprawdzanie…");
    diskSpace->setObjectName("diskSpace");
    diskSpace->setTextFormat(Qt::PlainText);
    rendererLine->addWidget(diskSpace, 0, Qt::AlignRight);
    foot->addLayout(rendererLine);
    storagePool.setMaxThreadCount(1);
    storagePool.setThreadPriority(QThread::LowPriority);
    connect(&storageWatcher, &QFutureWatcher<QString>::finished, this, [this] {
      storageInFlight = false;
      if (storageDirectory == history.current())
        diskSpace->setText(storageWatcher.result());
      else
        updateStorage();
    });
    storageTimer.setInterval(30000);
    connect(&storageTimer, &QTimer::timeout, this, [this] {
      if (isVisible())
        updateStorage();
    });
    storageTimer.start();
    updateStorage();
    layout->addWidget(footer);
    favorites = new Favorites(this);
    connect(favorites, &Favorites::changed, this,
            [this] { heart->setSelected(favorites->contains(activePhoto())); });
    connect(favorites, &Favorites::added, this, [this](const QString &name) {
      if (name == activePhoto()) {
        heart->setSelected(true, true);
        status->setText("Dodano do ulubionych • ścieżka i MD5 zapisane.");
      }
    });
    connect(favorites, &Favorites::error, status, &UiLabel::setText);
    auto cancelFavorite = new GlassButton("Anuluj ulubione");
    cancelFavorite->setFixedHeight(24);
    auto cancelPolicy = cancelFavorite->sizePolicy();
    cancelPolicy.setRetainSizeWhenHidden(true);
    cancelFavorite->setSizePolicy(cancelPolicy);
    statusLine->addWidget(cancelFavorite);
    cancelFavorite->hide();
    connect(cancelFavorite, &QPushButton::clicked, favorites,
            &Favorites::cancel);
    connect(favorites, &Favorites::working, this,
            [this, cancelFavorite](bool busy) {
              heart->setEnabled(!busy && !activePhoto().isEmpty());
              cancelFavorite->setVisible(busy);
            });
    connect(favorites, &Favorites::progress, this,
            [this](qint64 n, qint64 total) {
              status->setText(
                  total < 0 ? QString("Ulubione: przeszukano %1 wpisów…").arg(n)
                            : QString("Ulubione · MD5: %1 / %2 MiB")
                                  .arg(n / 1048576.0, 0, 'f', 1)
                                  .arg(total / 1048576.0, 0, 'f', 1));
            });
    connect(heart, &QAbstractButton::clicked, this, [this] {
      auto photo = activePhoto();
      if (photo.isEmpty())
        return;
      if (favorites->contains(activePhoto())) {
        for (const auto &e : favorites->all())
          if (e.path == photo) {
            favorites->remove(e.id);
            break;
          }
      } else
        favorites->add(photo);
    });
    heart->setEnabled(false);
    favorites->reload();
    maximizeTimeout.setSingleShot(true);
    maximizeTimeout.setInterval(3000);
    connect(&maximizeTimeout, &QTimer::timeout, this, [this] {
      maximizeProcess.kill();
      status->setText("Hyprland nie odpowiedział na żądanie maksymalizacji.");
    });
    connect(&maximizeProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
              maximizeTimeout.stop();
              maximizeButton->setEnabled(true);
              QString detail =
                  QString::fromUtf8(maximizeProcess.readAllStandardOutput() +
                                    maximizeProcess.readAllStandardError())
                      .left(512);
              if (code != 0 || detail.trimmed() != "ok")
                status->setText("Nie udało się zmienić maksymalizacji: " +
                                (detail.isEmpty()
                                     ? QString("Brak potwierdzenia Hyprland.")
                                     : detail));
            });
    connect(&maximizeProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
              maximizeTimeout.stop();
              maximizeButton->setEnabled(true);
              status->setText("Nie można wywołać sterowania oknem Hyprland.");
            });
    prefetch = new PreviewPrefetch(this);
    prefetchDelay.setSingleShot(true);
    prefetchDelay.setInterval(400);
    connect(&prefetchDelay, &QTimer::timeout, this,
            [this] { prefetchNeighbors(); });
    connect(view, &View::status, this, [this](const QString &text) {
      if (!shuttingDown && text.startsWith("Gotowe"))
        prefetchDelay.start();
    });
    connect(compareView, &View::status, this, [this](const QString &text) {
      if (comparePanel->isVisible())
        status->setText("Porównanie • " + text);
    });
    connect(view, &View::loadingChanged, this, [this](bool value) {
      if (shuttingDown) return;
      leftLoading = value;
      if (value) {
        prefetchDelay.stop();
        prefetch->cancel();
        prefetchSignature.clear();
      }
      thumbnails->setPaused(leftLoading || rightLoading);
    });
    connect(compareView, &View::loadingChanged, this, [this](bool value) {
      if (shuttingDown) return;
      rightLoading = value;
      if (value) {
        prefetchDelay.stop();
        prefetch->cancel();
        prefetchSignature.clear();
      }
      thumbnails->setPaused(leftLoading || rightLoading);
    });
    connect(filesButton, &QPushButton::clicked, this,
            [this] { updateThumbnailActivity(); });
    connect(view, &View::status, status, &UiLabel::setText);
    connect(view, &View::metadata, this, [this](const QString &text) {
      leftMetadata = text;
      if (activeView() == view)
        info->setPlainText(text);
    });
    connect(compareView, &View::metadata, this, [this](const QString &text) {
      rightMetadata = text;
      if (activeView() == compareView)
        info->setPlainText(text);
    });
    connect(view, &View::zoomChanged, this, [this](double value) {
      if (activeView() == view)
        percent->setText(QString::number(value * 100, 'f', 1) + "%");
    });
    connect(compareView, &View::zoomChanged, this, [this](double value) {
      if (activeView() == compareView)
        percent->setText(QString::number(value * 100, 'f', 1) + "%");
    });
    connect(view, &View::gpuReady, this, [this](const QString &gpu) {
      renderer->setText("Renderer: " + gpu +
                        (gpu.contains("NVIDIA", Qt::CaseInsensitive)
                             ? " • RTX aktywna"
                             : " • sprawdź wybór GPU, jeśli oczekujesz RTX"));
      std::cout << "OpenGL renderer: " << gpu.toStdString() << std::endl;
    });
    auto shortcut = [this](auto key, auto fn) {
      auto action = new QShortcut(key, this);
      action->setAutoRepeat(false);
      connect(action, &QShortcut::activated, this, fn);
      return action;
    };
    auto imageShortcut = [this](QKeySequence key, auto fn) {
      for (auto target : {view, compareView}) {
        auto action = new QShortcut(key, target);
        action->setContext(Qt::WidgetShortcut);
        connect(action, &QShortcut::activated, this, fn);
      }
    };
    shortcut(QKeySequence::Open, [openButton] { openButton->click(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_B),
             [filesButton] { filesButton->click(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_L), [this] { focusDirectory(); });
    shortcut(QKeySequence(Qt::ALT | Qt::Key_D), [this] { focusDirectory(); });
    shortcut(QKeySequence(Qt::Key_F6), [this] { cycleFocus(1); });
    shortcut(QKeySequence(Qt::SHIFT | Qt::Key_F6), [this] { cycleFocus(-1); });
    shortcut(QKeySequence(Qt::ALT | Qt::Key_Left),
             [this] { directoryHistory(-1); });
    shortcut(QKeySequence(Qt::ALT | Qt::Key_Right),
             [this] { directoryHistory(1); });
    shortcut(QKeySequence(Qt::ALT | Qt::Key_Up), [up] { up->click(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_D), [this] { toggleBookmark(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B),
             [this] { showBookmarks(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H), [this] {
      if (heart->isEnabled())
        heart->click();
    });
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F),
             [this] { showFavorites(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C),
             [this] { showFileDialog(false); });
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D),
             [this] { showFileDialog(true); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_I),
             [exifButton] { exifButton->click(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_M), [this] { maximize(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_L), [languageButton] { languageButton->click(); });
    shortcut(QKeySequence(Qt::Key_F1), [this] { showAbout(1); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_F1), [this] { showAbout(0); });
    shortcut(Qt::Key_F11, [this] { fullscreen(); });
    shortcut(Qt::Key_Escape, [this] {
      if (isFullScreen())
        fullscreen();
      else
        info->hide();
    });
    shortcut(QKeySequence::Close, [this] { close(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Escape),
             [this] { favorites->cancel(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_0),
             [this] { activeView()->fit(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_1),
             [this] { activeView()->actual(); });
    shortcut(QKeySequence::ZoomIn, [this] {
      activeView()->scale(1.25, activeView()->rect().center());
    });
    shortcut(QKeySequence::ZoomOut, [this] {
      activeView()->scale(0.8, activeView()->rect().center());
    });
    imageShortcut(Qt::Key_1, [this] { activeView()->actual(); });
    imageShortcut(Qt::Key_F, [this] { activeView()->fit(); });
    for (auto key : {Qt::Key_Left, Qt::Key_PageUp})
      imageShortcut(key, [this] { navigate(-1); });
    for (auto key : {Qt::Key_Right, Qt::Key_PageDown})
      imageShortcut(key, [this] { navigate(1); });
    imageShortcut(Qt::Key_Plus, [this] {
      activeView()->scale(1.25, activeView()->rect().center());
    });
    imageShortcut(Qt::Key_Equal, [this] {
      activeView()->scale(1.25, activeView()->rect().center());
    });
    imageShortcut(Qt::Key_Minus, [this] {
      activeView()->scale(0.8, activeView()->rect().center());
    });
    for (auto key : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down})
      for (bool large : {false, true}) {
        QPoint delta = key == Qt::Key_Left    ? QPoint(64, 0)
                       : key == Qt::Key_Right ? QPoint(-64, 0)
                       : key == Qt::Key_Up    ? QPoint(0, 64)
                                              : QPoint(0, -64);
        Qt::KeyboardModifiers modifiers = Qt::ShiftModifier;
        if (large)
          modifiers |= Qt::ControlModifier;
        imageShortcut(QKeySequence(QKeyCombination(modifiers, key)),
                      [this, delta, large] {
                        activeView()->pan(delta * (large ? 4 : 1));
                      });
      }
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_T), [this] { newTab(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), [this] {
      tabs->setCurrentIndex((activeTab + 1) % tabs->count());
    });
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), [this] {
      tabs->setCurrentIndex((activeTab + tabs->count() - 1) % tabs->count());
    });
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W),
             [this] { closeTab(activeTab); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_U), [this] { openUsb(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P),
             [this] { toggleCompare(); });
    connect(&scan, &QFutureWatcher<QStringList>::finished, this, [this] {
      if (shuttingDown) return;
      scanInFlight = false;
      siblings = scan.result();
      prefetchDelay.start();
      if (!activePhoto().isEmpty() &&
          QFileInfo(activePhoto()).absolutePath() != scanDirectory)
        scanSiblings();
    });
    connect(&theme, &QFileSystemWatcher::directoryChanged, this,
            [this] { applyTheme(); });
    connect(&theme, &QFileSystemWatcher::fileChanged, this,
            [this] { applyTheme(); });
    applyTheme();
  }
  ~Window() override {
    beginShutdown();
    qApp->removeEventFilter(this);
    disconnect(view, &View::loadingChanged, this, nullptr);
    disconnect(compareView, &View::loadingChanged, this, nullptr);
    prefetchDelay.stop();
    delete prefetch;
    prefetch = nullptr;
    delete thumbnails;
    thumbnails = nullptr;
    maximizeTimeout.stop();
    maximizeProcess.disconnect(this);
    maximizeTimeout.disconnect(this);
    maximizeProcess.kill();
    storageTimer.stop();
    storageWatcher.disconnect(this);
    storagePool.waitForDone();
    scanPool.waitForDone();
    favorites->shutdown();
    delete fileDialog.data();
    delete compareView;
    compareView = nullptr;
    delete view;
    view = nullptr;
    delete library;
    delete fileModel;
  }
  void browse(const QString &dir, bool remember = true) {
    if (shuttingDown) return;
    if (!validLocalPath(dir) || !QFileInfo(dir).isDir())
      return;
    QString absolute = QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
    if (remember)
      history.visit(absolute);
    folderPath->setText(absolute);
    library->setDirectory(absolute);
    auto rootIndex = library->mapFromSource(fileModel->setRootPath(absolute));
    fileTree->setRootIndex(rootIndex);
    thumbnailGrid->setRootIndex(rootIndex);
    thumbnails->discardRequests();
    if (tabs && !switchingTab) {
      tabs->setTabToolTip(activeTab, absolute);
      if (path.isEmpty())
        updatePathTabLabel(activeTab, QFileInfo(absolute).fileName());
    }
    if (diskSpace)
      updateStorage();
    updateBookmark();
    backDirectory->setEnabled(history.backAvailable());
    forwardDirectory->setEnabled(history.forwardAvailable());
  }
  void directoryHistory(int direction) {
    QString target = history.move(direction);
    if (target.isEmpty())
      return;
    if (!QFileInfo(target).isDir()) {
      history.move(-direction);
      status->setText("Katalog z historii już nie istnieje: " + target);
      return;
    }
    browse(target, false);
  }
  void updateBookmark() {
    try {
      bool saved = FolderBookmarks::read().contains(history.current());
      bookmarkStar->setChecked(saved);
      bookmarkStar->setText("");
      bookmarkStar->setToolTip(
          (saved ? "Usuń zakładkę katalogu" : "Zapisz zakładkę katalogu") +
          QString(" • Ctrl+D\n") + history.current());
    } catch (const std::exception &) {
      bookmarkStar->setChecked(false);
      bookmarkStar->setText("");
      bookmarkStar->setToolTip(
          "Nie można odczytać zakładek — otwórz przeglądarkę zakładek.");
    }
  }
  void toggleBookmark() {
    try {
      bool saved = FolderBookmarks::read().contains(history.current());
      FolderBookmarks::change(history.current(), !saved);
      updateBookmark();
      if (bookmarksDialog)
        bookmarksDialog->refresh();
      status->setText(saved ? "Usunięto zakładkę. Katalog pozostaje bez zmian."
                            : "Zapisano zakładkę katalogu.");
    } catch (const std::exception &e) {
      updateBookmark();
      status->setText(QString::fromUtf8(e.what()));
    }
  }
  void showBookmarks() {
    if (!bookmarksDialog) {
      bookmarksDialog =
          new BookmarksDialog([this] { return history.current(); }, this);
      connect(bookmarksDialog, &BookmarksDialog::openRequested, this,
              [this](const QString &directory) { browse(directory); });
      connect(bookmarksDialog, &BookmarksDialog::changed, this,
              [this] { updateBookmark(); });
    }
    bookmarksDialog->show();
    bookmarksDialog->raise();
    bookmarksDialog->activateWindow();
  }
  void updatePathTabLabel(int index,const QString &name) {
    tabs->setTabData(index,QVariant());tabs->setTabText(index,name);
  }
  void showAbout(int page) {
    if (!aboutDialog)
      aboutDialog = new AboutDialog(this);
    aboutDialog->showPage(page);
  }
  void focusDirectory() {
    if (!browser->isVisible())
      for (auto b : findChildren<QPushButton *>())
        if (b->objectName() == "toggleLibrary")
          b->click();
    folderPath->setFocus();
    folderPath->selectAll();
  }
  void cycleFocus(int delta) {
    QList<QWidget *> regions;
    if (view->isVisible())
      regions.append(view);
    if (compareView->isVisible())
      regions.append(compareView);
    else if (welcome->isVisible())
      regions.append(welcome->findChild<QPushButton *>());
    if (browser->isVisible()) {
      regions.append(libraryViews->currentWidget());
      regions.append(folderPath);
    }
    if (info->isVisible())
      regions.append(info);
    int current = regions.indexOf(QApplication::focusWidget());
    int next =
        current < 0 ? 0 : (current + delta + regions.size()) % regions.size();
    if (!regions.isEmpty())
      regions.at(next)->setFocus(Qt::ShortcutFocusReason);
  }
  void showFileDialog(bool duplicates) {
    if (shuttingDown) return;
    if (!duplicates && selectedPhotos().isEmpty()) {
      status->setText("Najpierw otwórz zdjęcie do skopiowania.");
      return;
    }
    if (fileDialog) {
      fileDialog->show();
      fileDialog->raise();
      fileDialog->activateWindow();
      return;
    }
    fileDialog =
        new FileDialog(activePhoto(), duplicates, folderPath->text(), this);
    if (!duplicates)
      fileDialog->setSources(selectedPhotos());
    fileDialog->show();
  }
  void open(const QString &name) {
    if (shuttingDown) return;
    if (!validLocalPath(name)) {
      if (status)
        status->setText("Nieprawidłowa lub zbyt długa ścieżka.");
      return;
    }
    if (QFileInfo(name).isDir()) {
      browse(name);
      return;
    }
    browse(QFileInfo(name).absolutePath());
    path = QDir::cleanPath(QFileInfo(name).absoluteFilePath());
    bool restoringPrimary = primaryPanel->isHidden();
    primaryPanel->show();
    if (restoringPrimary && !comparedPath.isEmpty())
      balancePhotoPanes();
    primaryCaptionBar->setVisible(!comparedPath.isEmpty());
    heart->setSelected(favorites->contains(path));
    heart->setEnabled(!favorites->busy());
    pages->setCurrentIndex(0);
    selectedView = view;
    title->setText(QFileInfo(path).fileName());
    findChild<QLabel *>("primaryTitle")->setText(QFileInfo(path).fileName());
    findChild<QLabel *>("primaryTitle")->setToolTip(path);
    setWindowTitle(QFileInfo(path).fileName() + " — ky-prism");
    info->setPlainText("Odczyt metadanych…");
    selectedView = view;
    view->open(path, profile);
    updatePathTabLabel(activeTab, QFileInfo(path).fileName());
    tabs->setTabToolTip(activeTab, path);
    updateStorage();
    scanSiblings();
  }
  View *activeView() const {
    return selectedView == compareView && comparePanel &&
                   comparePanel->isVisible()
               ? compareView
               : view;
  }
  QString activePhoto() const {
    return activeView() == compareView ? comparedPath : path;
  }
  QStringList selectedPhotos() const {
    QStringList result;
    for (const auto &i : fileTree->selectionModel()->selectedIndexes()) {
      if (i.column() != 0)
        continue;
      auto source = library->mapToSource(i);
      if (!fileModel->isDir(source))
        result.append(fileModel->filePath(source));
    }
    if (result.isEmpty() && !activePhoto().isEmpty())
      result.append(activePhoto());
    return result;
  }
  void updateStorage() {
    if (shuttingDown || !diskSpace || storageInFlight)
      return;
    auto dir = history.current();
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
  void saveTab() {
    if (activeTab >= 0 && activeTab < tabStates.size())
      tabStates[activeTab] = {path, history.current(), comparedPath, history};
  }
  void switchTab(int index) {
    if (switchingTab || index < 0 || index >= tabStates.size() ||
        index == activeTab)
      return;
    saveTab();
    activeTab = index;
    const auto state = tabStates.at(index);
    switchingTab = true;
    comparePanel->hide();
    primaryCaptionBar->hide();
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
      pages->setCurrentWidget(welcome);
      title->setText("ky-prism");
      setWindowTitle("ky-prism • Zdjęcia");
      heart->setSelected(false);
      heart->setEnabled(false);
      info->clear();
    }
    if (!state.comparison.isEmpty())
      showComparison(state.comparison);
    switchingTab = false;
    updateStorage();
  }
  void focusTabControls() {
    if (tabs)
      for (auto b : tabs->findChildren<QAbstractButton *>())
        b->setFocusPolicy(Qt::StrongFocus);
  }
  void newTab() {
    if (tabs->count() >= 24) {
      status->setText("Maksymalnie 24 karty.");
      return;
    }
    saveTab();
    TabState state;
    state.directory = history.current();
    state.history.visit(state.directory);
    tabStates.append(state);
    tabs->addTab(PrismLanguage::text("Nowa karta"));
    tabs->setTabData(tabs->count()-1,"prismNewTab");
    focusTabControls();
    tabsChrome->show();
    tabs->setCurrentIndex(tabs->count() - 1);
  }
  void closeTab(int index) {
    if (index < 0 || index >= tabs->count())
      return;
    if (tabs->count() == 1) {
      newTab();
      index = 0;
    }
    if (index == activeTab) {
      tabs->setCurrentIndex(index ? index - 1 : 1);
    }
    QSignalBlocker blocker(tabs);
    tabStates.removeAt(index);
    tabs->removeTab(index);
    if (activeTab > index)
      --activeTab;
    tabs->setCurrentIndex(activeTab);
    tabsChrome->setVisible(tabs->count() > 1);
    focusTabControls();
  }
  void balancePhotoPanes() {
    auto pair = findChild<QSplitter *>("photoPair");
    if (pair) {
      auto half = std::max(100, pair->width() / 2);
      pair->setSizes({half, half});
    }
  }
  void showComparison(const QString &name) {
    if (shuttingDown) return;
    if (!validLocalPath(name) || !QFileInfo(name).isFile())
      return;
    bool startingPair = comparePanel->isHidden() || primaryPanel->isHidden();
    comparedPath = QFileInfo(name).absoluteFilePath();
    findChild<QLabel *>("comparisonTitle")->setText(QFileInfo(name).fileName());
    findChild<QLabel *>("comparisonTitle")->setToolTip(comparedPath);
    primaryCaptionBar->setVisible(!path.isEmpty());
    primaryPanel->setVisible(!path.isEmpty());
    comparePanel->show();
    pages->setCurrentIndex(0);
    if (startingPair && !path.isEmpty())
      balancePhotoPanes();
    compareView->open(comparedPath, profile);
    if (path.isEmpty()) {
      selectedView = compareView;
      title->setText(QFileInfo(comparedPath).fileName());
      setWindowTitle(QFileInfo(comparedPath).fileName() + " — ky-prism");
      scanSiblings();
    }
  }
  void toggleCompare() {
    if (!comparedPath.isEmpty()) {
      closePhotoPane(true);
      return;
    }
    auto selected = selectedPhotos();
    if (selected.size() >= 2) {
      open(selected.first());
      showComparison(selected.at(1));
      return;
    }
    if (path.isEmpty()) {
      status->setText(
          "Otwórz pierwsze zdjęcie lub zaznacz dwa pliki w bibliotece.");
      return;
    }
    auto name = UiFileDialog::getOpenFileName(
        this, "Drugie zdjęcie do porównania", history.current(), "Zdjęcia (*)",
        nullptr, QFileDialog::DontUseNativeDialog);
    if (!name.isEmpty())
      showComparison(name);
  }
  void updateThumbnailActivity() {
    if (thumbnails)
      thumbnails->setActive(!shuttingDown && isVisible() && !isMinimized() &&
                            browser->isVisible() &&
                            libraryViews->currentWidget() == thumbnailGrid);
  }
  void closePhotoPane(bool right) {
    prefetchDelay.stop();
    prefetch->cancel();
    prefetchSignature.clear();
    if (right) {
      comparedPath.clear();
      compareView->release();
      comparePanel->hide();
      primaryPanel->show();
      primaryCaptionBar->hide();
      selectedView = view;
    } else {
      view->release();
      path.clear();
      leftMetadata.clear();
      primaryCaptionBar->hide();
      selectedView = compareView;
      if (!comparedPath.isEmpty())
        primaryPanel->hide();
    }
    auto current = right ? path : comparedPath;
    if (current.isEmpty()) {
      pages->setCurrentWidget(welcome);
      title->setText("ky-prism");
      setWindowTitle("ky-prism • Zdjęcia");
      heart->setSelected(false);
      heart->setEnabled(false);
      info->clear();
    } else {
      pages->setCurrentIndex(0);
      title->setText(QFileInfo(current).fileName());
      setWindowTitle(QFileInfo(current).fileName() + " — ky-prism");
      auto active = right ? view : compareView;
      active->setFocus();
      scanSiblings();
    }
  }
  void openUsb() {
    if (!usb) {
      usb = new UsbAccess(this);
      connect(usb, &UsbAccess::opened, this, [this](const QString &directory) {
        browse(directory);
        focusDirectory();
      });
      connect(usb, &UsbAccess::failed, status, &UiLabel::setText);
    }
    usb->open();
  }
  void scanSiblings() {
    if (shuttingDown || activePhoto().isEmpty())
      return;
    QString dir = QFileInfo(activePhoto()).absolutePath();
    if (scanInFlight || dir == scanDirectory)
      return;
    scanDirectory = dir;
    siblings.clear();
    scanInFlight = true;
    const auto cancelled = scanCancelled;
    scanPool.setMaxThreadCount(1);
    scanPool.setThreadPriority(QThread::LowPriority);
    scan.setFuture(QtConcurrent::run(&scanPool, [dir, cancelled] {
      QStringList list;
      QDirIterator it(dir, QDir::Files | QDir::Readable);
      QRegularExpression supported(
          "\\.(jpe?g|tiff?|png|raw|cr[23]|fff|nef|nrw|arw|dng|raf|rw2|orf|pef|"
          "srw|3fr|iiq|kdc|mos)$",
          QRegularExpression::CaseInsensitiveOption);
      int entries = 0;
      while (!cancelled->load() && it.hasNext() && entries++ < 50000) {
        QString p = it.next();
        if (supported.match(p).hasMatch())
          list.append(p);
      }
      if (!cancelled->load()) list.sort(Qt::CaseInsensitive);
      return list;
    }));
  }
  void prefetchNeighbors() {
    if (shuttingDown || !isVisible() || isMinimized() || path.isEmpty() || leftLoading ||
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
    int index = siblings.indexOf(path);
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
  void navigate(int delta) {
    int i = siblings.indexOf(activePhoto());
    if (i >= 0 && i + delta >= 0 && i + delta < siblings.size()) {
      auto next = siblings.at(i + delta);
      if (activeView() == compareView)
        showComparison(next);
      else
        open(next);
    }
  }
  void maximize() {
    if (!qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE") &&
        QGuiApplication::platformName() != "offscreen") {
      if (maximizeProcess.state() != QProcess::NotRunning)
        return;
      maximizeButton->setEnabled(false);
      maximizeProcess.start(
          "hyprctl",
          {"eval", prismMaximizeCommand(QCoreApplication::applicationPid())});
      maximizeTimeout.start();
    } else {
      if (isMaximized())
        showNormal();
      else
        showMaximized();
    }
  }
  void fullscreen() {
    if (isFullScreen()) {
      if (restoreMaximized)
        showMaximized();
      else
        showNormal();
    } else {
      restoreMaximized = isMaximized();
      showFullScreen();
    }
  }
  void showFavorites() {
    if (!favoritesDialog) {
      favoritesDialog = new FavoritesDialog(favorites, this);
      connect(favoritesDialog, &FavoritesDialog::openRequested, this,
              [this](const QString &name) { open(name); });
    }
    favoritesDialog->show();
    favoritesDialog->raise();
    favoritesDialog->activateWindow();
  }
  void applyTheme(QString data = {}) {
    QString base = QDir::homePath() + "/.local/state/omarchy/current",
            file = base + "/theme/colors.toml";
    QFile f(file);
    if (data.isEmpty() && f.open(QIODevice::ReadOnly))
      data = QString::fromUtf8(f.read(65536));
    auto color = [&](QString key, QString fallback) {
      QRegularExpression re("(?:^|\\n)\\s*" + key +
                            "\\s*=\\s*[\"'](#[0-9a-fA-F]{6})[\"']");
      auto m = re.match(data);
      return m.hasMatch() ? m.captured(1) : fallback;
    };
    QString bg = color("background", "#1e222c"),
            fg = color("foreground", "#e6e6e6"),
            accent = color("accent", "#8bb9ff");
    QColor c(bg);
    QString rgba =
        QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue())
            .arg(PrismAppearance::alpha(175));
    QColor edge(fg);
    QString separator = QString("rgba(%1,%2,%3,48)")
                            .arg(edge.red())
                            .arg(edge.green())
                            .arg(edge.blue());
    const QPalette colors = PrismDialogTheme::palette(palette(), QColor(bg), QColor(fg), QColor(accent));
    // Dialog windows do not inherit a parent's palette by default. Updating
    // only this Window left a dark stylesheet over the system's light Base.
    qApp->setPalette(colors);
    setPalette(colors);
    setWindowIcon(productIcon());
    fileTree->header()->setPalette(colors);
    fileTree->header()->viewport()->setPalette(colors);
    setStyleSheet(
        QString(
            "QWidget {color:%1;font-size:13px;} "
            "QWidget#chrome,QWidget#footer,QWidget#tabsChrome,QLabel#"
            "primaryTitle,QLabel#comparisonTitle "
            "{background:%2;border:0;border-bottom:1px solid "
            "%5;border-radius:0;} QWidget#footer "
            "{border-bottom:0;border-top:1px solid %5;} "
            "QPushButton#bookmarkStar {font-size:22px;} QLabel#productName "
            "{font-size:29px;font-weight:600;} QLabel#authorName "
            "{font-size:18px;font-weight:500;} QTabWidget::pane {border:1px "
            "solid %5;} QTabBar::tab {background:%4;color:%1;padding:9px;} "
            "QTabBar::tab:selected {border-bottom:2px solid %3;} "
            "QPlainTextEdit {background:%2;color:%1;border:1px solid "
            "%5;border-radius:12px;padding:12px;} "
            "QWidget#browser {background:%2;border-radius:0;} QTreeWidget "
            "{background:%4;color:%1;alternate-background-color:%2;selection-"
            "background-color:%3;selection-color:%4;} QProgressBar "
            "{border:1px solid "
            "%5;border-radius:4px;text-align:center;min-height:8px;} "
            "QProgressBar::chunk {background:%3;} QTreeView "
            "{background:transparent;border:0;outline:0;} QTreeView::item "
            "{height:34px;border-radius:5px;padding-left:3px;} "
            "QTreeView::item:selected {background:%3;color:%4;} "
            "QHeaderView::section "
            "{background:%4;color:%1;border:0;padding:7px;} QLineEdit "
            "{background:%4;color:%1;border:1px solid "
            "%5;border-radius:10px;padding:8px;} QDialog {background:%2;} "
            "QTableView "
            "{background:%2;alternate-background-color:%4;border:0;gridline-"
            "color:%5;selection-background-color:%3;selection-color:%4;} "
            "QWidget#welcome {background:%2;} QLabel#camera "
            "{font-size:52px;color:%3;} QLabel#hello "
            "{font-size:21px;font-weight:600;} QLabel#sectionTitle "
            "{font-size:11px;font-weight:600;} QSplitter::handle "
            "{background:%5;} QScrollBar:vertical "
            "{background:transparent;width:8px;} QScrollBar::handle:vertical "
            "{background:%3;border-radius:4px;min-height:24px;} "
            "QPlainTextEdit#exifPanel "
            "{background:%2;border:0;} "
            "QLineEdit#folderPath {background:transparent;border:1px solid "
            "%5;} "
            "QLineEdit#folderPath:focus {border:1px solid %3;} "
            "QLabel#documentTitle {font-size:14px;font-weight:600;} "
            "QLabel#copyHeading {font-size:24px;font-weight:600;} "
            "QLabel#dialogEyebrow {font-size:10px;font-weight:600;color:%3;} "
            "QLabel#destinationPath {background:%2;border:1px solid "
            "%5;border-radius:12px;padding:12px;} "
            "QHeaderView::section "
            "{font-size:11px;font-weight:600;border-bottom:1px solid %5;} "
            "QComboBox {background:%2;border:1px solid "
            "%5;border-radius:5px;padding:6px;} "
            "QComboBox QAbstractItemView "
            "{background:%4;color:%1;selection-background-color:%3;} "
            "QTabBar::tab {background:transparent;padding:7px "
            "12px;min-width:70px;max-width:200px;} "
            "QTabBar::tab:selected {background:%2;border-bottom:2px solid %3;} "
            "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical "
            "{height:0;}")
            .arg(fg, rgba, accent, bg, separator));
    setStyleSheet(
        styleSheet() + glassButtons(QColor(bg), QColor(fg), QColor(accent)) +
        PrismDialogTheme::fileDialogStyle(QColor(bg), QColor(fg), QColor(accent)) +
        QString("QListView#thumbnailGrid {background:transparent;border:0;} "
                "QWidget#primaryBar,QWidget#comparisonBar {background:%1;}")
            .arg(rgba));

    for (const QString &p : {base, base + "/theme", file})
      if (QFileInfo::exists(p) && !theme.files().contains(p) &&
          !theme.directories().contains(p))
        theme.addPath(p);
  }

protected:
  bool eventFilter(QObject *object, QEvent *event) override {
    if (shuttingDown) return UiWidget::eventFilter(object, event);
    if (event->type() == QEvent::FocusIn && info &&
        (object == view || object == compareView)) {
      selectedView = object == view ? view : compareView;
      if (favorites) {
        heart->setSelected(favorites->contains(activePhoto()));
        heart->setEnabled(!favorites->busy() && !activePhoto().isEmpty());
      }
      info->setPlainText(object == view ? leftMetadata : rightMetadata);
      scanSiblings();
      auto target = object == view ? view : compareView;
      if (!target->imageSize().isEmpty())
        percent->setText(QString::number(target->displayedImageRect().width() /
                                             target->imageSize().width() *
                                             target->devicePixelRatioF() * 100,
                                         'f', 1) +
                         "%");
    }
    if (event->type() == QEvent::KeyPress) {
      auto button = qobject_cast<QAbstractButton *>(object);
      auto key = static_cast<QKeyEvent *>(event);
      if (button && button->isEnabled() &&
          (button->window() == this || isAncestorOf(button)) &&
          (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
          (key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
        if (!key->isAutoRepeat())
          button->click();
        return true;
      }
    }
    if (event->type() == QEvent::MouseButtonRelease ||
        event->type() == QEvent::ApplicationDeactivate ||
        (object == this && (event->type() == QEvent::WindowDeactivate ||
                            event->type() == QEvent::Hide)))
      pointerGesture.cancel(this);
    auto target = qobject_cast<QWidget *>(object);
    if (target && target->window() == this && windowHandle()) {
      if (event->type() == QEvent::MouseButtonPress) {
        auto mouse = static_cast<QMouseEvent *>(event);
        pointerGesture.press(mouse, mapFromGlobal(mouse->globalPosition()),
                             size(), findChild<QWidget *>("chrome")->height(),
                             !isFullScreen() && !isMaximized(),
                             !isFullScreen() && !isMaximized(),
                             PointerGesture::interactive(target, this));
      } else if (event->type() == QEvent::MouseMove) {
        auto mouse = static_cast<QMouseEvent *>(event);
        if (pointerGesture.move(
                mouse, mapFromGlobal(mouse->globalPosition()), this,
                [this](Qt::Edges edges) {
                  return edges ? windowHandle()->startSystemResize(edges)
                               : windowHandle()->startSystemMove();
                }))
          return true;
      }
    }
    return QWidget::eventFilter(object, event);
  }
  void showEvent(QShowEvent *e) override {
    QWidget::showEvent(e);
    QTimer::singleShot(0, this, [this] { updateThumbnailActivity(); });
  }
  void hideEvent(QHideEvent *e) override {
    pointerGesture.cancel(this);
    PointerGesture::releaseOwnedGrab(this);
    prefetchDelay.stop();
    if (prefetch)
      prefetch->cancel();
    if (thumbnails)
      thumbnails->setActive(false);
    QWidget::hideEvent(e);
  }
  void changeEvent(QEvent *e) override {
    UiWidget::changeEvent(e);
    if(e->type()==QEvent::LanguageChange && tabs) {
      for(int i=0;i<tabs->count() && i<tabStates.size();++i)
        if(tabs->tabData(i).toString()=="prismNewTab")tabs->setTabText(i,PrismLanguage::text("Nowa karta"));
      if(fileTree)fileTree->header()->viewport()->update();
    }
    if (e->type() == QEvent::WindowStateChange) {
      updateThumbnailActivity();
      if (isMinimized()) {
        prefetchDelay.stop();
        if (prefetch)
          prefetch->cancel();
      }
    }
    if (e->type() == QEvent::WindowStateChange && maximizeButton)
      static_cast<GlassButton *>(maximizeButton)
          ->setSymbol(isMaximized() ? "restore" : "maximize");
  }
  void dragEnterEvent(QDragEnterEvent *e) override {
    if (e->mimeData()->hasUrls() && e->mimeData()->urls().first().isLocalFile())
      e->acceptProposedAction();
  }
  void dropEvent(QDropEvent *e) override {
    auto urls = e->mimeData()->urls();
    if (!urls.isEmpty() && urls.first().isLocalFile())
      open(urls.first().toLocalFile());
  }
  void closeEvent(QCloseEvent *e) override {
    beginShutdown();
    if (favoritesDialog) favoritesDialog->hide();
    if (bookmarksDialog) bookmarksDialog->hide();
    if (aboutDialog) aboutDialog->hide();
    if (fileDialog) fileDialog->hide();
    for (auto *chooser : findChildren<QFileDialog *>()) chooser->reject();
    hide();
    e->accept();
  }
private:
  void beginShutdown() {
    if (shuttingDown) return;
    shuttingDown = true;
    // Cancel every producer BEFORE any destructor waits for a worker. No
    // completion callback may restart scans/prefetch while another pool drains.
    scanCancelled->store(true);
    scan.disconnect(this);
    storageTimer.stop();
    storageWatcher.disconnect(this);
    theme.disconnect(this);
    if (library) library->stopWork();
    if (fileDialog) fileDialog->cancelForShutdown();
    if (usb) usb->shutdown();
    pointerGesture.cancel(this);
    PointerGesture::releaseOwnedGrab(this);
    prefetchDelay.stop();
    prefetch->cancel();
    thumbnails->setActive(false);
    compareView->stop();
    view->stop();
    favorites->stopWork();
    maximizeTimeout.stop();
    maximizeProcess.kill();
  }
};
int main(int argc, char **argv) {
  QString cacheRoot = qEnvironmentVariable(
      "PRISM_CACHE_DIR",
      qEnvironmentVariable("XDG_CACHE_HOME", QDir::homePath() + "/.cache") +
          "/omarchy-prism");
  if (!QDir().mkpath(cacheRoot)) {
    std::cerr << "Nie można utworzyć cache\n";
    return 1;
  }
  QTemporaryDir scratch(cacheRoot + "/session-XXXXXX");
  if (!scratch.isValid()) {
    std::cerr << "Nie można utworzyć katalogu roboczego\n";
    return 1;
  }
  qputenv("TMPDIR", scratch.path().toUtf8());
  qputenv("VIPS_DISC_THRESHOLD", "512m");
  if (VIPS_INIT(argv[0]))
    return 1;
  vips_concurrency_set(4);
  vips_cache_set_max_mem(128ULL * 1024 * 1024);
  vips_cache_set_max(0);
  vips_cache_set_max_files(32);
  QSurfaceFormat format;
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setAlphaBufferSize(8);
  format.setSwapInterval(1);
  QSurfaceFormat::setDefaultFormat(format);
  int rc;
  {
    // The UI follows its own palette. GTK's platform theme also initializes
    // GIO and can register the same DBus connection before Qt's host portal.
    // Keep portals enabled; avoid the competing GTK theme in this process only.
    if (qEnvironmentVariable("QT_QPA_PLATFORMTHEME").startsWith("gtk"))
      qunsetenv("QT_QPA_PLATFORMTHEME");
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setApplicationName("ky-prism");
    QGuiApplication::setDesktopFileName("ky-prism");
    QApplication app(argc, argv);
    PrismLanguage::initialize();
    app.setWindowIcon(productIcon());
    app.setStyle(new MenuGlassStyle);
    QFont uiFont;
    uiFont.setFamilies({"Inter", "Adwaita Sans", "Noto Sans"});
    uiFont.setPointSizeF(10.5);
    app.setFont(uiFont);
    QCoreApplication::setApplicationName("ky-prism");
    QCoreApplication::setApplicationVersion("0.12.2");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        PrismLanguage::text("ky-prism — kafelkowa przeglądarka zdjęć SDR"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(PrismLanguage::text("zdjęcie"), PrismLanguage::text("Lokalny plik zdjęcia"));
    parser.addOption(
        {"display-profile",
         PrismLanguage::text("Profil ICC aktualnego monitora (bez automatycznego przełączania)."),
         PrismLanguage::text("plik")});
    parser.addOption({"fullscreen", PrismLanguage::text("Uruchom na pełnym ekranie")});
    parser.process(app);
    {
    Window w(parser.value("display-profile"));
    if (parser.isSet("fullscreen"))
      w.showFullScreen();
    else
      w.show();
    if (!parser.positionalArguments().isEmpty())
      w.open(parser.positionalArguments().first());
    rc = app.exec();
    }
    // Release vendor contexts while Qt and the decoder runtime still exist,
    // rather than depending on global/static destructor ordering at exit.
    NvjpegBackend::shutdown();
    Document::clearSharedPreviews();
  }
  vips_shutdown();
  return rc;
}
