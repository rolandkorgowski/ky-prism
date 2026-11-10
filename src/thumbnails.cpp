#include "thumbnails.h"
#include "thumbnail_gpu.h"
#include <QGuiApplication>
#include <QPainter>
#include <QTimer>
void ThumbnailDelegate::paint(QPainter *p, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const {
  p->save();
  p->setRenderHint(QPainter::Antialiasing);
  p->setRenderHint(QPainter::SmoothPixmapTransform);
  auto proxy = static_cast<const LibraryProxy *>(index.model());
  auto model = static_cast<const QFileSystemModel *>(proxy->sourceModel());
  auto file = model->fileInfo(proxy->mapToSource(index));
  auto ink = option.palette.color(QPalette::Text),
       background = option.palette.color(QPalette::Window),
       accent = option.palette.color(QPalette::Highlight);
  auto r = QRectF(option.rect).adjusted(4, 4, -4, -4);
  background.setAlpha(90);
  p->setBrush(background);
  auto edge = ink;
  edge.setAlpha(35);
  p->setPen(edge);
  if (option.state & QStyle::State_Selected) {
    accent.setAlpha(70);
    p->setBrush(accent);
    accent.setAlpha(220);
    p->setPen(QPen(accent, 1.2));
  }
  p->drawRoundedRect(r, 8, 8);
  QRectF slot = r.adjusted(7, 7, -7, -40);
  auto image = cache->request(
      file,
      std::min(512, int(slot.width() * p->device()->devicePixelRatioF())));
  if (!image.isNull()) {
    auto size = QSizeF(image.size());
    size.scale(slot.size(), Qt::KeepAspectRatio);
    const QRectF destination(
        slot.center() - QPointF(size.width() / 2, size.height() / 2), size);
    if (!gpuPainter || !gpuPainter->draw(p, image, destination))
      p->drawImage(destination, image);
  } else {
    auto center = slot.center();
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(ink, 1.4));
    if (file.isDir()) {
      p->drawRoundedRect(QRectF(center.x() - 20, center.y() - 10, 40, 25), 4,
                         4);
      p->drawLine(center + QPointF(-18, -14), center + QPointF(-3, -14));
    } else {
      p->drawRoundedRect(QRectF(center.x() - 18, center.y() - 15, 36, 30), 4,
                         4);
      p->drawPolyline(
          QPolygonF({center + QPointF(-13, 8), center + QPointF(-4, -3),
                     center + QPointF(3, 5), center + QPointF(10, -5),
                     center + QPointF(15, 8)}));
    }
  }
  p->setPen(ink);
  auto text = option.fontMetrics.elidedText(file.fileName(), Qt::ElideMiddle,
                                            int(r.width() - 12));
  p->drawText(r.adjusted(6, r.height() - 36, -6, -16),
              Qt::AlignHCenter | Qt::AlignVCenter, text);
  auto small = option.font;
  small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1));
  p->setFont(small);
  ink.setAlpha(170);
  p->setPen(ink);
  p->drawText(r.adjusted(6, r.height() - 19, -6, -2), Qt::AlignCenter,
              file.isDir()
                  ? QString("Katalog")
                  : QString("%1 MiB").arg(file.size() / 1048576.0, 0, 'f', 1));
  p->restore();
}

void ThumbnailGrid::enableGpu() {
  const auto platform = QGuiApplication::platformName();
  if (gpu || platform == "offscreen" || platform == "minimal" ||
      qEnvironmentVariable("PRISM_THUMB_GPU") == "0")
    return;
  gpu = new ThumbnailCanvas(this);
  setViewport(gpu);
  QTimer::singleShot(1500, this, [this] {
    if (gpu && isVisible() && !gpu->isValid())
      fallbackFromGpu();
  });
}
bool ThumbnailGrid::viewportEvent(QEvent *event) {
  if (gpu && event->type() == QEvent::Paint)
    return false;
  if (gpu && event->type() == QEvent::Resize) {
    QListView::viewportEvent(event);
    return false;
  }
  return QListView::viewportEvent(event);
}
void ThumbnailGrid::scrollContentsBy(int dx, int dy) {
  QListView::scrollContentsBy(dx, dy);
  if (gpu)
    gpu->update();
}
void ThumbnailGrid::fallbackFromGpu() {
  if (fallbackPending || !gpu)
    return;
  fallbackPending = true;
  QTimer::singleShot(0, this, [this] {
    gpu = nullptr;
    setViewport(new QWidget);
    fallbackPending = false;
    viewport()->update();
  });
}
bool ThumbnailGrid::gpuReady() const { return gpu && gpu->hardwareReady(); }
quint64 ThumbnailGrid::gpuBytes() const {
  return gpu ? gpu->textureBytes() : 0;
}
quint64 ThumbnailGrid::gpuUploads() const {
  return gpu ? gpu->uploadCount() : 0;
}
QString ThumbnailGrid::gpuRenderer() const {
  return gpu ? gpu->rendererName() : QString();
}
QImage ThumbnailGrid::gpuFrame() {
  return gpu ? gpu->grabFramebuffer() : QImage();
}
