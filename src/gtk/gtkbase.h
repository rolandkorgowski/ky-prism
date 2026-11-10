#pragma once
// GTK must be parsed before Qt's `signals`/`slots` keywords exist: GIO uses
// both words as ordinary identifiers. Include this header first everywhere.
#pragma push_macro("signals")
#pragma push_macro("slots")
#undef signals
#undef slots
#include <gtk/gtk.h>
#pragma pop_macro("slots")
#pragma pop_macro("signals")
#include <QByteArray>
#include <QString>
#include <functional>
#include <utility>

namespace Gx {
inline QByteArray u8(const QString &text) { return text.toUtf8(); }
inline QString qs(const char *text) {
  return text ? QString::fromUtf8(text) : QString();
}
using Callback = std::function<void()>;
inline void destroyCallback(gpointer data, GClosure *) {
  delete static_cast<Callback *>(data);
}
// Connect any void signal whose arguments are not needed. The swapped handler
// receives only the closure data; extra C arguments are ignored by the ABI.
inline gulong on(gpointer object, const char *signal, Callback run,
                 bool after = false) {
  return g_signal_connect_data(
      object, signal,
      G_CALLBACK(+[](gpointer data) { (*static_cast<Callback *>(data))(); }),
      new Callback(std::move(run)), destroyCallback,
      GConnectFlags(G_CONNECT_SWAPPED | (after ? G_CONNECT_AFTER : 0)));
}
// Typed signal connection as a real function: unlike the g_signal_connect
// macro it accepts handler lambdas whose bodies contain commas.
template <class Handler>
inline gulong connect(gpointer object, const char *signal, Handler handler,
                      gpointer data, bool after = false) {
  return g_signal_connect_data(object, signal,
                               reinterpret_cast<GCallback>(handler), data,
                               nullptr, after ? G_CONNECT_AFTER : GConnectFlags(0));
}
// One-shot main-loop callbacks.
inline guint later(Callback run, guint milliseconds = 0) {
  auto *boxed = new Callback(std::move(run));
  auto invoke = +[](gpointer data) -> gboolean {
    (*static_cast<Callback *>(data))();
    return G_SOURCE_REMOVE;
  };
  auto destroy = +[](gpointer data) { delete static_cast<Callback *>(data); };
  return milliseconds ? g_timeout_add_full(G_PRIORITY_DEFAULT, milliseconds,
                                           invoke, boxed, destroy)
                      : g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, invoke, boxed,
                                        destroy);
}
inline void addClass(GtkWidget *widget, const char *name) {
  gtk_widget_add_css_class(widget, name);
}
inline void setClass(GtkWidget *widget, const char *name, bool enabled) {
  if (enabled)
    gtk_widget_add_css_class(widget, name);
  else
    gtk_widget_remove_css_class(widget, name);
}
inline GtkWidget *hbox(int spacing = 0) {
  return gtk_box_new(GTK_ORIENTATION_HORIZONTAL, spacing);
}
inline GtkWidget *vbox(int spacing = 0) {
  return gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing);
}
inline void margins(GtkWidget *w, int left, int top, int right, int bottom) {
  gtk_widget_set_margin_start(w, left);
  gtk_widget_set_margin_top(w, top);
  gtk_widget_set_margin_end(w, right);
  gtk_widget_set_margin_bottom(w, bottom);
}
// Keyboard shortcut handled at `owner`. Capture-phase shortcuts run before the
// focused control and before built-in bindings (GtkPaned F6, Ctrl+Tab focus
// movement); bubble-phase ones only see keys the focused control ignored.
inline void shortcut(GtkWidget *owner, guint key, GdkModifierType modifiers,
                     Callback run, bool capture = false) {
  auto *controller = gtk_shortcut_controller_new();
  gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(controller),
                                    GTK_SHORTCUT_SCOPE_LOCAL);
  if (capture)
    gtk_event_controller_set_propagation_phase(controller, GTK_PHASE_CAPTURE);
  auto *action = gtk_callback_action_new(
      +[](GtkWidget *, GVariant *, gpointer data) -> gboolean {
        (*static_cast<Callback *>(data))();
        return TRUE;
      },
      new Callback(std::move(run)),
      +[](gpointer data) { delete static_cast<Callback *>(data); });
  gtk_shortcut_controller_add_shortcut(
      GTK_SHORTCUT_CONTROLLER(controller),
      gtk_shortcut_new(gtk_keyval_trigger_new(key, modifiers), action));
  gtk_widget_add_controller(owner, controller);
}
inline constexpr GdkModifierType NoModifier = GdkModifierType(0);
inline constexpr GdkModifierType Ctrl = GDK_CONTROL_MASK;
inline constexpr GdkModifierType Shift = GDK_SHIFT_MASK;
inline constexpr GdkModifierType Alt = GDK_ALT_MASK;
inline constexpr GdkModifierType CtrlShift =
    GdkModifierType(GDK_CONTROL_MASK | GDK_SHIFT_MASK);
inline constexpr GdkModifierType CtrlAlt =
    GdkModifierType(GDK_CONTROL_MASK | GDK_ALT_MASK);
} // namespace Gx

// Generic custom-drawn widget. One GObject type serves every vector element
// (icons, heart, flag, hourglass, thumbnails) through a snapshot callback.
using CanvasPaint = std::function<void(GtkSnapshot *, int width, int height)>;
GtkWidget *prism_canvas_new(int width, int height, CanvasPaint paint);
void prism_canvas_set_paint(GtkWidget *canvas, CanvasPaint paint);
// Cairo convenience: the callback draws with the widget's CSS colour as ink.
using CairoPaint =
    std::function<void(cairo_t *, int width, int height, const GdkRGBA &ink)>;
GtkWidget *prism_cairo_canvas_new(int width, int height, CairoPaint paint);
