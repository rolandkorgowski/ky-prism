#pragma once
#include "localized_widgets.h"
#include "usb_entries.h"
#include <QDir>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <functional>
class UsbAccess final : public QObject {
  Q_OBJECT
  QProcess process;
  QTimer timeout;
  QWidget *window;
  bool mounting = false;
  QString device;

public:
  void shutdown() {
    timeout.stop();
    process.disconnect(this);
    process.kill();
  }
  explicit UsbAccess(QWidget *parent) : QObject(parent), window(parent) {
    timeout.setSingleShot(true);
    timeout.setInterval(20000);
    connect(&timeout, &QTimer::timeout, this, [this] {
      process.kill();
      Q_EMIT failed("Nośnik nie odpowiedział w ciągu 20 sekund.");
    });
    connect(&process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
              timeout.stop();
              Q_EMIT failed("Nie można uruchomić obsługi USB: " +
                          process.errorString());
            });
    connect(
        &process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this, [this](int code, QProcess::ExitStatus) {
          timeout.stop();
          if (code) {
            Q_EMIT failed(
                "USB: " +
                QString::fromUtf8(process.readAllStandardError()).left(500));
            return;
          }
          if (mounting) {
            for (const auto &v : QStorageInfo::mountedVolumes())
              if (v.isReady() && QString::fromLocal8Bit(v.device()) == device) {
                Q_EMIT opened(v.rootPath());
                return;
              }
            Q_EMIT failed("Nośnik zamontowano, ale nie potwierdzono jego "
                        "ścieżki. Spróbuj ponownie.");
            return;
          }
          const auto rows = usbEntries(process.readAllStandardOutput());
          if (rows.isEmpty()) {
            QMessageBox::information(
                window, "Pendrive",
                "Nie ma pendrive’a ani dostępnego nośnika USB ze zdjęciami.");
            return;
          }
          int selected = 0;
          if (rows.size() > 1) {
            QStringList names;
            for (const auto &r : rows)
              names.append(
                  (r.label.isEmpty() ? QString("Nośnik USB") : r.label) +
                  " · " + r.device);
            bool ok = false;
            auto choice = UiInputDialog::getItem(window, "Wybierz pendrive",
                                                "Dostępne nośniki USB", names,
                                                0, false, &ok);
            if (!ok)
              return;
            selected = names.indexOf(choice);
          }
          const auto row = rows.at(selected);
          if (!row.mount.isEmpty()) {
            Q_EMIT opened(row.mount);
            return;
          }
          auto command = QStandardPaths::findExecutable("udisksctl");
          if (command.isEmpty()) {
            Q_EMIT failed("Brak udisksctl. Zamontuj nośnik w programie Pliki lub "
                        "zainstaluj udisks2.");
            return;
          }
          mounting = true;
          device = row.device;
          process.start(command, {"mount", "--block-device", device});
          timeout.start();
        });
  }
  ~UsbAccess() override {
    process.disconnect(this);
    timeout.stop();
    process.kill();
    process.waitForFinished(1000);
  }
  void open() {
    if (process.state() != QProcess::NotRunning)
      return;
    mounting = false;
    process.start("lsblk", {"--json", "--paths", "--output",
                            "NAME,TYPE,TRAN,RM,FSTYPE,MOUNTPOINTS,LABEL"});
    timeout.start();
  }
Q_SIGNALS:
  void opened(const QString &path);
  void failed(const QString &message);
};
