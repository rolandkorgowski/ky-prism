#include "photo_view.h"
#include "document.h"
#include "icons.h"
#include "language.h"
#include "shaders.h"
#include "theme.h"
#include "ui_text.h"
#include <epoxy/gl.h>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QLineF>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <map>

namespace {
struct WorkResult {
  quint64 generation = 0;
  bool loaded = false;
  Info info;
  Tile tile;
  QString error;
};
QPointF nativeAligned(QPointF offset, double zoom, double dpr) {
  if (std::abs(zoom * dpr - 1.0) < 1e-8)
    return {std::round(offset.x() * dpr) / dpr,
            std::round(offset.y() * dpr) / dpr};
  return offset;
}
struct Texture {
  GLuint id = 0;
  int w = 0, h = 0, level = 0, x = 0, y = 0;
  quint64 used = 0, bytes = 0;
  qint64 created = 0;
};
} // namespace

struct PhotoView::Impl : QObject {
  PhotoView *q;
  GtkWidget *overlay = nullptr, *base = nullptr, *area = nullptr,
            *hourglass = nullptr, *sizeWatch = nullptr;
  // GL state
  bool glReady = false, failed = false, immutableTextures = false,
       cpuOnly = false, gles = false, norm16 = true;
  GLuint program = 0, vao = 0, vbo = 0;
  GLint photoUniform = -1, opacityUniform = -1;
  std::map<QString, Texture> textures;
  quint64 gpuLimit = 768ULL * 1024 * 1024, gpuBytes = 0, clock = 0,
          generation = 0;
  int fbWidth = 0, fbHeight = 0;
  // View state (logical pixels)
  QSize dimensions;
  double zoom = 1.0, zoomTarget = 1.0, zoomFrom = 0, zoomTo = 0;
  QPointF offset, dragOrigin, pointer, zoomAnchor, zoomPoint;
  bool dragging = false, fitMode = true, pendingOpen = false,
       workInFlight = false, isLoading = false;
  double pinchLast = 1.0;
  QString filename, profile, gpuName;
  std::shared_ptr<Document> document = std::make_shared<Document>();
  std::shared_ptr<std::atomic_bool> cancel =
      std::make_shared<std::atomic_bool>(false);
  QThreadPool pool;
  QFutureWatcher<WorkResult> watcher;
  QTimer settle;
  QElapsedTimer timeline, loadingClock;
  Animation zoomAnimation;
  guint refineTick = 0, loadingTimer = 0;
  QImage pendingPreview, cpuPreview;
  GdkTexture *cpuTexture = nullptr;

  std::shared_ptr<bool> alive = std::make_shared<bool>(true);

  explicit Impl(PhotoView *owner) : q(owner) {}
  ~Impl() override { *alive = false; }
  int width() const { return base ? gtk_widget_get_width(base) : 0; }
  int height() const { return base ? gtk_widget_get_height(base) : 0; }
  double dpr() const {
    if (fbWidth > 0 && width() > 0)
      return double(fbWidth) / width();
    return base ? std::max(1, gtk_widget_get_scale_factor(base)) : 1.0;
  }
  bool valid() const { return glReady && !failed && area; }
  void update() {
    if (valid())
      gtk_gl_area_queue_render(GTK_GL_AREA(area));
    if (base)
      gtk_widget_queue_draw(base);
  }
  void emitStatus(const QString &text) {
    if (q->status)
      q->status(text);
  }
  void setLoading(bool value) {
    if (q->loadingChanged)
      q->loadingChanged(value);
    showHourglass(value);
  }
  void showHourglass(bool value) {
    if (isLoading == value)
      return;
    isLoading = value;
    if (value)
      loadingClock.start();
    gtk_widget_set_visible(hourglass, value);
    syncHourglass();
  }
  // 30 fps only while loading AND mapped; no timer otherwise.
  void syncHourglass() {
    const bool run = isLoading && hourglass && gtk_widget_get_mapped(hourglass);
    if (run && !loadingTimer)
      loadingTimer = g_timeout_add(
          33,
          +[](gpointer data) -> gboolean {
            auto *self = static_cast<Impl *>(data);
            gtk_widget_queue_draw(self->hourglass);
            return G_SOURCE_CONTINUE;
          },
          this);
    else if (!run && loadingTimer) {
      g_source_remove(loadingTimer);
      loadingTimer = 0;
    }
  }

