#include "view.h"
#include "glass_widgets.h"
#include "shaders.h"
#include <QGuiApplication>
#include <QOpenGLExtraFunctions>
#include <QPainter>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
namespace {
QPointF nativeAligned(QPointF offset, double zoom, double dpr) {
  if (std::abs(zoom * dpr - 1.0) < 1e-8)
    return {std::round(offset.x() * dpr) / dpr,
            std::round(offset.y() * dpr) / dpr};
  return offset;
}
} // namespace
class GlCanvas final : public QOpenGLWidget {
  View *owner;
  void initializeGL() override { owner->initializeGL(); }
  void paintGL() override { owner->paintGL(); }
  void resizeGL(int w, int h) override { owner->resizeGL(w, h); }

public:
  explicit GlCanvas(View *v) : QOpenGLWidget(v), owner(v) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
  }
};
QOpenGLContext *View::context() const {
  return canvas ? canvas->context() : nullptr;
}
bool View::isValid() const { return canvas && canvas->isValid(); }
void View::makeCurrent() {
  if (canvas)
    canvas->makeCurrent();
}
QImage View::framebuffer() {
  return canvas ? canvas->grabFramebuffer() : QImage();
}
void View::doneCurrent() {
  if (canvas)
    canvas->doneCurrent();
}
void View::update() {
  if (canvas)
    canvas->update();
  else
    QWidget::update();
}
void View::resizeEvent(QResizeEvent *e) {
  QWidget::resizeEvent(e);
  if (canvas)
    canvas->setGeometry(rect());
  else
    resizeGL(width(), height());
  if (loader) {
    loader->move((width() - loader->width()) / 2,
                 (height() - loader->height()) / 2);
    loader->raise();
  }
}
void View::paintEvent(QPaintEvent *e) {
  if (canvas) {
    QWidget::paintEvent(e);
    return;
  }
  QPainter p(this);
  p.fillRect(rect(), QColor("#131313"));
  if (!pendingPreview.isNull()) {
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(displayedImageRect(), pendingPreview);
  }
}

