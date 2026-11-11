#include "icons.h"
#include "theme.h"
#include <algorithm>
#include <cmath>

// --------------------------------------------------------------- canvas ---
struct PrismCanvas {
  GtkWidget parent;
  CanvasPaint *paint;
  int width, height;
};
struct PrismCanvasClass {
  GtkWidgetClass parent;
};
G_DEFINE_TYPE(PrismCanvas, prism_canvas, GTK_TYPE_WIDGET)
static void prism_canvas_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  auto *self = reinterpret_cast<PrismCanvas *>(widget);
  const int w = gtk_widget_get_width(widget),
            h = gtk_widget_get_height(widget);
  if (self->paint && *self->paint && w > 0 && h > 0)
    (*self->paint)(snapshot, w, h);
}
static void prism_canvas_measure(GtkWidget *widget, GtkOrientation orientation,
                                 int, int *minimum, int *natural,
                                 int *minimumBaseline, int *naturalBaseline) {
  auto *self = reinterpret_cast<PrismCanvas *>(widget);
  *minimum = *natural =
      orientation == GTK_ORIENTATION_HORIZONTAL ? self->width : self->height;
  *minimumBaseline = *naturalBaseline = -1;
}
static void prism_canvas_finalize(GObject *object) {
  auto *self = reinterpret_cast<PrismCanvas *>(object);
  delete self->paint;
  self->paint = nullptr;
  G_OBJECT_CLASS(prism_canvas_parent_class)->finalize(object);
}
static void prism_canvas_class_init(PrismCanvasClass *klass) {
  G_OBJECT_CLASS(klass)->finalize = prism_canvas_finalize;
  GTK_WIDGET_CLASS(klass)->snapshot = prism_canvas_snapshot;
  GTK_WIDGET_CLASS(klass)->measure = prism_canvas_measure;
}
static void prism_canvas_init(PrismCanvas *self) {
  self->paint = nullptr;
  self->width = self->height = 0;
}
GtkWidget *prism_canvas_new(int width, int height, CanvasPaint paint) {
  auto *self = reinterpret_cast<PrismCanvas *>(
      g_object_new(prism_canvas_get_type(), nullptr));
  self->width = width;
  self->height = height;
  self->paint = new CanvasPaint(std::move(paint));
  return GTK_WIDGET(self);
}
void prism_canvas_set_paint(GtkWidget *canvas, CanvasPaint paint) {
  auto *self = reinterpret_cast<PrismCanvas *>(canvas);
  delete self->paint;
  self->paint = new CanvasPaint(std::move(paint));
  gtk_widget_queue_draw(canvas);
}
GtkWidget *prism_cairo_canvas_new(int width, int height, CairoPaint paint) {
  auto *holder = new GtkWidget *(nullptr);
  auto *canvas = prism_canvas_new(
      width, height,
      [holder, paint = std::move(paint)](GtkSnapshot *snapshot, int w, int h) {
        graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, float(w), float(h));
        cairo_t *cr = gtk_snapshot_append_cairo(snapshot, &bounds);
        GdkRGBA ink{1, 1, 1, 1};
        if (*holder)
          gtk_widget_get_color(*holder, &ink);
        paint(cr, w, h, ink);
        cairo_destroy(cr);
      });
  *holder = canvas;
  g_object_weak_ref(
      G_OBJECT(canvas),
      +[](gpointer data, GObject *) { delete static_cast<GtkWidget **>(data); },
      holder);
  Theme::redrawOnChange(canvas);
  return canvas;
}

