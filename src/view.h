#pragma once
#include "document.h"
#include <QOpenGLWidget>
#include <QResizeEvent>
class GlCanvas;
class LoadingGlass;
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QMouseEvent>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QThreadPool>
#include <QTimer>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <cmath>
#include <map>
struct WorkResult {
  quint64 generation = 0;
  bool loaded = false;
  Info info;
  Tile tile;
  QString error;
};
class View : public QWidget, protected QOpenGLFunctions_3_3_Core {
  Q_OBJECT
  friend class GlCanvas;
  GlCanvas *canvas = nullptr;
  LoadingGlass *loader = nullptr;
  bool immutableTextures = false;
  quint64 gpuLimit = 768ULL * 1024 * 1024;
  QOpenGLContext *context() const;
  bool isValid() const;
  void makeCurrent();
  void doneCurrent();
  void update();
  struct Texture {
    GLuint id = 0;
    int w = 0, h = 0, level = 0, x = 0, y = 0;
    quint64 used = 0;
    qint64 created = 0;
  };
  std::map<QString, Texture> textures;
  QOpenGLShaderProgram program;
  GLuint vao = 0, vbo = 0;
  quint64 gpuBytes = 0, clock = 0, generation = 0;
  QSize dimensions;
  double zoom = 1.0;
  QPointF offset, drag;
  bool dragging = false, fitMode = true, pendingOpen = false, failed = false;
  bool workInFlight = false;
  QString filename, profile;
  std::shared_ptr<Document> document = std::make_shared<Document>();
  std::shared_ptr<std::atomic_bool> cancel =
      std::make_shared<std::atomic_bool>(false);
  QThreadPool pool;
  QFutureWatcher<WorkResult> watcher;
  QTimer settle, refinement;
  QElapsedTimer timeline;
  QVariantAnimation zoomAnimation;
  double zoomTarget = 1.0;
  QPointF zoomAnchor, zoomPoint;
  QImage pendingPreview;
  QList<Tile> visibleTiles() const;
  QRectF tileRect(const Tile &tile) const;
  void schedule();
  void cleanGL();
  void upload(const Tile &tile);
  void request();
  void finishWork();

protected:
  void initializeGL();
  void paintGL();
  void paintEvent(QPaintEvent *) override;
  void resizeEvent(QResizeEvent *) override;
  void resizeGL(int, int);
  void wheelEvent(QWheelEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;

public:
  explicit View(QWidget *parent = nullptr);
  ~View() override;
  void open(const QString &, const QString &displayProfile = {});
  void fit();
  void release();
  bool hardwareReady() const { return isValid(); }
  QImage framebuffer();
  QSize imageSize() const { return dimensions; }
  void actual();
  void scale(double factor, QPointF anchor);
  void pan(QPointF delta);
  QRectF displayedImageRect() const {
    return QRectF(offset, QSizeF(dimensions) * zoom);
  }
  void stop() {
    cancel->store(true);
    pendingOpen = false;
    Q_EMIT loadingChanged(false);
    settle.stop();
    refinement.stop();
    zoomAnimation.stop();
  }
  bool busy() const {
    return workInFlight || pendingOpen || settle.isActive() ||
           refinement.isActive() ||
           zoomAnimation.state() == QAbstractAnimation::Running;
  }
  int previewEdge() const {
    return Document::previewBucket(
        int(std::ceil(std::max(width(), height()) * devicePixelRatioF())));
  }
  QSize previewViewport() const {
    return {int(std::ceil(width() * devicePixelRatioF())),
            int(std::ceil(height() * devicePixelRatioF()))};
  }
  QString gpu;
Q_SIGNALS:
  void loadingChanged(bool);
  void status(const QString &);
  void metadata(const QString &);
  void gpuReady(const QString &);
  void zoomChanged(double);
};
