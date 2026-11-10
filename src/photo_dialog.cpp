#include "photo_dialog.h"
#include "thumbnails.h"
#include <QDateTime>
#include <QFileSystemModel>
#include <QGridLayout>
#include <QHideEvent>
#include <QIconEngine>
#include <QListView>
#include <QPainter>
#include <QShowEvent>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QtConcurrent>
#include <vips/vips8>

namespace {
bool photoFile(const QFileInfo &file) {
  static const QSet<QString> types{"jpg","jpeg","tif","tiff","png","webp","avif","heic",
    "raw","cr2","cr3","nef","nrw","arw","dng","raf","rw2","orf","pef","srw","3fr","fff","iiq","kdc","mos"};
  return types.contains(file.suffix().toLower()) && file.isFile();
}
class PhotoIcon final : public QIconEngine {
  QImage image;
public:
  explicit PhotoIcon(QImage value) : image(std::move(value)) {}
  QIconEngine *clone() const override { return new PhotoIcon(image); }
  QSize actualSize(const QSize &size,QIcon::Mode,QIcon::State) override {
    return image.size().scaled(size,Qt::KeepAspectRatio);
  }
  void paint(QPainter *p,const QRect &rect,QIcon::Mode,QIcon::State) override {
    const auto size=image.size().scaled(rect.size(),Qt::KeepAspectRatio);
    p->save();p->setRenderHint(QPainter::SmoothPixmapTransform);
    p->drawImage(QRect(rect.center()-QPoint(size.width()/2,size.height()/2),size),image);p->restore();
  }
  QPixmap pixmap(const QSize &size,QIcon::Mode mode,QIcon::State state) override {
    QPixmap result(actualSize(size,mode,state));result.fill(Qt::transparent);
    QPainter p(&result);paint(&p,result.rect(),mode,state);return result;
  }
};
class PhotoDelegate final : public QStyledItemDelegate {
  PhotoDialogCache *cache;
public:
  PhotoDelegate(PhotoDialogCache *value, QObject *parent) : QStyledItemDelegate(parent), cache(value) {}
  QSize sizeHint(const QStyleOptionViewItem &option,const QModelIndex &index) const override {
    if (auto *list=qobject_cast<QListView *>(parent()); list && list->viewMode()==QListView::IconMode)
      return list->gridSize();
    return QStyledItemDelegate::sizeHint(option,index);
  }
  void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
    QStyledItemDelegate::initStyleOption(option, index);
    if (index.column() != 0) return;
    const QFileInfo file(index.data(QFileSystemModel::FilePathRole).toString());
    if (!photoFile(file)) return;
    const auto image = cache->request(file);
    // Standard pixmap icons are recoloured by Fusion when selected. Photo
    // pixels must retain their colours; only the selection background changes.
    if (!image.isNull()) option->icon = QIcon(new PhotoIcon(image));
  }
};
} // namespace

PhotoDialogCache::PhotoDialogCache(QObject *parent) : QObject(parent) {
  pool.setMaxThreadCount(2);
  pool.setThreadPriority(QThread::LowPriority);
}
PhotoDialogCache::~PhotoDialogCache() {
  setActive(false);
  for (auto *watcher : findChildren<QFutureWatcherBase *>()) watcher->disconnect(this);
  // Decoder tasks own only value data and cancellation tokens. Complete them
  // before libvips shuts down, with no callbacks into a destroyed dialog.
  pool.waitForDone();
}
void PhotoDialogCache::resetRequests() {
  ++generation;
  queue.clear(); pending.clear(); failed.clear();
  for (auto token : running) token->store(true);
}
void PhotoDialogCache::setActive(bool enabled) {
  active = enabled;
  if (!active) resetRequests();
  else pump();
}
QImage PhotoDialogCache::request(const QFileInfo &file) {
  if (!photoFile(file)) return {};
  const auto key = file.absoluteFilePath()+"\n"+QString::number(file.size())+"/"+
                   QString::number(file.lastModified().toMSecsSinceEpoch());
  if (auto *image = images.object(key)) return *image;
  if (active && !failed.contains(key) && !pending.contains(key) && !running.contains(key) && queue.size()<96) {
    pending.insert(key);
    queue.enqueue({key,file.absoluteFilePath(),generation});
    pump();
  }
  return {};
}
void PhotoDialogCache::pump() {
  while (active && running.size()<2 && !queue.isEmpty()) {
    const auto job = queue.dequeue();
    auto token = std::make_shared<std::atomic_bool>(false);
    running.insert(job.key,token);
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher,&QFutureWatcher<QImage>::finished,this,[this,job,token,watcher] {
      const auto image = watcher->result();
      running.remove(job.key); pending.remove(job.key); watcher->deleteLater();
      if (!token->load() && job.generation==generation) {
        if (!image.isNull()) images.insert(job.key,new QImage(image),int(image.sizeInBytes()/1024)+1);
        else failed.insert(job.key,new bool(true));
      }
      pump();
      if (active) emit changed();
    });
    watcher->setFuture(QtConcurrent::run(&pool,[path=job.path,token] {
      try {
        if (token->load()) return QImage{};
        return decodeThumbnail(path,512,token);
      } catch (const std::exception &) {
        vips_error_clear();
        return QImage{};
      }
    }));
  }
}

