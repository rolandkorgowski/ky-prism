#pragma once
#include "localized_widgets.h"
#include "library.h"
#include "thumbnail_cache.h"
#include <QCache>
#include <QHideEvent>
#include <QImage>
#include <QListView>
#include <QQueue>
#include <QShowEvent>
#include <QStyledItemDelegate>
#include <QTimer>
class ThumbnailImagePainter;
class ThumbnailCanvas;
class ThumbnailDelegate final : public QStyledItemDelegate {
  ThumbnailCache *cache;
  ThumbnailImagePainter *gpuPainter = nullptr;

public:
  explicit ThumbnailDelegate(ThumbnailCache *c, QObject *parent)
      : QStyledItemDelegate(parent), cache(c) {}
  void setGpuPainter(ThumbnailImagePainter *value) { gpuPainter = value; }
  void paint(QPainter *, const QStyleOptionViewItem &,
             const QModelIndex &) const override;
  QSize sizeHint(const QStyleOptionViewItem &,
                 const QModelIndex &) const override {
    auto grid = qobject_cast<QListView *>(parent());
    return {grid && grid->gridSize().width() > 0 ? grid->gridSize().width()
                                                 : 136,
            160};
  }
};
class ThumbnailGrid final : public QListView {
  ThumbnailCanvas *gpu = nullptr;
  bool fallbackPending = false;
  QTimer layoutDelay;
  void layoutCells() {
    int available = std::max(100, viewport()->width() - 4);
    int columns = std::max(1, available / 136);
    const QSize next(available / columns, 160);
    if (gridSize() != next)
      setGridSize(next);
  }
  void scheduleLayout() {
    if (isVisible() && !layoutDelay.isActive())
      layoutDelay.start();
  }
  void resizeEvent(QResizeEvent *e) override {
    QListView::resizeEvent(e);
    scheduleLayout();
  }
  void showEvent(QShowEvent *e) override {
    QListView::showEvent(e);
    scheduleLayout();
  }
  void hideEvent(QHideEvent *e) override {
    layoutDelay.stop();
    QListView::hideEvent(e);
  }

protected:
  bool viewportEvent(QEvent *) override;
  void scrollContentsBy(int, int) override;

public:
  void paintNativeItems(QPaintEvent *e) { QListView::paintEvent(e); }
  void fallbackFromGpu();
  bool gpuReady() const;
  quint64 gpuBytes() const;
  quint64 gpuUploads() const;
  QString gpuRenderer() const;
  QImage gpuFrame();
  void enableGpu();
  explicit ThumbnailGrid(QWidget *parent = nullptr) : QListView(parent) {
    setObjectName("thumbnailGrid");
    setViewMode(QListView::IconMode);
    // Custom reflow is coalesced to one pass per frame; never combine an
    // Adjust relayout with another synchronous relayout from resizeEvent.
    setResizeMode(QListView::Fixed);
    setGridSize({136, 160});
    layoutDelay.setSingleShot(true);
    layoutDelay.setInterval(16);
    connect(&layoutDelay, &QTimer::timeout, this, [this] { layoutCells(); });
    setMovement(QListView::Static);
    setLayoutMode(QListView::Batched);
    setBatchSize(40);
    setUniformItemSizes(true);
    setWrapping(true);
    setWordWrap(false);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Reserve a stable gutter. Auto scrollbar visibility must not change the
    // viewport width while the thumbnail columns are being recalculated.
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    uiAccessible(this,"Miniatury zdjęć i katalogów");
  }
};
