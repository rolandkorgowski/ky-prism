#pragma once
// Photo print layout (toolkit independent, QtCore only). All lengths are in
// points (1/72 inch) on the printable area of the page.
#include <QList>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>

struct PrintOptions {
  enum Size { Fit, Fill, Actual, Fixed, Custom };
  int perPage = 1;              // 1, 2, 4, 6 or 9 photos on a sheet
  Size size = Fit;
  double widthMm = 100, heightMm = 150; // Fixed (preset) and Custom sizes
  bool cropToFormat = true;     // Fixed/Custom: fill the format, cropping
  int scalePercent = 100;       // 10–400 % of the size above
  double marginMm = 5;          // space around and between photos
  bool grayscale = false;
  int dpi = 300;                // rendering resolution: 150, 300 or 600
  bool caption = false;         // file name under each photo
  bool autoRotate = true;       // turn photos to the cell's orientation

  // Stored as plain values inside GtkPrintSettings ("ky-prism-*" keys).
  static PrintOptions fromValues(const QList<QPair<QString, QString>> &values);
  QList<QPair<QString, QString>> values() const;
};

struct PrintPlacement {
  QRectF cell;      // the photo's share of the page
  QRectF clip;      // visible area (photo, after cropping and clipping)
  QRectF image;     // where the whole photo would lie (may exceed clip)
  bool rotate = false; // photo turned by 90° to match the cell
  QRectF caption;   // file name area (empty without a caption)
};

namespace PrintLayout {
constexpr double pointsPerMm = 72.0 / 25.4;
// Grid of cells on the printable area; rows/columns follow its orientation.
QList<QRectF> cells(QSizeF printable, int perPage, double gap);
// One photo (pixel size and file resolution) in one cell.
PrintPlacement place(const QRectF &cell, QSize pixels, double fileDpi,
                     const PrintOptions &options, double captionHeight);
// Sheets needed: one photo repeated fills a sheet; otherwise one per cell.
int pages(int photos, int perPage);
// Photo index shown in cell `slot` of page `page` (-1: empty cell).
int photoAt(int photos, int perPage, int page, int slot);
// Pixels worth rendering for a placement at `dpi`, never above the source.
QSize renderPixels(const PrintPlacement &placement, QSize source, int dpi);
} // namespace PrintLayout
