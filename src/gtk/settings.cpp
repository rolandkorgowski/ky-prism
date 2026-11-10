#include "settings.h"
#include "appearance.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace PrismSettings {
QString appearanceFile() {
  return QStandardPaths::writableLocation(
             QStandardPaths::GenericConfigLocation) +
         "/ky-prism/appearance.json";
}
int storedOpacity(const QByteArray &environment, const QString &file) {
  return PrismAppearance::readOpacity(environment, file);
}
int defaultOpacity(bool compositorBlurs) { return compositorBlurs ? 69 : 94; }
double alphaFor(int percent) {
  return std::clamp(percent, 0, 100) / 100.0;
}
bool saveOpacity(const QString &file, int percent) {
  if (percent < 0 || percent > 100)
    return false;
  const QFileInfo info(file);
  if (info.isSymLink() || !QDir().mkpath(info.absolutePath()))
    return false;
  QJsonObject root;
  QFile existing(file);
  if (existing.open(QIODevice::ReadOnly) && existing.size() <= 4096) {
    const auto parsed = QJsonDocument::fromJson(existing.read(4096));
    if (parsed.isObject())
      root = parsed.object();
  }
  root.insert("ui_opacity_percent", percent);
  const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact) + "\n";
  if (bytes.size() > 4096)
    return false;
  QSaveFile output(file);
  output.setDirectWriteFallback(false);
  return output.open(QIODevice::WriteOnly) &&
         output.write(bytes) == bytes.size() && output.commit();
}
} // namespace PrismSettings