  // ------------------------------------------------------------- GL -----
  static GLuint compile(GLenum type, const char *source, QString *log) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char buffer[1024] = {0};
      glGetShaderInfoLog(shader, sizeof buffer - 1, nullptr, buffer);
      *log += QString::fromUtf8(buffer);
      glDeleteShader(shader);
      return 0;
    }
    return shader;
  }
  // The validated GLSL 3.30 sources, adapted verbatim for GLSL ES 3.00:
  // only the version line, default precision and the uniform initialiser
  // (not allowed in ES; the value is set before every draw) differ.
  QByteArray shaderSource(const char *desktop) const {
    QByteArray source(desktop);
    if (!gles)
      return source;
    source.replace("#version 330 core",
                   "#version 300 es\nprecision highp float;\nprecision highp "
                   "sampler2D;");
    source.replace("uniform float opacity=1.0;", "uniform float opacity;");
    return source;
  }
  void useCpuFallback(const QString &reason) {
    failed = false;
    cpuOnly = true;
    glReady = false;
    if (area)
      gtk_widget_set_visible(area, FALSE);
    if (!reason.isEmpty())
      g_message("ky-prism: OpenGL canvas unavailable (%s); CPU preview only",
                reason.toUtf8().constData());
    if (q->gpuReady)
      q->gpuReady("CPU");
    gtk_widget_queue_draw(base);
  }
  void initializeGL() {
    if (cpuOnly)
      return;
    gtk_gl_area_make_current(GTK_GL_AREA(area));
    if (const GError *error = gtk_gl_area_get_error(GTK_GL_AREA(area))) {
      useCpuFallback(QString::fromUtf8(error->message));
      return;
    }
    // GTK shares one context family per display: desktop OpenGL on some
    // systems, OpenGL ES on others (its default on Wayland). Both run the same
    // shader and the same 16-bit tile path.
    auto *context = gtk_gl_area_get_context(GTK_GL_AREA(area));
    gles = context && gdk_gl_context_get_api(context) == GDK_GL_API_GLES;
    if (!context || epoxy_gl_version() < (gles ? 30 : 33)) {
      useCpuFallback("OpenGL 3.3 or OpenGL ES 3.0 required");
      return;
    }
    // Normalised 16-bit textures are core in OpenGL; an extension in ES.
    norm16 = !gles || epoxy_has_gl_extension("GL_EXT_texture_norm16");
    gpuName = QString::fromLatin1(
                  reinterpret_cast<const char *>(glGetString(GL_VENDOR))) +
              " • " +
              QString::fromLatin1(
                  reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
    QString log;
    const QByteArray vertexSource = shaderSource(PrismShaders::vertex),
                     fragmentSource = shaderSource(PrismShaders::fragment);
    const GLuint vertex =
                     compile(GL_VERTEX_SHADER, vertexSource.constData(), &log),
                 fragment = compile(GL_FRAGMENT_SHADER,
                                    fragmentSource.constData(), &log);
    GLint linked = GL_FALSE;
    if (vertex && fragment) {
      program = glCreateProgram();
      glAttachShader(program, vertex);
      glAttachShader(program, fragment);
      glBindAttribLocation(program, 0, "position");
      glBindAttribLocation(program, 1, "texcoord");
      glLinkProgram(program);
      glGetProgramiv(program, GL_LINK_STATUS, &linked);
      if (!linked) {
        char buffer[1024] = {0};
        glGetProgramInfoLog(program, sizeof buffer - 1, nullptr, buffer);
        log += QString::fromUtf8(buffer);
      }
    }
    if (vertex)
      glDeleteShader(vertex);
    if (fragment)
      glDeleteShader(fragment);
    if (!linked) {
      if (program)
        glDeleteProgram(program);
      program = 0;
      useCpuFallback("shader: " + log.left(300));
      return;
    }
    photoUniform = glGetUniformLocation(program, "photo");
    opacityUniform = glGetUniformLocation(program, "opacity");
    immutableTextures = gles || epoxy_gl_version() >= 42 ||
                        epoxy_has_gl_extension("GL_ARB_texture_storage");
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
    glReady = true;
    if (q->gpuReady)
      q->gpuReady(gpuName);
    if (!pendingPreview.isNull()) {
      upload({-1, 0, 0, pendingPreview});
      pendingPreview = {};
    }
    request();
  }
  void cleanGL() {
    if (!area || !gtk_widget_get_realized(area))
      return;
    gtk_gl_area_make_current(GTK_GL_AREA(area));
    if (gtk_gl_area_get_error(GTK_GL_AREA(area)))
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
    if (program)
      glDeleteProgram(program);
    vbo = vao = program = 0;
    glReady = false;
  }
  void dropTextures() {
    if (!valid() || !gtk_widget_get_realized(area))
      return;
    gtk_gl_area_make_current(GTK_GL_AREA(area));
    for (const auto &[key, t] : textures) {
      Q_UNUSED(key);
      glDeleteTextures(1, &t.id);
    }
    textures.clear();
    gpuBytes = 0;
  }
  QList<Tile> visibleTiles() const {
    QList<Tile> result;
    if (dimensions.isEmpty() || zoom <= 0)
      return result;
    double physical = zoom * dpr();
    int level = std::clamp(int(std::floor(std::log2(1.0 / physical))), 0, 24);
    double divisor = std::ldexp(1.0, level), step = 512 * divisor * zoom;
    int cols = int(std::ceil(
        std::max(1.0, std::round(dimensions.width() / divisor)) / 512));
    int rows = int(std::ceil(
        std::max(1.0, std::round(dimensions.height() / divisor)) / 512));
    int x0 = std::clamp(int(std::floor(-offset.x() / step)), 0, cols - 1);
    int y0 = std::clamp(int(std::floor(-offset.y() / step)), 0, rows - 1);
    int x1 = std::clamp(int(std::floor((width() - offset.x()) / step)), 0,
                        cols - 1);
    int y1 = std::clamp(int(std::floor((height() - offset.y()) / step)), 0,
                        rows - 1);
    for (int y = y0; y <= y1 && result.size() < 512; ++y)
      for (int x = x0; x <= x1 && result.size() < 512; ++x)
        result.append({level, x, y, {}});
    const QPointF center(width() / 2.0, height() / 2.0);
    std::stable_sort(result.begin(), result.end(),
                     [&](const Tile &a, const Tile &b) {
                       return QLineF(tileRect(a).center(), center).length() <
                              QLineF(tileRect(b).center(), center).length();
                     });
    return result;
  }
  QRectF tileRect(const Tile &t) const {
    double divisor = std::ldexp(1.0, t.level), s = divisor * zoom;
    int w = std::max(1, int(std::round(dimensions.width() / divisor)));
    int h = std::max(1, int(std::round(dimensions.height() / divisor)));
    return {offset.x() + t.x * 512 * s, offset.y() + t.y * 512 * s,
            std::min(512, w - t.x * 512) * s, std::min(512, h - t.y * 512) * s};
  }
  void setCpuPreview(const QImage &image) {
    cpuPreview = image;
    g_clear_object(&cpuTexture);
    gtk_widget_queue_draw(base);
  }
  void upload(const Tile &t) {
    if (t.image.isNull())
      return;
    if (!valid()) {
      if (t.level < 0 && (cpuOnly || !glReady))
        setCpuPreview(t.image);
      return;
    }
    if (t.level >= 0 && textures.contains(t.key()))
      return;
    gtk_gl_area_make_current(GTK_GL_AREA(area));
    if (t.level < 0) {
      auto old = textures.find(t.key());
      if (old != textures.end()) {
        glDeleteTextures(1, &old->second.id);
        gpuBytes -= old->second.bytes;
        textures.erase(old);
      }
    }
    // Without normalised 16-bit textures (rare ES drivers) tiles are uploaded
    // as 8 bits per channel, the depth of the canvas they are drawn into.
    const QImage pixels = norm16
                              ? t.image
                              : t.image.convertToFormat(QImage::Format_RGBA8888);
    const GLenum internal = norm16 ? GL_RGBA16 : GL_RGBA8,
                 type = norm16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
    const quint64 cost = quint64(pixels.sizeInBytes());
    while (gpuBytes + cost > gpuLimit && !textures.empty()) {
      auto oldest = std::min_element(
          textures.begin(), textures.end(),
          [](auto &a, auto &b) { return a.second.used < b.second.used; });
      glDeleteTextures(1, &oldest->second.id);
      gpuBytes -= oldest->second.bytes;
      textures.erase(oldest);
    }
    Texture tex;
    tex.level = t.level;
    tex.x = t.x;
    tex.y = t.y;
    tex.w = pixels.width();
    tex.h = pixels.height();
    tex.used = ++clock;
    tex.bytes = cost;
    tex.created = timeline.elapsed();
    while (glGetError() != GL_NO_ERROR) {
    }
    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    if (immutableTextures) {
      glTexStorage2D(GL_TEXTURE_2D, 1, internal, tex.w, tex.h);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex.w, tex.h, GL_RGBA, type,
                      pixels.constBits());
    } else
      glTexImage2D(GL_TEXTURE_2D, 0, GLint(internal), tex.w, tex.h, 0, GL_RGBA,
                   type, pixels.constBits());
    const GLenum error = glGetError();
    glBindTexture(GL_TEXTURE_2D, 0);
    if (error != GL_NO_ERROR) {
      glDeleteTextures(1, &tex.id);
      failed = true;
      q->stop();
      emitStatus("Błąd pamięci GPU/OpenGL: " + QString::number(error));
    } else {
      textures.emplace(t.key(), tex);
      gpuBytes += cost;
      if (t.level >= 0)
        startRefinement();
    }
  }
  void startRefinement() {
    if (refineTick || !area)
      return;
    refineTick = gtk_widget_add_tick_callback(
        area,
        +[](GtkWidget *widget, GdkFrameClock *, gpointer) -> gboolean {
          gtk_gl_area_queue_render(GTK_GL_AREA(widget));
          return G_SOURCE_CONTINUE;
        },
        nullptr, nullptr);
  }
  void stopRefinement() {
    if (refineTick && area)
      gtk_widget_remove_tick_callback(area, refineTick);
    refineTick = 0;
  }
  void paintGL() {
    // The canvas is opaque by construction: alpha is cleared to 1 and the
    // blend function keeps destination alpha at 1 for every tile.
    glClearColor(0.075f, 0.075f, 0.075f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (failed || !glReady || width() <= 0 || height() <= 0)
      return;
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                        GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    if (!gles)
      glDisable(GL_FRAMEBUFFER_SRGB);
    glUseProgram(program);
    glUniform1i(photoUniform, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao);
    bool fading = false;
    const double w = width(), h = height();
    auto draw = [&](Texture &tex, QRectF r, float u0, float v0, float u1,
                    float v1) {
      tex.used = ++clock;
      float opacity =
          tex.level < 0
              ? 1.0f
              : std::clamp(float(timeline.elapsed() - tex.created) / 100.0f,
                           0.0f, 1.0f);
      fading = fading || opacity < 1;
      glUniform1f(opacityUniform, opacity);
      float l = float(2 * r.left() / w - 1), rr = float(2 * r.right() / w - 1);
      float top = float(1 - 2 * r.top() / h),
            bottom = float(1 - 2 * r.bottom() / h);
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
    const QRectF viewport(0, 0, w, h);
    for (auto *texture : layers) {
      auto &tex = *texture;
      Tile t{tex.level, tex.x, tex.y, {}};
      QRectF r = tileRect(t);
      if (!r.intersects(viewport))
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
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    if (!fading)
      stopRefinement();
  }
  // CPU path: only when no OpenGL 3.3 context exists (remote/virtual hosts).
  void paintCpu(GtkSnapshot *snapshot, int w, int h) {
    const GdkRGBA backdrop{0.075f, 0.075f, 0.075f, 1.0f};
    graphene_rect_t all = GRAPHENE_RECT_INIT(0, 0, float(w), float(h));
    gtk_snapshot_append_color(snapshot, &backdrop, &all);
    if (valid() || cpuPreview.isNull() || dimensions.isEmpty())
      return;
    if (!cpuTexture) {
      auto *keep = new QImage(cpuPreview);
      GBytes *bytes = g_bytes_new_with_free_func(
          keep->constBits(), gsize(keep->sizeInBytes()),
          +[](gpointer data) { delete static_cast<QImage *>(data); }, keep);
      cpuTexture = gdk_memory_texture_new(
          keep->width(), keep->height(), GDK_MEMORY_R16G16B16A16, bytes,
          gsize(keep->bytesPerLine()));
      g_bytes_unref(bytes);
    }
    const QRectF r(offset, QSizeF(dimensions) * zoom);
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(
        float(r.x()), float(r.y()), float(r.width()), float(r.height()));
    gtk_snapshot_push_clip(snapshot, &all);
    gtk_snapshot_append_scaled_texture(snapshot, cpuTexture,
                                       GSK_SCALING_FILTER_LINEAR, &bounds);
    gtk_snapshot_pop(snapshot);
  }

  // ------------------------------------------------------- scheduling ---
  void request() {
    update();
    if (!settle.isActive())
      settle.start();
  }
  void schedule() {
    if (workInFlight || zoomAnimation.running() || failed || cancel->load())
      return;
    quint64 gen = generation;
    auto doc = document;
    auto token = cancel;
    if (pendingOpen) {
      pendingOpen = false;
      QString name = filename, icc = profile;
      int edge = q->previewEdge();
      auto viewport = q->previewViewport();
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
                          if (gen != generation || token->load() ||
                              !workInFlight)
                            return;
                          dimensions = preview.size;
                          upload({-1, 0, 0, preview.preview});
                          if (!valid() && !cpuOnly)
                            pendingPreview = preview.preview;
                          showHourglass(false);
                          emitStatus(preview.text);
                          q->fit();
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
    if (!valid()) {
      if (cpuOnly && !dimensions.isEmpty())
        emitStatus("Gotowe • podgląd CPU: OpenGL niedostępny. Spróbuj "
                   "PRISM_GPU=system albo PRISM_BACKEND=x11.");
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
    emitStatus(QString("Gotowe • RAM cache ≤256 MiB • GPU kafelki %1 MiB")
                   .arg(gpuBytes / 1048576));
  }
  void finishWork() {
    auto r = watcher.result();
    workInFlight = false;
    if (cancel->load())
      return;
    if (r.generation != generation) {
      schedule();
      return;
    }
    if (!r.error.isEmpty()) {
      setLoading(false);
      emitStatus(r.error);
      if (q->metadata)
        q->metadata(r.error);
      return;
    }
    if (r.loaded) {
      setLoading(false);
      bool first = dimensions.isEmpty();
      dimensions = r.info.size;
      upload({-1, 0, 0, r.info.preview});
      if (!valid() && !cpuOnly)
        pendingPreview = r.info.preview;
      if (q->metadata)
        q->metadata(r.info.text);
      if (first)
        q->fit();
    } else
      upload(r.tile);
    update();
    schedule();
  }
  void resized() {
    if (fitMode)
      q->fit();
    else
      request();
  }
};

PhotoView::PhotoView(const QString &accessibleName) : d(new Impl(this)) {
  auto *impl = d.get();
  d->pool.setMaxThreadCount(1);
  d->pool.setExpiryTimeout(-1);
  d->pool.setThreadPriority(QThread::NormalPriority);
  d->timeline.start();
  d->settle.setSingleShot(true);
  d->settle.setInterval(80);
  QObject::connect(&d->settle, &QTimer::timeout, impl,
                   [impl] { impl->schedule(); });
  QObject::connect(&d->watcher, &QFutureWatcher<WorkResult>::finished, impl,
                   [impl] { impl->finishWork(); });

  d->overlay = gtk_overlay_new();
  g_object_add_weak_pointer(G_OBJECT(d->overlay),
                            reinterpret_cast<gpointer *>(&d->overlay));
  Gx::addClass(d->overlay, "prism-stage");
  gtk_widget_set_hexpand(d->overlay, TRUE);
  gtk_widget_set_vexpand(d->overlay, TRUE);
  gtk_widget_set_overflow(d->overlay, GTK_OVERFLOW_HIDDEN);
  d->base = prism_canvas_new(
      120, 90, [impl](GtkSnapshot *s, int w, int h) { impl->paintCpu(s, w, h); });
  gtk_widget_set_focusable(d->base, TRUE);
  gtk_widget_set_focus_on_click(d->base, TRUE);
  gtk_widget_set_hexpand(d->base, TRUE);
  gtk_widget_set_vexpand(d->base, TRUE);
  Ui::accessible(d->base, accessibleName);
  gtk_overlay_set_child(GTK_OVERLAY(d->overlay), d->base);

  d->area = gtk_gl_area_new();
  gtk_gl_area_set_allowed_apis(
      GTK_GL_AREA(d->area), GdkGLAPI(GDK_GL_API_GL | GDK_GL_API_GLES));
  gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(d->area), FALSE);
  gtk_gl_area_set_has_stencil_buffer(GTK_GL_AREA(d->area), FALSE);
  gtk_gl_area_set_auto_render(GTK_GL_AREA(d->area), TRUE);
  gtk_widget_set_can_target(d->area, FALSE);
  gtk_widget_set_can_focus(d->area, FALSE);
  gtk_overlay_add_overlay(GTK_OVERLAY(d->overlay), d->area);
  Gx::on(d->area, "realize", [impl] { impl->initializeGL(); }, true);
  Gx::on(d->area, "unrealize", [impl] {
    impl->stopRefinement();
    impl->cleanGL();
  });
  Gx::connect(d->area, "render",
                   +[](GtkGLArea *, GdkGLContext *, gpointer data)
                                  -> gboolean {
                     static_cast<Impl *>(data)->paintGL();
                     return TRUE;
                   },
                   impl);
  Gx::connect(d->area, "resize",
                   +[](GtkGLArea *, int w, int h, gpointer data) {
                     auto *self = static_cast<Impl *>(data);
                     const bool changed =
                         self->fbWidth != w || self->fbHeight != h;
                     self->fbWidth = w;
                     self->fbHeight = h;
                     if (changed)
                       Gx::later([self, guard = self->alive] {
                         if (*guard)
                           self->resized();
                       });
                   },
                   impl);
  // Diagnostic switch: PRISM_GTK_GL=0 never creates an OpenGL context.
  if (qEnvironmentVariable("PRISM_GTK_GL") == "0") {
    d->cpuOnly = true;
    gtk_widget_set_visible(d->area, FALSE);
    Gx::later([impl, guard = impl->alive] {
      if (*guard)
        impl->useCpuFallback("PRISM_GTK_GL=0");
    });
  }

  d->hourglass = Gx::vbox(0);
  Gx::addClass(d->hourglass, "prism-card");
  gtk_widget_set_halign(d->hourglass, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(d->hourglass, GTK_ALIGN_CENTER);
  gtk_widget_set_can_target(d->hourglass, FALSE);
  auto *glass = prism_cairo_canvas_new(
      112, 96, [impl](cairo_t *cr, int w, int h, const GdkRGBA &) {
        const double phase = impl->loadingClock.isValid()
                                 ? (impl->loadingClock.elapsed() % 2200) / 2200.
                                 : 0;
        Icons::hourglass(cr, w, h, phase, GdkRGBA{0.92f, 0.93f, 0.95f, 1},
                         Theme::rgba(Theme::palette().accent));
      });
  gtk_box_append(GTK_BOX(d->hourglass), glass);
  auto *caption = Ui::label("Wczytywanie");
  gtk_label_set_xalign(GTK_LABEL(caption), 0.5f);
  gtk_box_append(GTK_BOX(d->hourglass), caption);
  Ui::accessible(d->hourglass, "Wczytywanie zdjęcia");
  gtk_widget_set_visible(d->hourglass, FALSE);
  gtk_overlay_add_overlay(GTK_OVERLAY(d->overlay), d->hourglass);
  Gx::on(d->hourglass, "map", [impl] { impl->syncHourglass(); }, true);
  Gx::on(d->hourglass, "unmap", [impl] { impl->syncHourglass(); }, true);

  // Without OpenGL the base canvas itself tracks size changes.
  auto *sizeWatch = d->sizeWatch = gtk_drawing_area_new();
  gtk_widget_set_can_target(sizeWatch, FALSE);
  gtk_widget_set_opacity(sizeWatch, 0);
  gtk_overlay_add_overlay(GTK_OVERLAY(d->overlay), sizeWatch);
  Gx::connect(sizeWatch, "resize",
                   +[](GtkDrawingArea *, int, int, gpointer data) {
                     auto *self = static_cast<Impl *>(data);
                     if (!self->valid())
                       Gx::later([self, guard = self->alive] {
                         if (*guard)
                           self->resized();
                       });
                   },
                   impl);

  auto *scroll = gtk_event_controller_scroll_new(
      GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  Gx::connect(scroll, "scroll",
                   +[](GtkEventControllerScroll *, double, double dy,
                                  gpointer data) -> gboolean {
                     auto *self = static_cast<Impl *>(data);
                     self->q->scale(std::exp(-dy * 0.18), self->pointer);
                     return TRUE;
                   },
                   impl);
  gtk_widget_add_controller(d->base, scroll);
  auto *motion = gtk_event_controller_motion_new();
  Gx::connect(motion, "motion",
                   +[](GtkEventControllerMotion *, double x,
                                  double y, gpointer data) {
                     static_cast<Impl *>(data)->pointer = QPointF(x, y);
                   },
                   impl);
  gtk_widget_add_controller(d->base, motion);
  auto *drag = gtk_gesture_drag_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
  Gx::connect(drag, "drag-begin",
                   +[](GtkGestureDrag *, double x, double y,
                                  gpointer data) {
                     auto *self = static_cast<Impl *>(data);
                     gtk_widget_grab_focus(self->base);
                     self->pointer = QPointF(x, y);
                     self->zoomAnimation.stop();
                     self->dragOrigin = self->offset;
                     self->dragging = true;
                     gtk_widget_set_cursor_from_name(self->base, "grabbing");
                   },
                   impl);
  Gx::connect(drag, "drag-update",
                   +[](GtkGestureDrag *, double dx, double dy,
                                  gpointer data) {
                     auto *self = static_cast<Impl *>(data);
                     if (!self->dragging || (dx == 0 && dy == 0))
                       return;
                     self->offset = self->dragOrigin + QPointF(dx, dy);
                     self->fitMode = false;
                     self->request();
                   },
                   impl);
  Gx::connect(drag, "drag-end",
                   +[](GtkGestureDrag *, double, double,
                                  gpointer data) {
                     auto *self = static_cast<Impl *>(data);
                     self->dragging = false;
                     self->offset = nativeAligned(self->offset, self->zoom,
                                                  self->dpr());
                     gtk_widget_set_cursor(self->base, nullptr);
                     self->request();
                   },
                   impl);
  gtk_widget_add_controller(d->base, GTK_EVENT_CONTROLLER(drag));
  auto *pinch = gtk_gesture_zoom_new();
  Gx::connect(pinch, "begin",
                   +[](GtkGesture *, GdkEventSequence *,
                                  gpointer data) {
                     static_cast<Impl *>(data)->pinchLast = 1.0;
                   },
                   impl);
  Gx::connect(pinch, "scale-changed",
                   +[](GtkGestureZoom *gesture, double scale,
                                  gpointer data) {
                     auto *self = static_cast<Impl *>(data);
                     if (scale <= 0 || self->dimensions.isEmpty())
                       return;
                     double cx = self->width() / 2.0;
                     double cy = self->height() / 2.0;
                     gtk_gesture_get_bounding_box_center(GTK_GESTURE(gesture),
                                                         &cx, &cy);
                     const double factor = scale / self->pinchLast;
                     self->pinchLast = scale;
                     self->zoomAnimation.stop();
                     const QPointF anchor(cx, cy);
                     const QPointF point = (anchor - self->offset) / self->zoom;
                     self->zoom = std::clamp(self->zoom * factor, 1.0 / 1048576,
                                             64.0 / self->dpr());
                     self->zoomTarget = self->zoom;
                     self->offset = anchor - point * self->zoom;
                     self->fitMode = false;
                     if (self->q->zoomChanged)
                       self->q->zoomChanged(self->zoom * self->dpr());
                     self->request();
                   },
                   impl);
  gtk_widget_add_controller(d->base, GTK_EVENT_CONTROLLER(pinch));
  auto *focus = gtk_event_controller_focus_new();
  Gx::on(focus, "enter", [this] {
    if (focused)
      focused();
  });
  gtk_widget_add_controller(d->base, focus);
}
PhotoView::~PhotoView() {
  stop();
  loadingChanged = {};
  status = metadata = gpuReady = {};
  zoomChanged = {};
  focused = {};
  d->watcher.disconnect(d.get());
  d->settle.stop();
  d->pool.waitForDone();
  d->document.reset();
  d->zoomAnimation.stop();
  d->stopRefinement();
  if (d->loadingTimer)
    g_source_remove(d->loadingTimer);
  d->loadingTimer = 0;
  d->cleanGL();
  g_clear_object(&d->cpuTexture);
  // Destroyed before its window: drop the canvas widgets so no controller or
  // GL signal can reach this object afterwards. The bare overlay stays.
  if (d->overlay) {
    g_object_remove_weak_pointer(G_OBJECT(d->overlay),
                                 reinterpret_cast<gpointer *>(&d->overlay));
    for (auto *child : {d->sizeWatch, d->hourglass, d->area})
      gtk_overlay_remove_overlay(GTK_OVERLAY(d->overlay), child);
    gtk_overlay_set_child(GTK_OVERLAY(d->overlay), nullptr);
  }
  d->area = d->base = d->hourglass = d->sizeWatch = nullptr;
}
GtkWidget *PhotoView::widget() const { return d->overlay; }
GtkWidget *PhotoView::focusWidget() const { return d->base; }
QString PhotoView::gpu() const { return d->gpuName; }
bool PhotoView::hardwareReady() const { return d->valid(); }
bool PhotoView::loading() const { return d->isLoading; }
bool PhotoView::loadingAnimating() const { return d->loadingTimer != 0; }
QSize PhotoView::imageSize() const { return d->dimensions; }
QSize PhotoView::logicalSize() const { return {d->width(), d->height()}; }
double PhotoView::devicePixelRatio() const { return d->dpr(); }
double PhotoView::zoomFactor() const { return d->zoom * d->dpr(); }
quint64 PhotoView::textureBytes() const { return d->gpuBytes; }
QRectF PhotoView::displayedImageRect() const {
  return QRectF(d->offset, QSizeF(d->dimensions) * d->zoom);
}
int PhotoView::previewEdge() const {
  return Document::previewBucket(int(
      std::ceil(std::max(d->width(), d->height()) * d->dpr())));
}
QSize PhotoView::previewViewport() const {
  return {int(std::ceil(d->width() * d->dpr())),
          int(std::ceil(d->height() * d->dpr()))};
}
void PhotoView::stop() {
  d->cancel->store(true);
  d->pendingOpen = false;
  d->setLoading(false);
  d->settle.stop();
  d->stopRefinement();
  d->zoomAnimation.stop();
}
bool PhotoView::busy() const {
  return d->workInFlight || d->pendingOpen || d->settle.isActive() ||
         d->refineTick != 0 || d->zoomAnimation.running();
}
void PhotoView::release() {
  stop();
  d->pendingOpen = false;
  d->dimensions = {};
  d->pendingPreview = {};
  d->setCpuPreview({});
  ++d->generation;
  d->dropTextures();
  // Clear decoder resources on its own serialized worker, without blocking UI.
  auto doc = d->document;
  (void)QtConcurrent::run(&d->pool, [doc] { doc->clear(); });
  d->update();
}
void PhotoView::open(const QString &path, const QString &displayProfile) {
  if (d->failed) {
    d->setLoading(false);
    d->emitStatus("GPU nie jest gotowe. Sprawdź obsługę OpenGL 3.3 i uruchom "
                  "program ponownie.");
    return;
  }
  d->zoomAnimation.stop();
  d->stopRefinement();
  d->pendingPreview = {};
  d->setCpuPreview({});
  d->cancel->store(true);
  d->cancel = std::make_shared<std::atomic_bool>(false);
  d->filename = path;
  d->profile = displayProfile;
  ++d->generation;
  d->pendingOpen = true;
  d->setLoading(true);
  d->dimensions = {};
  d->dropTextures();
  d->emitStatus("Odczyt i profilowanie kolorów…");
  d->update();
  d->schedule();
}
void PhotoView::fit() {
  if (d->dimensions.isEmpty() || d->width() <= 0 || d->height() <= 0)
    return;
  d->zoomAnimation.stop();
  d->fitMode = true;
  d->zoom = std::min(double(d->width()) / d->dimensions.width(),
                     double(d->height()) / d->dimensions.height());
  d->offset = {(d->width() - d->dimensions.width() * d->zoom) / 2,
               (d->height() - d->dimensions.height() * d->zoom) / 2};
  d->offset = nativeAligned(d->offset, d->zoom, d->dpr());
  if (zoomChanged)
    zoomChanged(d->zoom * d->dpr());
  d->request();
}
void PhotoView::actual() {
  if (d->dimensions.isEmpty())
    return;
  d->zoomAnimation.stop();
  d->fitMode = false;
  d->zoom = 1.0 / d->dpr();
  d->zoomTarget = d->zoom;
  d->offset = {(d->width() - d->dimensions.width() * d->zoom) / 2,
               (d->height() - d->dimensions.height() * d->zoom) / 2};
  d->offset = nativeAligned(d->offset, d->zoom, d->dpr());
  if (zoomChanged)
    zoomChanged(1.0);
  d->emitStatus("1:1 • jeden piksel zdjęcia na fizyczny piksel ekranu");
  d->request();
}
void PhotoView::scaleCentered(double factor) {
  scale(factor, QPointF(d->width() / 2.0, d->height() / 2.0));
}
void PhotoView::scale(double factor, QPointF anchor) {
  if (d->dimensions.isEmpty())
    return;
  d->fitMode = false;
  const double next =
      std::clamp((d->zoomAnimation.running() ? d->zoomTarget : d->zoom) * factor,
                 1.0 / 1048576, 64.0 / d->dpr());
  d->zoomAnimation.stop();
  d->zoomAnchor = anchor;
  d->zoomPoint = (anchor - d->offset) / d->zoom;
  d->zoomTarget = next;
  d->zoomFrom = std::log(d->zoom);
  d->zoomTo = std::log(next);
  auto *impl = d.get();
  auto apply = [this, impl](double t) {
    const double eased = 1 - std::pow(1 - t, 3); // OutCubic
    impl->zoom =
        std::exp(impl->zoomFrom + (impl->zoomTo - impl->zoomFrom) * eased);
    impl->offset = impl->zoomAnchor - impl->zoomPoint * impl->zoom;
    if (zoomChanged)
      zoomChanged(impl->zoom * impl->dpr());
    impl->update();
  };
  auto finish = [impl] {
    impl->offset = nativeAligned(impl->offset, impl->zoom, impl->dpr());
    impl->request();
  };
  if (!gtk_widget_get_mapped(d->base)) {
    apply(1.0);
    finish();
    return;
  }
  d->zoomAnimation.start(d->base, 110, apply, finish);
}
void PhotoView::pan(QPointF delta) {
  if (d->dimensions.isEmpty())
    return;
  d->zoomAnimation.stop();
  d->fitMode = false;
  d->offset += delta;
  d->offset = nativeAligned(d->offset, d->zoom, d->dpr());
  d->request();
}
QImage PhotoView::framebuffer() {
  if (!d->valid() || d->fbWidth <= 0 || d->fbHeight <= 0)
    return {};
  gtk_gl_area_make_current(GTK_GL_AREA(d->area));
  gtk_gl_area_attach_buffers(GTK_GL_AREA(d->area));
  QImage image(d->fbWidth, d->fbHeight, QImage::Format_RGBA8888);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, d->fbWidth, d->fbHeight, GL_RGBA, GL_UNSIGNED_BYTE,
               image.bits());
  return image.mirrored(false, true);
}
