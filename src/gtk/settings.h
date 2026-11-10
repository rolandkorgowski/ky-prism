#pragma once
// Persistent interface preferences. QtCore only: testable without a display.
#include <QByteArray>
#include <QString>

namespace PrismSettings {
// Lowest value offered by the in-app slider. Lower values remain accepted
// from PRISM_UI_OPACITY / appearance.json for compatibility with 0.12.
constexpr int minimumSliderOpacity = 35;
QString appearanceFile();
// 0–100, or -1 when neither the environment nor the file has a valid value.
int storedOpacity(const QByteArray &environment, const QString &file);
// Compositors with background blur keep the lighter 0.12 material; without
// blur a translucent panel over arbitrary windows is unreadable.
int defaultOpacity(bool compositorBlurs);
// Atomic write that preserves unrelated keys already present in the file.
bool saveOpacity(const QString &file, int percent);
// Chrome alpha (0.0–1.0) used by the stylesheet for a 0–100 percent value.
double alphaFor(int percent);
} // namespace PrismSettings