class PhotoDialogPreview final : public QWidget {
  QImage image;
  QString caption = "Zaznacz zdjęcie, aby zobaczyć podgląd";
public:
  explicit PhotoDialogPreview(QWidget *parent) : QWidget(parent) {
    setObjectName("photoChooserPreview");
    setAccessibleName("Podgląd zaznaczonego zdjęcia");
    setMinimumWidth(180); setMaximumWidth(280);
    setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Expanding);
  }
  void setImage(QImage value, const QString &text) {
    if (image.cacheKey()==value.cacheKey() && caption==text) return;
    image=std::move(value); caption=text;
    setProperty("hasPreview",!image.isNull());
    setAccessibleDescription(PrismLanguage::text(caption));
    update();
  }
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);
    auto edge=palette().color(QPalette::WindowText); edge.setAlpha(45);
    p.setPen(QPen(edge,1)); p.drawLine(0,0,0,height());
    auto slot=QRectF(rect()).adjusted(14,12,-10,-90);
    if (!image.isNull()) {
      auto size=QSizeF(image.size()); size.scale(slot.size(),Qt::KeepAspectRatio);
      p.drawImage(QRectF(slot.center()-QPointF(size.width()/2,size.height()/2),size),image);
    }
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(QRectF(rect()).adjusted(14,height()-80,-10,-10),Qt::AlignCenter|Qt::TextWrapAnywhere,PrismLanguage::text(caption));
  }
};

PhotoFileDialog::PhotoFileDialog(QWidget *parent,const QString &directory,const QString &filter)
  : UiFileDialog(parent,QStringLiteral("Załaduj zdjęcie"),directory,PrismLanguage::text(filter)),cache(this),preview(new PhotoDialogPreview(this)) {
  setObjectName("photoFileDialog");
  setWindowTitle("Załaduj zdjęcie");
  setOption(QFileDialog::DontUseNativeDialog);
  setFileMode(QFileDialog::ExistingFile);
  setAcceptMode(QFileDialog::AcceptOpen);
  setViewMode(QFileDialog::List);
  // Only the photo picker's existing file views are enhanced. Location,
  // filters, directory navigation and acceptance retain normal Qt behavior.
  for (auto *view : findChildren<QAbstractItemView *>()) {
    if (view->objectName()!="listView" && view->objectName()!="treeView") continue;
    view->setItemDelegate(new PhotoDelegate(&cache,view));
    if (auto *list=qobject_cast<QListView *>(view)) {
      list->setViewMode(QListView::IconMode);
      list->setIconSize(QSize(112,90)); list->setGridSize(QSize(160,132));
      list->setResizeMode(QListView::Adjust); list->setMovement(QListView::Static);
      list->setWrapping(true); list->setUniformItemSizes(true);
      list->setTextElideMode(Qt::ElideMiddle);
    }
    connect(&cache,&PhotoDialogCache::changed,view->viewport(),qOverload<>(&QWidget::update));
  }
  if (auto *grid=qobject_cast<QGridLayout *>(layout())) {
    grid->addWidget(preview,1,grid->columnCount());
  } else preview->hide();
  resize(1080,650);
  connect(this,&QFileDialog::currentChanged,this,[this](const QString &path) {currentPath=path; refreshPreview();});
  connect(this,&QFileDialog::directoryEntered,this,[this] {cache.resetRequests(); currentPath.clear(); refreshPreview();});
  connect(&cache,&PhotoDialogCache::changed,this,[this] {refreshPreview();});
}
void PhotoFileDialog::refreshPreview() {
  const QFileInfo file(currentPath);
  if (currentPath.isEmpty() || file.isDir()) {
    preview->setImage({},"Zaznacz zdjęcie, aby zobaczyć podgląd"); return;
  }
  const auto image=cache.request(file);
  preview->setImage(image,image.isNull() ? (cache.busy()?"Ładowanie podglądu…":"Brak podglądu tego pliku") : file.fileName());
}
void PhotoFileDialog::showEvent(QShowEvent *event) {
  cache.setActive(true);
  QFileDialog::showEvent(event);
  refreshPreview();
}
void PhotoFileDialog::hideEvent(QHideEvent *event) {
  cache.setActive(false);
  QFileDialog::hideEvent(event);
}