View::View(QWidget *parent) : QWidget(parent) {
  const auto platform = QGuiApplication::platformName();
  if (platform != "offscreen" && platform != "minimal")
    canvas = new GlCanvas(this);
  loader = new LoadingGlass(this);
  connect(this, &View::loadingChanged, loader, &LoadingGlass::setLoading);
  pool.setMaxThreadCount(1);
  pool.setExpiryTimeout(-1);
  pool.setThreadPriority(QThread::NormalPriority);
  timeline.start();
  refinement.setInterval(16);
  connect(&refinement, &QTimer::timeout, this, [this] { update(); });
  setFocusPolicy(Qt::StrongFocus);
  settle.setSingleShot(true);
  settle.setInterval(80);
  connect(&settle, &QTimer::timeout, this, &View::schedule);
  zoomAnimation.setDuration(110);
  zoomAnimation.setEasingCurve(QEasingCurve::OutCubic);
  connect(&zoomAnimation, &QVariantAnimation::valueChanged, this,
          [this](const QVariant &value) {
            zoom = std::exp(value.toDouble());
            offset = zoomAnchor - zoomPoint * zoom;
            Q_EMIT zoomChanged(zoom * devicePixelRatioF());
            update();
          });
  connect(&zoomAnimation, &QVariantAnimation::finished, this, [this] {
    offset = nativeAligned(offset, zoom, devicePixelRatioF());
    request();
  });
  connect(&watcher, &QFutureWatcher<WorkResult>::finished, this,
          &View::finishWork);
}
View::~View() {
  stop();
  watcher.disconnect(this);
  pool.waitForDone();
  document.reset();
  if (context())
    disconnect(context(), nullptr, this, nullptr);
  makeCurrent();
  cleanGL();
  doneCurrent();
}
void View::cleanGL() {
  if (!context() || !context()->isValid())
    return;
  for (const auto &[key, t] : textures) {
    Q_UNUSED(key);
    glDeleteTextures(1, &t.id);
  }
  textures.clear();
  gpuBytes = 0;
  if (vbo)
    glDeleteBuffers(1, &vbo);
  if (vao)
    glDeleteVertexArrays(1, &vao);
  vbo = vao = 0;
  program.removeAllShaders();
}
void View::initializeGL() {
  if (!initializeOpenGLFunctions()) {
    failed = true;
    stop();
    Q_EMIT status("Wymagany OpenGL 3.3.");
    return;
  }
  connect(
      context(), &QOpenGLContext::aboutToBeDestroyed, this,
      [this] {
        makeCurrent();
        cleanGL();
        doneCurrent();
      },
      Qt::DirectConnection);
  gpu = QString::fromLatin1(
            reinterpret_cast<const char *>(glGetString(GL_VENDOR))) +
        " • " +
        QString::fromLatin1(
            reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
  Q_EMIT gpuReady(gpu);
  bool ok = program.addShaderFromSourceCode(QOpenGLShader::Vertex,
                                            PrismShaders::vertex) &&
            program.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                            PrismShaders::fragment) &&
            program.link();
  if (!ok) {
    failed = true;
    stop();
    Q_EMIT status("Błąd shadera: " + program.log());
    return;
  }
  immutableTextures = context()->format().version() >= qMakePair(4, 2) ||
                      context()->hasExtension("GL_ARB_texture_storage");
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<void *>(2 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);
  if (!pendingPreview.isNull()) {
    upload({-1, 0, 0, pendingPreview});
    pendingPreview = {};
  }
  request();
}
QList<Tile> View::visibleTiles() const {
  QList<Tile> result;
  if (dimensions.isEmpty() || zoom <= 0)
    return result;
  double physical = zoom * devicePixelRatioF();
  int level = std::clamp(int(std::floor(std::log2(1.0 / physical))), 0, 24);
  double divisor = std::ldexp(1.0, level), step = 512 * divisor * zoom;
  int cols = int(
      std::ceil(std::max(1.0, std::round(dimensions.width() / divisor)) / 512));
  int rows = int(std::ceil(
      std::max(1.0, std::round(dimensions.height() / divisor)) / 512));
  int x0 = std::clamp(int(std::floor(-offset.x() / step)), 0, cols - 1);
  int y0 = std::clamp(int(std::floor(-offset.y() / step)), 0, rows - 1);
  int x1 =
      std::clamp(int(std::floor((width() - offset.x()) / step)), 0, cols - 1);
  int y1 =
      std::clamp(int(std::floor((height() - offset.y()) / step)), 0, rows - 1);
  for (int y = y0; y <= y1 && result.size() < 512; ++y)
    for (int x = x0; x <= x1 && result.size() < 512; ++x)
      result.append({level, x, y, {}});
  auto center = rect().center();
  std::stable_sort(result.begin(), result.end(),
                   [&](const Tile &a, const Tile &b) {
                     return QLineF(tileRect(a).center(), center).length() <
                            QLineF(tileRect(b).center(), center).length();
                   });
  return result;
}
QRectF View::tileRect(const Tile &t) const {
  double divisor = std::ldexp(1.0, t.level), s = divisor * zoom;
  int w = std::max(1, int(std::round(dimensions.width() / divisor)));
  int h = std::max(1, int(std::round(dimensions.height() / divisor)));
  return {offset.x() + t.x * 512 * s, offset.y() + t.y * 512 * s,
          std::min(512, w - t.x * 512) * s, std::min(512, h - t.y * 512) * s};
}
void View::upload(const Tile &t) {
  if (t.image.isNull() || (t.level >= 0 && textures.contains(t.key())) ||
      !isValid() || failed)
    return;
  makeCurrent();
  if (t.level < 0) {
    auto old = textures.find(t.key());
    if (old != textures.end()) {
      glDeleteTextures(1, &old->second.id);
      gpuBytes -= quint64(old->second.w) * old->second.h * 8;
      textures.erase(old);
    }
  }
  quint64 cost = quint64(t.image.sizeInBytes());
  while (gpuBytes + cost > gpuLimit && !textures.empty()) {
    auto oldest = std::min_element(
        textures.begin(), textures.end(),
        [](auto &a, auto &b) { return a.second.used < b.second.used; });
    glDeleteTextures(1, &oldest->second.id);
    gpuBytes -= quint64(oldest->second.w) * oldest->second.h * 8;
    textures.erase(oldest);
  }
  Texture tex;
  tex.level = t.level;
  tex.x = t.x;
  tex.y = t.y;
  tex.w = t.image.width();
  tex.h = t.image.height();
  tex.used = ++clock;
  tex.created = timeline.elapsed();
  glGenTextures(1, &tex.id);
  glBindTexture(GL_TEXTURE_2D, tex.id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  if (immutableTextures) {
    context()->extraFunctions()->glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16,
                                                tex.w, tex.h);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex.w, tex.h, GL_RGBA,
                    GL_UNSIGNED_SHORT, t.image.constBits());
  } else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, tex.w, tex.h, 0, GL_RGBA,
                 GL_UNSIGNED_SHORT, t.image.constBits());
  GLenum error = glGetError();
  if (error != GL_NO_ERROR) {
    glDeleteTextures(1, &tex.id);
    failed = true;
    stop();
    Q_EMIT status("Błąd pamięci GPU/OpenGL: " + QString::number(error));
  } else {
    textures.emplace(t.key(), tex);
    gpuBytes += cost;
    if (t.level >= 0)
      refinement.start();
  }
  doneCurrent();
}
void View::paintGL() {
  if (failed)
    return;
  glClearColor(0.075f, 0.075f, 0.075f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                      GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_FRAMEBUFFER_SRGB);
  program.bind();
  program.setUniformValue("photo", 0);
  glActiveTexture(GL_TEXTURE0);
  glBindVertexArray(vao);
  bool fading = false;
  auto draw = [&](Texture &tex, QRectF r, float u0, float v0, float u1,
                  float v1) {
    tex.used = ++clock;
    float opacity =
        tex.level < 0
            ? 1.0f
            : std::clamp(float(timeline.elapsed() - tex.created) / 100.0f, 0.0f,
                         1.0f);
    fading = fading || opacity < 1;
    program.setUniformValue("opacity", opacity);
    float l = float(2 * r.left() / width() - 1),
          rr = float(2 * r.right() / width() - 1);
    float top = float(1 - 2 * r.top() / height()),
          bottom = float(1 - 2 * r.bottom() / height());
    const float vertices[] = {l,  top,    u0, v0, l,  bottom, u0, v1,
                              rr, bottom, u1, v1, l,  top,    u0, v0,
                              rr, bottom, u1, v1, rr, top,    u1, v0};
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6);
  };
  auto preview = textures.find("-1/0/0");
  if (preview != textures.end())
    draw(preview->second, QRectF(offset, QSizeF(dimensions) * zoom), 0, 0, 1,
         1);
  auto visible = visibleTiles();
  int desired = visible.isEmpty() ? 0 : visible.first().level;
  // Coarse cached layers remain behind new detail; never expose blank tile
  // holes.
  QList<Texture *> layers;
  for (auto &[key, tex] : textures) {
    Q_UNUSED(key);
    if (tex.level >= desired)
      layers.append(&tex);
  }
  std::sort(
      layers.begin(), layers.end(),
      [](const Texture *a, const Texture *b) { return a->level > b->level; });
  for (auto *texture : layers) {
    auto &tex = *texture;
    Tile t{tex.level, tex.x, tex.y, {}};
    QRectF r = tileRect(t);
    if (!r.intersects(rect()))
      continue;
    float u0 = t.x ? 1.0f / tex.w : 0.0f, v0 = t.y ? 1.0f / tex.h : 0.0f;
    double div = std::ldexp(1.0, t.level);
    int iw = std::max(1, int(std::round(dimensions.width() / div))),
        ih = std::max(1, int(std::round(dimensions.height() / div)));
    float u1 = float((t.x ? 1 : 0) + std::min(512, iw - t.x * 512)) / tex.w,
          v1 = float((t.y ? 1 : 0) + std::min(512, ih - t.y * 512)) / tex.h;
    draw(tex, r, u0, v0, u1, v1);
  }
  glBindVertexArray(0);
  program.release();
  if (!fading)
    refinement.stop();
}
void View::release() {
  stop();
  pendingOpen = false;
  dimensions = {};
  pendingPreview = {};
  ++generation;
  if (isValid()) {
    makeCurrent();
    for (auto &[key, t] : textures) {
      Q_UNUSED(key);
      glDeleteTextures(1, &t.id);
    }
    textures.clear();
    gpuBytes = 0;
    doneCurrent();
  }
  // Clear decoder resources on its own serialized worker, without blocking UI.
  auto doc = document;
  (void)QtConcurrent::run(&pool, [doc] { doc->clear(); });
  update();
}
void View::open(const QString &path, const QString &displayProfile) {
  if (failed) {
    Q_EMIT loadingChanged(false);
    Q_EMIT status("GPU nie jest gotowe. Sprawdź obsługę OpenGL 3.3 i uruchom "
                "program ponownie.");
    return;
  }
  zoomAnimation.stop();
  refinement.stop();
  pendingPreview = {};
  cancel->store(true);
  cancel = std::make_shared<std::atomic_bool>(false);
  filename = path;
  profile = displayProfile;
  ++generation;
  pendingOpen = true;
  Q_EMIT loadingChanged(true);
  dimensions = {};
  if (isValid()) {
    makeCurrent();
    for (const auto &[key, t] : textures) {
      Q_UNUSED(key);
      glDeleteTextures(1, &t.id);
    }
    textures.clear();
    gpuBytes = 0;
    doneCurrent();
  }
  Q_EMIT status("Odczyt i profilowanie kolorów…");
  update();
  schedule();
}
void View::schedule() {
  if (workInFlight || zoomAnimation.state() == QAbstractAnimation::Running ||
      failed || cancel->load())
    return;
  quint64 gen = generation;
  auto doc = document;
  auto token = cancel;
  if (pendingOpen) {
    pendingOpen = false;
    QString name = filename, icc = profile;
    int edge = previewEdge();
    auto viewport = previewViewport();
    workInFlight = true;
    watcher.setFuture(QtConcurrent::run(
        &pool, [this, doc, token, gen, name, icc, edge, viewport] {
          WorkResult r;
          r.generation = gen;
          r.loaded = true;
          try {
            r.info = doc->open(
                name, icc, token,
                [this, token, gen](const Info &preview) {
                  QMetaObject::invokeMethod(
                      this,
                      [this, token, gen, preview] {
                        if (gen != generation || token->load() || !workInFlight)
                          return;
                        dimensions = preview.size;
                        if (isValid())
                          upload({-1, 0, 0, preview.preview});
                        else
                          pendingPreview = preview.preview;
                        loader->setLoading(false);
                        Q_EMIT status(preview.text);
                        fit();
                      },
                      Qt::QueuedConnection);
                },
                edge, viewport);
          } catch (const std::exception &e) {
            r.error = QString::fromUtf8(e.what()).left(2048);
            vips_error_clear();
          }
          return r;
        }));
    return;
  }
  if (!isValid()) {
    if (!canvas && !dimensions.isEmpty())
      Q_EMIT status("Gotowe • podgląd CPU (offscreen), bez testu GPU");
    return;
  }
  for (const auto &t : visibleTiles())
    if (!textures.contains(t.key())) {
      workInFlight = true;
      watcher.setFuture(QtConcurrent::run(&pool, [doc, token, gen, t] {
        WorkResult r;
        r.generation = gen;
        if (token->load())
          return r;
        try {
          r.tile = doc->tile(t.level, t.x, t.y);
        } catch (const std::exception &e) {
          r.error = QString::fromUtf8(e.what()).left(2048);
          vips_error_clear();
        }
        return r;
      }));
      return;
    }
  Q_EMIT status(QString("Gotowe • RAM cache ≤256 MiB • GPU kafelki %1 MiB")
                  .arg(gpuBytes / 1048576));
}
void View::finishWork() {
  auto r = watcher.result();
  workInFlight = false;
  if (cancel->load())
    return;
  if (r.generation != generation) {
    schedule();
    return;
  }
  if (!r.error.isEmpty()) {
    Q_EMIT loadingChanged(false);
    Q_EMIT status(r.error);
    Q_EMIT metadata(r.error);
    return;
  }
  if (r.loaded) {
    Q_EMIT loadingChanged(false);
    bool first = dimensions.isEmpty();
    dimensions = r.info.size;
    if (isValid())
      upload({-1, 0, 0, r.info.preview});
    else
      pendingPreview = r.info.preview;
    Q_EMIT metadata(r.info.text);
    if (first)
      fit();
  } else
    upload(r.tile);
  update();
  schedule();
}
void View::request() {
  update();
  if (!settle.isActive())
    settle.start();
}
void View::fit() {
  if (dimensions.isEmpty())
    return;
  zoomAnimation.stop();
  fitMode = true;
  zoom = std::min(double(width()) / dimensions.width(),
                  double(height()) / dimensions.height());
  offset = {(width() - dimensions.width() * zoom) / 2,
            (height() - dimensions.height() * zoom) / 2};
  offset = nativeAligned(offset, zoom, devicePixelRatioF());
  Q_EMIT zoomChanged(zoom * devicePixelRatioF());
  request();
}
void View::actual() {
  if (dimensions.isEmpty())
    return;
  zoomAnimation.stop();
  fitMode = false;
  zoom = 1.0 / devicePixelRatioF();
  zoomTarget = zoom;
  offset = {(width() - dimensions.width() * zoom) / 2,
            (height() - dimensions.height() * zoom) / 2};
  offset = nativeAligned(offset, zoom, devicePixelRatioF());
  Q_EMIT zoomChanged(1.0);
  Q_EMIT status("1:1 • jeden piksel zdjęcia na fizyczny piksel ekranu");
  request();
}
void View::scale(double factor, QPointF anchor) {
  if (dimensions.isEmpty())
    return;
  fitMode = false;
  double next = std::clamp((zoomAnimation.state() == QAbstractAnimation::Running
                                ? zoomTarget
                                : zoom) *
                               factor,
                           1.0 / 1048576, 64.0 / devicePixelRatioF());
  zoomAnimation.stop();
  zoomAnchor = anchor;
  zoomPoint = (anchor - offset) / zoom;
  zoomTarget = next;
  zoomAnimation.setStartValue(std::log(zoom));
  zoomAnimation.setEndValue(std::log(next));
  zoomAnimation.start();
}
void View::pan(QPointF delta) {
  if (dimensions.isEmpty())
    return;
  zoomAnimation.stop();
  fitMode = false;
  offset += delta;
  offset = nativeAligned(offset, zoom, devicePixelRatioF());
  request();
}
void View::resizeGL(int, int) {
  if (fitMode)
    fit();
  else
    request();
}
void View::wheelEvent(QWheelEvent *e) {
  double delta = e->angleDelta().y();
  if (delta == 0)
    delta = e->pixelDelta().y() * 3;
  scale(std::exp(delta * 0.0015), e->position());
  e->accept();
}
void View::mousePressEvent(QMouseEvent *e) {
  if (e->button() == Qt::LeftButton) {
    zoomAnimation.stop();
    drag = e->position();
    dragging = true;
    setCursor(Qt::ClosedHandCursor);
  }
}
void View::mouseMoveEvent(QMouseEvent *e) {
  if (dragging) {
    offset += e->position() - drag;
    drag = e->position();
    fitMode = false;
    request();
  }
}
void View::mouseReleaseEvent(QMouseEvent *) {
  dragging = false;
  offset = nativeAligned(offset, zoom, devicePixelRatioF());
  unsetCursor();
  request();
}