// -------------------------------------------------------------- painter ---
namespace {
const double pi = std::acos(-1.0);
void source(cairo_t *cr, const GdkRGBA &c) {
  cairo_set_source_rgba(cr, c.red, c.green, c.blue, c.alpha);
}
} // namespace
Painter::Painter(cairo_t *context) : cr(context) {
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_GOOD);
}
void Painter::rotate(double degrees) { cairo_rotate(cr, degrees * pi / 180); }
void Painter::setPen(const GdkRGBA &color, double lineWidth) {
  pen = color;
  width = lineWidth;
  hasPen = true;
}
void Painter::setBrush(const GdkRGBA &color) {
  brush = color;
  hasBrush = true;
}
void Painter::paint() {
  if (hasBrush) {
    source(cr, brush);
    if (hasPen)
      cairo_fill_preserve(cr);
    else
      cairo_fill(cr);
  }
  if (hasPen) {
    source(cr, pen);
    cairo_set_line_width(cr, width);
    cairo_stroke(cr);
  }
  cairo_new_path(cr);
}
void Painter::roundedRect(double x, double y, double w, double h, double rx,
                          double ry) {
  if (ry < 0)
    ry = rx;
  const double r = std::min({rx, ry, w / 2, h / 2});
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -pi / 2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, pi / 2);
  cairo_arc(cr, x + r, y + h - r, r, pi / 2, pi);
  cairo_arc(cr, x + r, y + r, r, pi, 3 * pi / 2);
  cairo_close_path(cr);
  paint();
}
void Painter::rect(double x, double y, double w, double h) {
  cairo_rectangle(cr, x, y, w, h);
  paint();
}
void Painter::ellipse(double x, double y, double w, double h) {
  cairo_save(cr);
  cairo_translate(cr, x + w / 2, y + h / 2);
  cairo_scale(cr, w / 2, h / 2);
  cairo_new_sub_path(cr);
  cairo_arc(cr, 0, 0, 1, 0, 2 * pi);
  cairo_close_path(cr);
  cairo_restore(cr);
  paint();
}
void Painter::line(double x1, double y1, double x2, double y2) {
  cairo_move_to(cr, x1, y1);
  cairo_line_to(cr, x2, y2);
  const bool fill = hasBrush;
  hasBrush = false;
  paint();
  hasBrush = fill;
}
void Painter::point(double x, double y) {
  if (!hasPen)
    return;
  source(cr, pen);
  cairo_arc(cr, x, y, width / 2, 0, 2 * pi);
  cairo_fill(cr);
}
void Painter::polyline(const std::vector<double> &xy) {
  for (size_t i = 0; i + 1 < xy.size(); i += 2)
    if (i == 0)
      cairo_move_to(cr, xy[i], xy[i + 1]);
    else
      cairo_line_to(cr, xy[i], xy[i + 1]);
  const bool fill = hasBrush;
  hasBrush = false;
  paint();
  hasBrush = fill;
}
void Painter::polygon(const std::vector<double> &xy) {
  for (size_t i = 0; i + 1 < xy.size(); i += 2)
    if (i == 0)
      cairo_move_to(cr, xy[i], xy[i + 1]);
    else
      cairo_line_to(cr, xy[i], xy[i + 1]);
  cairo_close_path(cr);
  paint();
}
void Painter::arc(double x, double y, double w, double h, double startDeg,
                  double spanDeg) {
  cairo_save(cr);
  cairo_translate(cr, x + w / 2, y + h / 2);
  cairo_scale(cr, w / 2, h / 2);
  cairo_new_sub_path(cr);
  // Qt angles grow counter-clockwise; cairo's y axis points down.
  const double a0 = -startDeg * pi / 180, a1 = -(startDeg + spanDeg) * pi / 180;
  if (spanDeg >= 0)
    cairo_arc_negative(cr, 0, 0, 1, a0, a1);
  else
    cairo_arc(cr, 0, 0, 1, a0, a1);
  cairo_restore(cr);
  const bool fill = hasBrush;
  hasBrush = false;
  paint();
  hasBrush = fill;
}
void Painter::path(const std::function<void(cairo_t *)> &build) {
  cairo_new_path(cr);
  build(cr);
  paint();
}

// ------------------------------------------------------------ animation ---
gboolean Animation::frame(GtkWidget *, GdkFrameClock *clock, gpointer data) {
  auto *self = static_cast<Animation *>(data);
  const double elapsed =
      double(gdk_frame_clock_get_frame_time(clock) - self->started) / 1000.0;
  self->value = self->duration <= 0
                    ? 1.0
                    : std::clamp(elapsed / self->duration, 0.0, 1.0);
  if (self->step)
    self->step(self->value);
  if (self->value < 1.0)
    return G_SOURCE_CONTINUE;
  self->tick = 0;
  auto done = self->finished;
  if (done)
    done();
  return G_SOURCE_REMOVE;
}
void Animation::start(GtkWidget *owner, double milliseconds,
                      std::function<void(double)> onStep,
                      Gx::Callback onFinished) {
  stop();
  widget = owner;
  duration = milliseconds;
  step = std::move(onStep);
  finished = std::move(onFinished);
  value = 0.0;
  auto *clock = gtk_widget_get_frame_clock(owner);
  started = clock ? gdk_frame_clock_get_frame_time(clock) : g_get_monotonic_time();
  g_object_add_weak_pointer(G_OBJECT(owner),
                            reinterpret_cast<gpointer *>(&widget));
  tick = gtk_widget_add_tick_callback(owner, frame, this, nullptr);
}
void Animation::stop() {
  if (widget) {
    if (tick)
      gtk_widget_remove_tick_callback(widget, tick);
    g_object_remove_weak_pointer(G_OBJECT(widget),
                                 reinterpret_cast<gpointer *>(&widget));
  }
  tick = 0;
  widget = nullptr;
}

