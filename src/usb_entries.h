#pragma once
// lsblk JSON parser shared by every front end (QtCore only).
#include <QByteArray>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <functional>
struct UsbEntry {
  QString device, label, mount;
};
inline QList<UsbEntry> usbEntries(const QByteArray &json) {
  QList<UsbEntry> result;
  if (json.size() > 2 * 1024 * 1024)
    return result;
  auto doc = QJsonDocument::fromJson(json);
  if (!doc.isObject())
    return result;
  std::function<void(QJsonArray, bool, int)> visit = [&](QJsonArray rows,
                                                         bool parentUsb,
                                                         int depth) {
    if (depth > 8)
      return;
    for (const auto &row : rows) {
      if (result.size() >= 64)
        return;
      auto o = row.toObject();
      bool usb = parentUsb || o["tran"].toString() == "usb" || o["rm"].toBool();
      QString device = o["name"].toString(), fs = o["fstype"].toString();
      bool valid = device.startsWith("/dev/") && !device.contains("..") &&
                   QRegularExpression("^/dev/[a-zA-Z0-9_./-]+$")
                       .match(device)
                       .hasMatch();
      if (usb && valid && !fs.isEmpty() && fs != "swap" &&
          fs != "crypto_LUKS" && fs != "LVM2_member" &&
          fs != "linux_raid_member") {
        QString mount;
        for (const auto &m : o["mountpoints"].toArray())
          if (m.isString() && QDir::isAbsolutePath(m.toString()) &&
              m.toString() != "/") {
            mount = m.toString();
            break;
          }
        result.append({device, o["label"].toString(), mount});
      }
      visit(o["children"].toArray(), usb, depth + 1);
    }
  };
  visit(doc.object()["blockdevices"].toArray(), false, 0);
  return result;
}
