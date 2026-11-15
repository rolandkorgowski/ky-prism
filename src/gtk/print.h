#pragma once
#include "gtkbase.h"
#include "print_layout.h"
#include <QStringList>
#include <functional>
#include <memory>

// Photo printing through the system print dialog (CUPS printers, paper, copies,
// driver options) with an extra "Zdjęcie" tab: photos per sheet, print size,
// scale, margins, colour or greyscale, rendering resolution, captions and a
// live page preview. Pages are prepared off the main thread, one at a time.
class PhotoPrint {
  struct Impl;
  std::unique_ptr<Impl> d;

public:
  PhotoPrint(GtkWindow *parent, const QStringList &photos);
  ~PhotoPrint();
  PhotoPrint(const PhotoPrint &) = delete;
  void run();
  // Tests and automation: write a PDF without any dialog.
  void exportPdf(const QString &file, const PrintOptions &options);
  void cancel();
  bool running() const;
  int pageCount() const;
  // Status line text ("Wysłano do drukarki…", errors).
  std::function<void(const QString &)> status;
  // Called once when the job is over; the owner may destroy this object.
  std::function<void()> finished;
  // Saved settings: ~/.config/ky-prism/print.ini
  static QString settingsFile();
};