// ---------------------------------------------------------------- icons ---
namespace Icons {
void draw(cairo_t *cr, const QString &name, double x, double y, double size,
          const GdkRGBA &ink, const GdkRGBA &accentColor) {
  Painter p(cr);
  p.save();
  p.translate(x, y);
  p.scale(size / 24.0, size / 24.0);
  const GdkRGBA accent = accentColor;
  const GdkRGBA soft = withAlpha(accent, 110 / 255.0),
                paper = withAlpha(ink, 35 / 255.0),
                shade = withAlpha(ink, 80 / 255.0);
  p.setPen(ink, 1.55);
  p.noBrush();
  if (name == "openPhoto") {
    p.setPen(ink, 1.2);
    p.setBrush(soft);
    p.roundedRect(2, 6, 20, 15, 3.5);
    p.setBrush(shade);
    p.roundedRect(5, 3, 5, 3, 1);
    p.setBrush(paper);
    p.roundedRect(4, 8, 5, 3, 1);
    p.setBrush(ink);
    p.ellipse(10, 9, 10, 10);
    GdkRGBA lens = accent;
    lens.red = std::min(1.0f, lens.red * 1.4f);
    lens.green = std::min(1.0f, lens.green * 1.4f);
    lens.blue = std::min(1.0f, lens.blue * 1.4f);
    p.setPen(lens, 1.5);
    p.noBrush();
    p.ellipse(12, 11, 6, 6);
    p.noPen();
    p.setBrush(GdkRGBA{1, 1, 1, 210 / 255.0f});
    p.ellipse(13, 11.5, 2, 2);
    p.setPen(ink, 1.1);
    p.line(4, 14, 7, 14);
  } else if (name == "bookmarks" || name == "favorites") {
    p.setPen(ink, 1.1);
    p.translate(12, 12);
    p.rotate(-7);
    p.translate(-12, -12);
    p.setBrush(paper);
    p.roundedRect(5, 2, 14, 19, 2);
    p.setBrush(soft);
    p.roundedRect(4, 4, 15, 18, 2);
    p.line(7, 4, 7, 21);
    if (name == "bookmarks") {
      p.setBrush(accent);
      p.noPen();
      p.polygon({12, 3, 16, 3, 16, 12, 14, 10, 12, 12});
      p.setPen(ink, 1);
      p.line(10, 16, 16, 16);
      p.line(10, 18, 14, 18);
    } else {
      p.noPen();
      p.setBrush(accent);
      p.path([](cairo_t *c) {
        cairo_move_to(c, 13, 17);
        cairo_curve_to(c, 4, 11, 11, 8, 13, 11);
        cairo_curve_to(c, 16, 8, 21, 11, 13, 17);
      });
    }
  } else if (name == "copyFiles" || name == "duplicates") {
    p.setBrush(soft);
    p.roundedRect(8, 8, 12, 13, 2.5);
    p.noBrush();
    p.polyline({15, 5, 15, 3, 4, 3, 4, 16, 6, 16});
    if (name == "duplicates") {
      p.line(11, 13, 17, 13);
      p.line(11, 16, 17, 16);
    }
  } else if (name == "openUsb") {
    p.setBrush(soft);
    p.roundedRect(6, 9, 12, 12, 3);
    p.setBrush(paper);
    p.rect(8, 2, 8, 7);
    p.line(11, 4, 11, 6);
    p.line(14, 4, 14, 6);
    p.line(10, 13, 14, 13);
    p.line(12, 13, 12, 18);
  } else if (name == "comparePhotos") {
    p.roundedRect(2, 4, 20, 16, 3);
    p.line(12, 4, 12, 20);
    p.polyline({4, 16, 7, 12, 10, 16});
    p.polyline({14, 16, 17, 12, 20, 16});
  } else if (name == "folder" || name == "newFolder") {
    p.path([](cairo_t *c) {
      cairo_move_to(c, 3, 19);
      cairo_line_to(c, 3, 6);
      cairo_curve_to(c, 3, 4.67, 3.67, 4, 5, 4);
      cairo_line_to(c, 9, 4);
      cairo_line_to(c, 12, 7);
      cairo_line_to(c, 19, 7);
      cairo_curve_to(c, 20.33, 7, 21, 7.67, 21, 9);
      cairo_line_to(c, 21, 19);
      cairo_close_path(c);
    });
    p.line(4, 11, 20, 11);
    if (name == "newFolder") {
      p.line(12, 13, 12, 17);
      p.line(10, 15, 14, 15);
    }
  } else if (name == "sidebar") {
    p.roundedRect(3, 4, 18, 16, 3);
    p.line(9, 4, 9, 20);
    p.line(5, 8, 7, 8);
    p.line(5, 12, 7, 12);
  } else if (name == "previous" || name == "next" || name == "up") {
    p.translate(12, 12);
    if (name == "next")
      p.rotate(180);
    if (name == "up")
      p.rotate(90);
    p.polyline({3, -6, -3, 0, 3, 6});
  } else if (name == "arrowLeft" || name == "arrowRight" ||
             name == "arrowUp" || name == "arrowDown") {
    p.translate(12, 12);
    if (name == "arrowRight")
      p.rotate(180);
    if (name == "arrowUp")
      p.rotate(90);
    if (name == "arrowDown")
      p.rotate(-90);
    p.line(-7, 0, 7, 0);
    p.polyline({-2, -5, -7, 0, -2, 5});
  } else if (name == "zoomOut" || name == "zoomIn") {
    p.setBrush(paper);
    p.ellipse(3, 3, 13, 13);
    p.setPen(accent, 2.5);
    p.line(14, 14, 21, 21);
    p.setPen(ink, 1.55);
    p.line(6, 9, 13, 9);
    if (name == "zoomIn")
      p.line(9.5, 5.5, 9.5, 12.5);
  } else if (name == "fit" || name == "fullscreen") {
    p.polyline({3, 9, 3, 3, 9, 3});
    p.polyline({15, 3, 21, 3, 21, 9});
    p.polyline({21, 15, 21, 21, 15, 21});
    p.polyline({9, 21, 3, 21, 3, 15});
    if (name == "fit")
      p.roundedRect(7, 8, 10, 8, 1);
  } else if (name == "actual") {
    p.roundedRect(3, 4, 18, 16, 3);
    p.polyline({6, 10, 8, 8, 8, 16});
    p.polyline({15, 10, 17, 8, 17, 16});
    p.point(12, 10);
    p.point(12, 14);
  } else if (name == "close") {
    p.line(6, 6, 18, 18);
    p.line(18, 6, 6, 18);
  } else if (name == "tabClose") {
    p.line(8, 8, 16, 16);
    p.line(16, 8, 8, 16);
  } else if (name == "minimize") {
    p.line(6, 17, 18, 17);
  } else if (name == "maximize" || name == "restore") {
    if (name == "restore") {
      p.roundedRect(8, 3, 13, 13, 2);
      p.roundedRect(3, 8, 13, 13, 2);
    } else
      p.roundedRect(4, 4, 16, 16, 3);
  } else if (name == "about" || name == "exif") {
    p.ellipse(3, 3, 18, 18);
    p.point(12, 7);
    p.line(12, 11, 12, 17);
  } else if (name == "newTab") {
    p.roundedRect(3, 3, 18, 18, 3);
    p.line(7, 12, 17, 12);
    p.line(12, 7, 12, 17);
  } else if (name == "list") {
    for (int row : {6, 12, 18}) {
      p.point(4, row);
      p.line(9, row, 20, row);
    }
  } else if (name == "grid") {
    for (int gx : {3, 14})
      for (int gy : {3, 14})
        p.roundedRect(gx, gy, 7, 7, 1.5);
  } else if (name == "star" || name == "starFilled") {
    if (name == "starFilled")
      p.setBrush(accent);
    p.polygon({12, 2, 15, 8, 22, 9, 17, 14, 18, 21,
               12, 18, 6, 21, 7, 14, 2, 9, 9, 8});
  } else if (name == "trash") {
    p.line(4, 6, 20, 6);
    p.line(9, 3, 15, 3);
    p.polyline({6, 6, 7, 21, 17, 21, 18, 6});
    p.line(10, 10, 10, 17);
    p.line(14, 10, 14, 17);
  } else if (name == "addPhoto") {
    p.line(12, 5, 12, 19);
    p.line(5, 12, 19, 12);
  } else if (name == "save") {
    p.roundedRect(3, 3, 18, 18, 3);
    p.rect(7, 3, 10, 6);
    p.rect(7, 14, 10, 7);
  } else if (name == "opacity") {
    // Half-filled disc: the adjustable density of the window material.
    p.ellipse(3, 3, 18, 18);
    p.noPen();
    p.setBrush(soft);
    p.path([](cairo_t *c) {
      cairo_arc(c, 12, 12, 9, -pi / 2, pi / 2);
      cairo_close_path(c);
    });
    p.setPen(ink, 1.2);
    p.line(12, 3, 12, 21);
    p.line(15, 8, 18.5, 8);
    p.line(15, 12, 20, 12);
    p.line(15, 16, 18.5, 16);
  } else if (name == "clipboard") {
    // Clipboard board with a small landscape: "copy the picture".
    p.setBrush(soft);
    p.roundedRect(4, 4, 16, 18, 2.5);
    p.setBrush(paper);
    p.roundedRect(8.5, 2, 7, 4, 1.5);
    p.noBrush();
    p.polyline({7, 18, 10.5, 13.5, 13, 16, 14.5, 14.5, 17, 18});
    p.circle(15, 10.5, 1.3);
  } else if (name == "export") {
    // Tray with an upward arrow: a copy of the file leaves for a new place.
    p.polyline({4, 14, 4, 20, 20, 20, 20, 14});
    p.line(12, 16, 12, 3.5);
    p.polyline({7.5, 8, 12, 3.5, 16.5, 8});
  } else if (name == "heart") {
    p.path([](cairo_t *c) {
      cairo_move_to(c, 12, 20);
      cairo_curve_to(c, 2, 13, 4, 4, 12, 8);
      cairo_curve_to(c, 20, 4, 22, 13, 12, 20);
      cairo_close_path(c);
    });
  } else if (name == "search") {
    p.ellipse(4, 4, 12, 12);
    p.line(14.5, 14.5, 20, 20);
  } else if (name == "refresh") {
    p.arc(4, 4, 16, 16, 40, 280);
    p.polyline({19, 3, 19, 8, 14, 8});
  }
  p.restore();
}
namespace {
const char *nameKey = "prism-icon-name";
}
GtkWidget *create(const QString &name, int size) {
  auto *holder = new GtkWidget *(nullptr);
  auto *canvas = prism_cairo_canvas_new(
      size, size,
      [holder](cairo_t *cr, int w, int h, const GdkRGBA &ink) {
        const auto *stored = *holder ? static_cast<const char *>(g_object_get_data(
                                           G_OBJECT(*holder), nameKey))
                                     : nullptr;
        const double edge = std::min(w, h);
        draw(cr, Gx::qs(stored), (w - edge) / 2, (h - edge) / 2, edge, ink,
             Theme::rgba(Theme::palette().accent));
      });
  *holder = canvas;
  g_object_weak_ref(
      G_OBJECT(canvas),
      +[](gpointer data, GObject *) { delete static_cast<GtkWidget **>(data); },
      holder);
  g_object_set_data_full(G_OBJECT(canvas), nameKey,
                         g_strdup(Gx::u8(name).constData()), g_free);
  gtk_widget_set_halign(canvas, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(canvas, GTK_ALIGN_CENTER);
  gtk_widget_set_can_target(canvas, FALSE);
  return canvas;
}
void rename(GtkWidget *icon, const QString &name) {
  g_object_set_data_full(G_OBJECT(icon), nameKey,
                         g_strdup(Gx::u8(name).constData()), g_free);
  gtk_widget_queue_draw(icon);
}
QString nameOf(GtkWidget *icon) {
  return Gx::qs(
      static_cast<const char *>(g_object_get_data(G_OBJECT(icon), nameKey)));
}
void heart(cairo_t *cr, int width, int height, bool selected, double pulse,
           const GdkRGBA &ink) {
  Painter p(cr);
  p.save();
  p.translate(width / 2.0, height / 2.0);
  const GdkRGBA red{226 / 255.0f, 70 / 255.0f, 89 / 255.0f, 1};
  GdkRGBA color = selected ? red : ink;
  if (pulse >= 0 && pulse < 1) {
    p.setPen(withAlpha(red, 1 - pulse), 1.0);
    for (int i = 0; i < 10; ++i) {
      const double angle = i * 2 * pi / 10, r = 11 + pulse * 5;
      p.line(std::cos(angle) * r, std::sin(angle) * r,
             std::cos(angle) * (r + 2), std::sin(angle) * (r + 2));
    }
    const double beat = 1 + 0.12 * std::sin(pulse * pi);
    p.scale(beat, beat);
  }
  p.setPen(color, 1.5);
  if (selected)
    p.setBrush(color);
  else
    p.noBrush();
  p.path([](cairo_t *c) {
    cairo_move_to(c, 0, 9);
    cairo_curve_to(c, -3, 6, -10, 1, -10, -4);
    cairo_curve_to(c, -10, -11, -3, -12, 0, -6);
    cairo_curve_to(c, 3, -12, 10, -11, 10, -4);
    cairo_curve_to(c, 10, 1, 3, 6, 0, 9);
    cairo_close_path(c);
  });
  p.restore();
}
void productMark(cairo_t *cr, int width, int height, double progress,
                 const GdkRGBA &ink, const GdkRGBA &accent) {
  Painter p(cr);
  p.save();
  p.translate(width / 2.0, height / 2.0);
  const double scale = std::min(width, height) / 170.0;
  p.scale(scale, scale);
  const double opacity = 0.25 + 0.75 * progress;
  cairo_push_group(cr);
  p.rotate(-28 * (1 - progress));
  for (int i = 0; i < 7; ++i) {
    p.save();
    p.rotate(i * 360.0 / 7);
    p.setPen(ink, 1.3);
    p.setBrush(withAlpha(accent, (18 + i * 4) / 255.0));
    p.path([progress](cairo_t *c) {
      cairo_move_to(c, 0, -60);
      cairo_curve_to(c, 32, -56, 57, -17, 36, 17);
      cairo_line_to(c, 15 + progress * 13, 5);
      cairo_curve_to(c, 30, -23, 18, -40, 0, -60);
      cairo_close_path(c);
    });
    p.restore();
  }
  p.noBrush();
  p.setPen(accent, 1.6);
  p.circle(0, 0, 66);
  p.setPen(ink, 0.8);
  p.circle(0, 0, 73);
  if (progress < 1) {
    p.setPen(withAlpha(accent, (1 - progress) * 0.8), 2);
    p.arc(-78, -78, 156, 156, progress * 300, 65);
  }
  cairo_pop_group_to_source(cr);
  cairo_paint_with_alpha(cr, opacity);
  p.restore();
}
void camera(cairo_t *cr, int width, int height, const GdkRGBA &ink) {
  Painter p(cr);
  p.save();
  p.translate(width / 2.0, height / 2.0);
  p.scale(0.8, 0.8);
  p.setPen(ink, 2.5);
  p.setBrush(withAlpha(ink, 16 / 255.0));
  p.roundedRect(-90, -48, 180, 108, 18);
  p.roundedRect(-30, -65, 60, 17, 5);
  p.noBrush();
  p.circle(0, 5, 40);
  p.circle(0, 5, 31);
  p.setBrush(ink);
  p.ellipse(-15, -5, 6, 10);
  p.ellipse(9, -5, 6, 10);
  p.noBrush();
  p.arc(-14, 4, 28, 20, 200, 140);
  p.roundedRect(56, -31, 20, 10, 3);
  p.line(-70, -24, -51, -24);
  p.line(-61, -33, -61, -15);
  p.restore();
}
void hourglass(cairo_t *cr, int width, int, double phase, const GdkRGBA &ink,
               const GdkRGBA &accent) {
  Painter p(cr);
  p.save();
  p.translate(width / 2.0, 52);
  const double turn = std::clamp((phase - .82) / .18, 0., 1.);
  const double sand = std::min(phase / .82, 1.);
  p.rotate(180 * turn * turn * (3 - 2 * turn));
  auto outline = [](cairo_t *c) {
    cairo_move_to(c, -18, -25);
    cairo_curve_to(c, -18, -10, -4, -8, -3, 0);
    cairo_curve_to(c, -4, 8, -18, 10, -18, 25);
    cairo_line_to(c, 18, 25);
    cairo_curve_to(c, 18, 10, 4, 8, 3, 0);
    cairo_curve_to(c, 4, -8, 18, -10, 18, -25);
    cairo_close_path(c);
  };
  p.setPen(ink, 1.4);
  p.setBrush(withAlpha(ink, 12 / 255.0));
  p.path(outline);
  cairo_save(cr);
  cairo_new_path(cr);
  outline(cr);
  cairo_clip(cr);
  GdkRGBA bright = accent;
  bright.red = std::min(1.0f, bright.red * 1.45f);
  bright.green = std::min(1.0f, bright.green * 1.45f);
  bright.blue = std::min(1.0f, bright.blue * 1.45f);
  bright.alpha = 230 / 255.0f;
  const GdkRGBA grain = withAlpha(accent, 210 / 255.0);
  auto *gradient = cairo_pattern_create_linear(0, -24, 0, 24);
  cairo_pattern_add_color_stop_rgba(gradient, 0, bright.red, bright.green,
                                    bright.blue, bright.alpha);
  cairo_pattern_add_color_stop_rgba(gradient, 1, grain.red, grain.green,
                                    grain.blue, grain.alpha);
  cairo_set_source(cr, gradient);
  cairo_rectangle(cr, -19, -24 + sand * 24, 38, 24 * (1 - sand));
  cairo_fill(cr);
  cairo_move_to(cr, -19, 26);
  cairo_line_to(cr, 0, 24 - sand * 23);
  cairo_line_to(cr, 19, 26);
  cairo_close_path(cr);
  cairo_fill(cr);
  cairo_pattern_destroy(gradient);
  if (turn == 0) {
    p.setPen(bright, 1.2);
    p.line(0, 0, 0, 24 - sand * 23);
    for (int i = 0; i < 3; ++i)
      p.point(i % 2 ? .8 : -.8, std::fmod(phase * 100 + i * 7, 22.));
  }
  cairo_restore(cr);
  p.setPen(ink, 2.3);
  p.line(-21, -27, 21, -27);
  p.line(-21, 27, 21, 27);
  p.restore();
}
void flag(cairo_t *cr, double x, double y, double w, double h, bool english) {
  cairo_save(cr);
  cairo_rectangle(cr, x, y, w, h);
  cairo_clip(cr);
  auto fill = [&](double fx, double fy, double fw, double fh, const char *hex) {
    GdkRGBA c;
    gdk_rgba_parse(&c, hex);
    source(cr, c);
    cairo_rectangle(cr, fx, fy, fw, fh);
    cairo_fill(cr);
  };
  auto diagonal = [&](const char *hex, double lw) {
    GdkRGBA c;
    gdk_rgba_parse(&c, hex);
    source(cr, c);
    cairo_set_line_width(cr, lw);
    cairo_move_to(cr, x, y);
    cairo_line_to(cr, x + w, y + h);
    cairo_move_to(cr, x + w, y);
    cairo_line_to(cr, x, y + h);
    cairo_stroke(cr);
  };
  const double cx = x + w / 2, cy = y + h / 2;
  if (english) {
    fill(x, y, w, h, "#163b72");
    diagonal("#ffffff", h * .24);
    diagonal("#c8253b", h * .08);
    fill(x, cy - h * .17, w, h * .34, "#ffffff");
    fill(cx - w * .12, y, w * .24, h, "#ffffff");
    fill(x, cy - h * .095, w, h * .19, "#c8253b");
    fill(cx - w * .065, y, w * .13, h, "#c8253b");
  } else {
    fill(x, y, w, h, "#ffffff");
    fill(x, cy, w, h / 2, "#dc2943");
  }
  cairo_restore(cr);
}
void folderGlyph(cairo_t *cr, double cx, double cy, const GdkRGBA &ink) {
  Painter p(cr);
  p.setPen(ink, 1.4);
  p.setBrush(withAlpha(ink, 0.08));
  p.roundedRect(cx - 20, cy - 10, 40, 25, 4);
  p.line(cx - 18, cy - 14, cx - 3, cy - 14);
}
void photoGlyph(cairo_t *cr, double cx, double cy, const GdkRGBA &ink) {
  Painter p(cr);
  p.setPen(ink, 1.4);
  p.noBrush();
  p.roundedRect(cx - 18, cy - 15, 36, 30, 4);
  p.polyline({cx - 13, cy + 8, cx - 4, cy - 3, cx + 3, cy + 5, cx + 10, cy - 5,
              cx + 15, cy + 8});
}
} // namespace Icons
