#pragma once
#include <QHash>
#include <QImage>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPainter>
#include <QSet>
class ThumbnailGrid;
class ThumbnailImagePainter {
public:
  virtual ~ThumbnailImagePainter() = default;
  virtual bool draw(QPainter *, const QImage &, const QRectF &) = 0;
};
#include "thumbnail_shaders.h"
class ThumbnailCanvas final : public QOpenGLWidget,
                              protected QOpenGLFunctions_3_3_Core,
                              public ThumbnailImagePainter {
  struct Texture {
    GLuint id = 0;
    quint64 bytes = 0, used = 0;
  };
  QHash<qint64, Texture> textures;
  QSet<qint64> visible, previousVisible;
  QOpenGLShaderProgram program;
  ThumbnailGrid *grid;
  GLuint vao = 0, vbo = 0;
  quint64 bytes = 0, clock = 0, uploads = 0;
  unsigned frameUploads = 0;
  bool redrawPending = false;
  bool ready = false;
  QString renderer;
  void cleanup();

protected:
  void initializeGL() override;
  void paintGL() override;

public:
  explicit ThumbnailCanvas(ThumbnailGrid *);
  ~ThumbnailCanvas() override;
  bool draw(QPainter *, const QImage &, const QRectF &) override;
  bool hardwareReady() const { return ready; }
  quint64 textureBytes() const { return bytes; }
  quint64 uploadCount() const { return uploads; }
  QString rendererName() const { return renderer; }
};
