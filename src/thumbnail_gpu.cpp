#include "thumbnail_gpu.h"
#include "thumbnails.h"
#include <QPaintEvent>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVector2D>
#include <QVector4D>
#include <algorithm>
ThumbnailCanvas::ThumbnailCanvas(ThumbnailGrid *owner)
    : QOpenGLWidget(owner), grid(owner) {
  QSurfaceFormat format;
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setAlphaBufferSize(8);
  format.setSwapInterval(1);
  setFormat(format);
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
  setFocusPolicy(Qt::NoFocus);
}
ThumbnailCanvas::~ThumbnailCanvas() {
  if (context())
    disconnect(context(), nullptr, this, nullptr);
  if (isValid()) {
    makeCurrent();
    cleanup();
    doneCurrent();
  }
}
void ThumbnailCanvas::cleanup() {
  if (!ready)
    return;
  for (const auto &texture : textures)
    glDeleteTextures(1, &texture.id);
  textures.clear();
  visible.clear();
  previousVisible.clear();
  bytes = 0;
  if (vao)
    glDeleteVertexArrays(1, &vao);
  if (vbo)
    glDeleteBuffers(1, &vbo);
  vao = vbo = 0;
  program.removeAllShaders();
  ready = false;
}
void ThumbnailCanvas::initializeGL() {
  if (!initializeOpenGLFunctions()) {
    grid->fallbackFromGpu();
    return;
  }
  renderer = QString::fromLatin1(
      reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
  if (!program.addShaderFromSourceCode(QOpenGLShader::Vertex,
                                       ThumbnailShaders::vertex) ||
      !program.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                       ThumbnailShaders::fragment) ||
      !program.link()) {
    grid->fallbackFromGpu();
    return;
  }
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  const float points[] = {0, 0, 1, 0, 0, 1, 1, 1};
  glBufferData(GL_ARRAY_BUFFER, sizeof(points), points, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);
  glBindVertexArray(0);
  ready = true;
  connect(
      context(), &QOpenGLContext::aboutToBeDestroyed, this,
      [this] {
        makeCurrent();
        cleanup();
        doneCurrent();
      },
      Qt::DirectConnection);
}
void ThumbnailCanvas::paintGL() {
  if (!ready)
    return;
  frameUploads = 0;
  previousVisible.swap(visible);
  visible.clear();
  glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  auto delegate = dynamic_cast<ThumbnailDelegate *>(grid->itemDelegate());
  if (!delegate)
    return;
  delegate->setGpuPainter(this);
  QPaintEvent event(rect());
  grid->paintNativeItems(&event);
  delegate->setGpuPainter(nullptr);
}
bool ThumbnailCanvas::draw(QPainter *p, const QImage &image, const QRectF &r) {
  if (!ready || image.isNull() || image.format() != QImage::Format_RGBA8888)
    return false;
  const auto target = p->worldTransform().mapRect(r);
  const auto key = image.cacheKey();
  visible.insert(key);
  p->beginNativePainting();
  auto it = textures.find(key);
  if (it == textures.end()) {
    quint64 cost = 64;
    for (int w = image.width(), h = image.height();;) {
      cost += quint64(w) * h * 4;
      if (w == 1 && h == 1)
        break;
      w = std::max(1, w / 2);
      h = std::max(1, h / 2);
    }
    constexpr quint64 limit = 512ULL * 1024 * 1024;
    while (bytes + cost > limit && !textures.isEmpty()) {
      auto oldest = textures.end();
      for (auto candidate = textures.begin(); candidate != textures.end();
           ++candidate) {
        if (!visible.contains(candidate.key()) &&
            !previousVisible.contains(candidate.key()) &&
            (oldest == textures.end() || candidate->used < oldest->used))
          oldest = candidate;
      }
      if (oldest == textures.end()) {
        // A very dense 8K grid may exceed VRAM budget. Let Qt paint overflow
        // cells, rather than evicting visible textures and repainting forever.
        p->endNativePainting();
        return false;
      }
      glDeleteTextures(1, &oldest->id);
      bytes -= oldest->bytes;
      textures.erase(oldest);
    }
    // Bound synchronous driver work in one GUI frame, including a warm folder
    // with dozens of images arriving from disk together. No idle repaint loop.
    if (frameUploads >= 8) {
      if (!redrawPending) {
        redrawPending = true;
        QTimer::singleShot(0, this, [this] {
          redrawPending = false;
          update();
        });
      }
      p->endNativePainting();
      return true;
    }
    Texture texture;
    texture.bytes = cost;
    glGenTextures(1, &texture.id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width(), image.height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, image.constBits());
    glGenerateMipmap(GL_TEXTURE_2D);
    if (glGetError() != GL_NO_ERROR) {
      glDeleteTextures(1, &texture.id);
      p->endNativePainting();
      grid->fallbackFromGpu();
      return false;
    }
    it = textures.insert(key, texture);
    bytes += cost;
    ++uploads;
    ++frameUploads;
  }
  it->used = ++clock;
  glViewport(0, 0, qRound(width() * devicePixelRatioF()),
             qRound(height() * devicePixelRatioF()));
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_FRAMEBUFFER_SRGB);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  QRectF clip(rect());
  if (p->hasClipping())
    clip = clip.intersected(p->worldTransform().mapRect(p->clipBoundingRect()));
  const auto physical = QRectF(clip.topLeft() * devicePixelRatioF(),
                               clip.size() * devicePixelRatioF())
                            .toAlignedRect();
  glEnable(GL_SCISSOR_TEST);
  glScissor(physical.x(),
            qRound(height() * devicePixelRatioF()) - physical.bottom() - 1,
            physical.width(), physical.height());
  program.bind();
  program.setUniformValue("photo", 0);
  program.setUniformValue("dest", QVector4D(target.x(), target.y(),
                                            target.width(), target.height()));
  program.setUniformValue("viewportSize", QVector2D(width(), height()));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, it->id);
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
  program.release();
  glDisable(GL_SCISSOR_TEST);
  p->endNativePainting();
  return true;
}
