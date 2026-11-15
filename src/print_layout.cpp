#include "print_layout.h"
#include <algorithm>
#include <cmath>

namespace {
QSizeF scaled(QSizeF size, double factor) { return size * factor; }
QRectF centered(const QRectF &box, QSizeF size) {
  return QRectF(box.center().x() - size.width() / 2,
                box.center().y() - size.height() / 2, size.width(),
                size.height());
}
int clampInt(int value, int low, int high) {
  return std::max(low, std::min(high, value));
}
} // namespace

PrintOptions
PrintOptions::fromValues(const QList<QPair<QString, QString>> &values) {
  PrintOptions o;
  for (const auto &[key, value] : values) {
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (key == "per-page" && ok && QList<int>{1, 2, 4, 6, 9}.contains(int(number)))
      o.perPage = int(number);
    else if (key == "size" && ok && number >= int(Fit) && number <= int(Custom))
      o.size = Size(int(number));
    else if (key == "width-mm" && ok && number >= 10 && number <= 2000)
      o.widthMm = number;
    else if (key == "height-mm" && ok && number >= 10 && number <= 2000)
      o.heightMm = number;
    else if (key == "crop")
      o.cropToFormat = value == "1";
    else if (key == "scale" && ok)
      o.scalePercent = clampInt(int(number), 10, 400);
    else if (key == "margin-mm" && ok && number >= 0 && number <= 50)
      o.marginMm = number;
    else if (key == "grayscale")
      o.grayscale = value == "1";
    else if (key == "dpi" && ok && QList<int>{150, 300, 600}.contains(int(number)))
      o.dpi = int(number);
    else if (key == "caption")
      o.caption = value == "1";
    else if (key == "auto-rotate")
      o.autoRotate = value == "1";
  }
  return o;
}

QList<QPair<QString, QString>> PrintOptions::values() const {
  auto flag = [](bool b) { return QString(b ? "1" : "0"); };
  return {{"per-page", QString::number(perPage)},
          {"size", QString::number(int(size))},
          {"width-mm", QString::number(widthMm)},
          {"height-mm", QString::number(heightMm)},
          {"crop", flag(cropToFormat)},
          {"scale", QString::number(scalePercent)},
          {"margin-mm", QString::number(marginMm)},
          {"grayscale", flag(grayscale)},
          {"dpi", QString::number(dpi)},
          {"caption", flag(caption)},
          {"auto-rotate", flag(autoRotate)}};
}

namespace PrintLayout {

QList<QRectF> cells(QSizeF printable, int perPage, double gap) {
  const bool portrait = printable.height() >= printable.width();
  int columns = 1;
  switch (perPage) {
  case 2: columns = portrait ? 1 : 2; break;
  case 4: columns = 2; break;
  case 6: columns = portrait ? 2 : 3; break;
  case 9: columns = 3; break;
  default: perPage = 1; columns = 1;
  }
  const int rows = perPage / columns;
  gap = std::max(0.0, gap);
  const double width = (printable.width() - gap * (columns + 1)) / columns;
  const double height = (printable.height() - gap * (rows + 1)) / rows;
  QList<QRectF> result;
  if (width <= 0 || height <= 0)
    return result;
  for (int r = 0; r < rows; ++r)
    for (int c = 0; c < columns; ++c)
      result.append(QRectF(gap + c * (width + gap), gap + r * (height + gap),
                           width, height));
  return result;
}

PrintPlacement place(const QRectF &cell, QSize pixels, double fileDpi,
                     const PrintOptions &o, double captionHeight) {
  PrintPlacement p;
  p.cell = cell;
  QRectF area = cell;
  if (o.caption && captionHeight > 0 && cell.height() > captionHeight * 2) {
    p.caption = QRectF(cell.left(), cell.bottom() - captionHeight, cell.width(),
                       captionHeight);
    area.setBottom(p.caption.top());
  }
  if (pixels.isEmpty() || area.isEmpty())
    return p;
  QSizeF photo(pixels.width(), pixels.height());
  const bool photoLandscape = photo.width() > photo.height();
  const bool areaLandscape = area.width() > area.height();
  p.rotate = o.autoRotate && photo.width() != photo.height() &&
             photoLandscape != areaLandscape;
  if (p.rotate)
    photo.transpose();
  const double factor = o.scalePercent / 100.0;
  auto fitIn = [&](QSizeF box) {
    const double s = std::min(box.width() / photo.width(),
                              box.height() / photo.height());
    return scaled(photo, s);
  };
  auto fillIn = [&](QSizeF box) {
    const double s = std::max(box.width() / photo.width(),
                              box.height() / photo.height());
    return scaled(photo, s);
  };
  QSizeF frame;  // visible part
  QSizeF whole;  // the full photo at print size
  switch (o.size) {
  case PrintOptions::Fit:
    whole = frame = fitIn(area.size());
    break;
  case PrintOptions::Fill:
    frame = area.size();
    whole = fillIn(frame);
    break;
  case PrintOptions::Actual: {
    // Cameras often write 72 dpi; without a plausible value assume 300 dpi.
    const double dpi = fileDpi >= 100 && fileDpi <= 4800 ? fileDpi : 300.0;
    whole = frame = QSizeF(photo.width() / dpi * 72.0, photo.height() / dpi * 72.0);
    break;
  }
  case PrintOptions::Fixed:
  case PrintOptions::Custom: {
    QSizeF format(o.widthMm * pointsPerMm, o.heightMm * pointsPerMm);
    // The format takes the photo's orientation (10×15 or 15×10).
    if ((format.width() > format.height()) != (photo.width() > photo.height()))
      format.transpose();
    if (o.cropToFormat) {
      frame = format;
      whole = fillIn(format);
    } else {
      whole = frame = fitIn(format);
    }
    break;
  }
  }
  frame *= factor;
  whole *= factor;
  const QRectF visible = centered(area, frame) & area;
  p.clip = visible;
  p.image = centered(area, whole);
  return p;
}

int pages(int photos, int perPage) {
  if (photos <= 0)
    return 0;
  if (photos == 1)
    return 1;
  return (photos + perPage - 1) / perPage;
}

int photoAt(int photos, int perPage, int page, int slot) {
  if (photos <= 0 || slot < 0 || slot >= perPage)
    return -1;
  if (photos == 1)
    return 0; // one photo repeated over the sheet (e.g. 4 small prints)
  const int index = page * perPage + slot;
  return index < photos ? index : -1;
}

QSize renderPixels(const PrintPlacement &p, QSize source, int dpi) {
  if (p.image.isEmpty() || source.isEmpty())
    return {};
  // Size of the whole photo on paper in inches times the resolution.
  QSizeF onPaper = p.image.size();
  if (p.rotate)
    onPaper.transpose();
  const double wanted = std::max(onPaper.width() / 72.0 * dpi,
                                 onPaper.height() / 72.0 * dpi);
  const double longest = std::max(source.width(), source.height());
  const double factor = std::min(1.0, wanted / longest);
  return QSize(std::max(1, int(std::lround(source.width() * factor))),
               std::max(1, int(std::lround(source.height() * factor))));
}

} // namespace PrintLayout
