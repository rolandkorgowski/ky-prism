#pragma once
#include "gtkbase.h"
#include <QColor>

// Palette, translucent-material stylesheet and the adjustable chrome opacity.
// The photograph is never styled from here: its canvas stays fully opaque.
namespace Theme {
struct Palette {
  QColor background, foreground, accent;
  bool dark = true, omarchy = false;
};
// Install the display-wide stylesheet and start following the desktop theme.
void install();
void shutdown();
const Palette &palette();
GdkRGBA rgba(const QColor &color, double alpha = 1.0);
// Chrome opacity in percent (0–100).
int opacity();
void setOpacity(int percent, bool persist);
bool opacityPersisted();
bool compositorBlurs();
// Re-read colours now; `toml` overrides the Omarchy palette file (tests).
void reload(const QString &toml = {});
// Custom-drawn widgets are redrawn after a palette change.
void redrawOnChange(GtkWidget *widget);
void onChange(GtkWidget *owner, Gx::Callback run);
QString stylesheet(const Palette &palette, int opacityPercent);
Palette paletteFromToml(const QString &toml, const Palette &fallback);
} // namespace Theme
