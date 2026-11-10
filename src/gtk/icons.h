#pragma once
#include "gtkbase.h"
#include <vector>

// QPainter-like facade over cairo so the vector artwork keeps the geometry of
// the reviewed Qt drawings: resolution independent, no bitmaps, no fonts.
class Painter {
  cairo_t *cr;
  GdkRGBA pen{0, 0, 0, 1}, brush{0, 0, 0, 0};
  double width = 1.0;
  bool hasPen = true, hasBrush = false;
  void paint();

public:
  explicit Painter(cairo_t *context);
  cairo_t *context() const { return cr; }
  void save() { cairo_save(cr); }
  void restore() { cairo_restore(cr); }
  void translate(double x, double y) { cairo_translate(cr, x, y); }
  void scale(double x, double y) { cairo_scale(cr, x, y); }
  void rotate(double degrees);
  void setPen(const GdkRGBA &color, double lineWidth);
  void noPen() { hasPen = false; }
  void setBrush(const GdkRGBA &color);
  void noBrush() { hasBrush = false; }
  void roundedRect(double x, double y, double w, double h, double rx,
                   double ry = -1);
  void rect(double x, double y, double w, double h);
  void ellipse(double x, double y, double w, double h);
  void circle(double cx, double cy, double r) {
    ellipse(cx - r, cy - r, 2 * r, 2 * r);
  }
  void line(double x1, double y1, double x2, double y2);
  void point(double x, double y);
  void polyline(const std::vector<double> &xy);
  void polygon(const std::vector<double> &xy);
  // Arc in Qt convention: degrees counter-clockwise from 3 o'clock.
  void arc(double x, double y, double w, double h, double startDeg,
           double spanDeg);
  // Free-form path: build with cairo calls, then stroke/fill with pen/brush.
  void path(const std::function<void(cairo_t *)> &build);
};
inline GdkRGBA withAlpha(GdkRGBA color, double alpha) {
  color.alpha = float(color.alpha * alpha);
  return color;
}

// Small frame-clock animation: progress 0→1 over `milliseconds`, then stops.
// No timer exists while idle or while the widget is unmapped.
class Animation {
  GtkWidget *widget = nullptr;
  guint tick = 0;
  gint64 started = 0;
  double duration = 0, value = 1.0;
  std::function<void(double)> step;
  Gx::Callback finished;
  static gboolean frame(GtkWidget *, GdkFrameClock *, gpointer);

public:
  Animation() = default;
  Animation(const Animation &) = delete;
  ~Animation() { stop(); }
  void start(GtkWidget *owner, double milliseconds,
             std::function<void(double)> onStep, Gx::Callback onFinished = {});
  void stop();
  bool running() const { return tick != 0; }
  double progress() const { return value; }
};

namespace Icons {
void draw(cairo_t *, const QString &name, double x, double y, double size,
          const GdkRGBA &ink, const GdkRGBA &accent);
// Themed icon widget: ink follows the widget's CSS colour, accent the palette.
GtkWidget *create(const QString &name, int size = 20);
void rename(GtkWidget *icon, const QString &name);
QString nameOf(GtkWidget *icon);
void heart(cairo_t *, int width, int height, bool selected, double pulse,
           const GdkRGBA &ink);
void productMark(cairo_t *, int width, int height, double progress,
                 const GdkRGBA &ink, const GdkRGBA &accent);
void camera(cairo_t *, int width, int height, const GdkRGBA &ink);
void hourglass(cairo_t *, int width, int height, double phase,
               const GdkRGBA &ink, const GdkRGBA &accent);
void flag(cairo_t *, double x, double y, double w, double h, bool english);
void folderGlyph(cairo_t *, double cx, double cy, const GdkRGBA &ink);
void photoGlyph(cairo_t *, double cx, double cy, const GdkRGBA &ink);
} // namespace Icons
