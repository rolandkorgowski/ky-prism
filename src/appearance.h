#pragma once
#include <QByteArray>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>
#include <cmath>

namespace PrismAppearance {
// Read once at startup; never read configuration in a repaint loop.
// -1 preserves the original material when no valid override is present.
inline int readOpacity(const QByteArray &environment, const QString &path) {
  if (!environment.isEmpty()) {
    bool valid = false;
    const int percent = environment.toInt(&valid);
    if (valid && percent >= 0 && percent <= 100)
      return percent;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 4096)
    return -1;
  const auto data = file.read(4097);
  if (data.size() > 4096)
    return -1;
  const auto value = QJsonDocument::fromJson(data).object().value("ui_opacity_percent");
  const double percent = value.toDouble(-1);
  return value.isDouble() && percent >= 0 && percent <= 100 &&
                 std::floor(percent) == percent
             ? int(percent)
             : -1;
}
inline int alpha(int original) {
  static const int percent = readOpacity(
      qgetenv("PRISM_UI_OPACITY"),
      QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
          "/ky-prism/appearance.json");
  return percent < 0 ? original : (percent * 255 + 50) / 100;
}
} // namespace PrismAppearance
