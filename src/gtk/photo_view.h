#pragma once
#include "gtkbase.h"
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <functional>
#include <memory>

// Tiled 16-bit OpenGL photo canvas. The renderer, shader, tile pyramid and
// cache policy are the ones validated in the Qt front end; only the windowing
// glue (GtkGLArea, gestures, frame clock) is GTK. The canvas is always opaque:
// interface translucency never reaches a photograph's pixels.
class PhotoView {
  struct Impl;
  std::unique_ptr<Impl> d;

public:
  explicit PhotoView(const QString &accessibleName);
  ~PhotoView();
  PhotoView(const PhotoView &) = delete;
  GtkWidget *widget() const;      // place this in the layout
  GtkWidget *focusWidget() const; // keyboard focus / shortcuts target

  std::function<void(bool)> loadingChanged;
  std::function<void(const QString &)> status, metadata, gpuReady;
  std::function<void(double)> zoomChanged;
  std::function<void()> focused;

  void open(const QString &path, const QString &displayProfile = {});
  void fit();
  void actual();
  void scale(double factor, QPointF anchor);
  void scaleCentered(double factor);
  void pan(QPointF delta);
  void release();
  void stop();
  bool busy() const;
  bool hardwareReady() const;
  bool loading() const;
  bool loadingAnimating() const;
  QSize imageSize() const;
  QRectF displayedImageRect() const;
  QSize logicalSize() const;
  double devicePixelRatio() const;
  double zoomFactor() const; // image pixels → physical pixels
  int previewEdge() const;
  QSize previewViewport() const;
  QString gpu() const;
  // Read back the rendered canvas (tests); null without a GL context.
  QImage framebuffer();
  quint64 textureBytes() const;
};
