#pragma once
#include <QIcon>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
// Resolution-independent outline icons. No font glyphs, network assets or
// fixed-DPI bitmaps; the same stroke scales correctly on HiDPI displays.
inline void drawPrismIcon(QPainter &p, const QString &name, const QRectF &rect,
                          QColor ink, QColor accent = {}) {
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  const double size = std::min(rect.width(), rect.height());
  p.translate(rect.center() - QPointF(size / 2, size / 2));
  p.scale(size / 24.0, size / 24.0);
  p.setPen(QPen(ink, 1.55, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(Qt::NoBrush);
  if (!accent.isValid())
    accent = ink;
  auto soft = accent;
  soft.setAlpha(110);
  auto paper = ink;
  paper.setAlpha(35);
  auto shade = ink;
  shade.setAlpha(80);
  if (name == "openPhoto") {
    // Layered camera inspired by the supplied illustrated icon sheet.
    p.setPen(QPen(ink, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(soft);
    p.drawRoundedRect(QRectF(2, 6, 20, 15), 3.5, 3.5);
    p.setBrush(shade);
    p.drawRoundedRect(QRectF(5, 3, 5, 3), 1, 1);
    p.setBrush(paper);
    p.drawRoundedRect(QRectF(4, 8, 5, 3), 1, 1);
    p.setBrush(ink);
    p.drawEllipse(QRectF(10, 9, 10, 10));
    p.setPen(QPen(accent.lighter(140), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(12, 11, 6, 6));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 210));
    p.drawEllipse(QRectF(13, 11.5, 2, 2));
    p.setPen(QPen(ink, 1.1));
    p.drawLine(4, 14, 7, 14);
  } else if (name == "bookmarks" || name == "favorites") {
    p.setPen(QPen(ink, 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.translate(12, 12);
    p.rotate(-7);
    p.translate(-12, -12);
    p.setBrush(paper);
    p.drawRoundedRect(QRectF(5, 2, 14, 19), 2, 2);
    p.setBrush(soft);
    p.drawRoundedRect(QRectF(4, 4, 15, 18), 2, 2);
    p.drawLine(7, 4, 7, 21);
    if (name == "bookmarks") {
      p.setBrush(accent);
      p.setPen(Qt::NoPen);
      p.drawPolygon(
          QPolygonF({{12, 3}, {16, 3}, {16, 12}, {14, 10}, {12, 12}}));
      p.setPen(QPen(ink, 1));
      p.drawLine(10, 16, 16, 16);
      p.drawLine(10, 18, 14, 18);
    } else {
      QPainterPath q;
      q.moveTo(13, 17);
      q.cubicTo(4, 11, 11, 8, 13, 11);
      q.cubicTo(16, 8, 21, 11, 13, 17);
      p.setPen(Qt::NoPen);
      p.setBrush(accent);
      p.drawPath(q);
    }
  } else if (name == "created" || name == "modified") {
    p.setBrush(paper);
    p.drawRoundedRect(QRectF(3, 5, 18, 17), 3, 3);
    p.setBrush(soft);
    p.drawRoundedRect(QRectF(3, 4, 18, 6), 2, 2);
    p.drawLine(8, 2, 8, 6);
    p.drawLine(16, 2, 16, 6);
    if (name == "created") {
      p.drawLine(8, 15, 16, 15);
      p.drawLine(12, 11, 12, 19);
    } else {
      p.drawEllipse(QRectF(8, 11, 8, 8));
      p.drawLine(12, 12, 12, 15);
      p.drawLine(12, 15, 15, 16);
    }
  } else if (name == "copyFiles" || name == "duplicates") {
    p.setBrush(soft);
    p.drawRoundedRect(QRectF(8, 8, 12, 13), 2.5, 2.5);
    p.setBrush(Qt::NoBrush);
    QPainterPath q;
    q.moveTo(15, 5);
    q.lineTo(15, 3);
    q.lineTo(4, 3);
    q.lineTo(4, 16);
    q.lineTo(6, 16);
    p.drawPath(q);
    if (name == "duplicates") {
      p.drawLine(11, 13, 17, 13);
      p.drawLine(11, 16, 17, 16);
    }
  } else if (name == "openUsb") {
    p.setBrush(soft);
    p.drawRoundedRect(QRectF(6, 9, 12, 12), 3, 3);
    p.setBrush(paper);
    p.drawRect(QRectF(8, 2, 8, 7));
    p.drawLine(11, 4, 11, 6);
    p.drawLine(14, 4, 14, 6);
    p.drawLine(10, 13, 14, 13);
    p.drawLine(12, 13, 12, 18);
  } else if (name == "comparePhotos") {
    p.drawRoundedRect(QRectF(2, 4, 20, 16), 3, 3);
    p.drawLine(12, 4, 12, 20);
    p.drawPolyline(QPolygonF({{4, 16}, {7, 12}, {10, 16}}));
    p.drawPolyline(QPolygonF({{14, 16}, {17, 12}, {20, 16}}));
  } else if (name == "folder" || name == "openPhoto" || name == "newFolder") {
    QPainterPath q;
    q.moveTo(3, 19);
    q.lineTo(3, 6);
    q.quadTo(3, 4, 5, 4);
    q.lineTo(9, 4);
    q.lineTo(12, 7);
    q.lineTo(19, 7);
    q.quadTo(21, 7, 21, 9);
    q.lineTo(21, 19);
    q.closeSubpath();
    p.drawPath(q);
    p.drawLine(4, 11, 20, 11);
    if (name == "newFolder") {
      p.drawLine(12,13,12,17);
      p.drawLine(10,15,14,15);
    }
  } else if (name == "sidebar") {
    p.drawRoundedRect(QRectF(3, 4, 18, 16), 3, 3);
    p.drawLine(9, 4, 9, 20);
    p.drawLine(5, 8, 7, 8);
    p.drawLine(5, 12, 7, 12);
  } else if (name == "previous" || name == "next" || name == "up") {
    p.translate(12, 12);
    if (name == "next")
      p.rotate(180);
    if (name == "up")
      p.rotate(90);
    p.drawPolyline(QPolygonF({{3, -6}, {-3, 0}, {3, 6}}));
  } else if (name == "zoomOut" || name == "zoomIn") {
    p.setBrush(paper);
    p.drawEllipse(QRectF(3, 3, 13, 13));
    p.setPen(QPen(accent, 2.5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(14, 14, 21, 21);
    p.setPen(QPen(ink, 1.55, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(6, 9, 13, 9);
    if (name == "zoomIn")
      p.drawLine(9.5, 5.5, 9.5, 12.5);
  } else if (name == "fit" || name == "fullscreen") {
    p.drawPolyline(QPolygonF({{3, 9}, {3, 3}, {9, 3}}));
    p.drawPolyline(QPolygonF({{15, 3}, {21, 3}, {21, 9}}));
    p.drawPolyline(QPolygonF({{21, 15}, {21, 21}, {15, 21}}));
    p.drawPolyline(QPolygonF({{9, 21}, {3, 21}, {3, 15}}));
    if (name == "fit")
      p.drawRoundedRect(QRectF(7, 8, 10, 8), 1, 1);
  } else if (name == "actual") {
    p.drawRoundedRect(QRectF(3, 4, 18, 16), 3, 3);
    p.drawPolyline(QPolygonF({{6, 10}, {8, 8}, {8, 16}}));
    p.drawPolyline(QPolygonF({{15, 10}, {17, 8}, {17, 16}}));
    p.drawPoint(12, 10);
    p.drawPoint(12, 14);
  } else if (name == "close") {
    p.drawLine(6, 6, 18, 18);
    p.drawLine(18, 6, 6, 18);
  } else if (name == "maximize" || name == "restore") {
    if (name == "restore") {
      p.drawRoundedRect(QRectF(8, 3, 13, 13), 2, 2);
      p.drawRoundedRect(QRectF(3, 8, 13, 13), 2, 2);
    } else
      p.drawRoundedRect(QRectF(4, 4, 16, 16), 3, 3);
  } else if (name == "about" || name == "exif") {
    p.drawEllipse(QRectF(3, 3, 18, 18));
    p.drawPoint(12, 7);
    p.drawLine(12, 11, 12, 17);
  } else if (name == "newTab") {
    p.drawRoundedRect(QRectF(3, 3, 18, 18), 3, 3);
    p.drawLine(7, 12, 17, 12);
    p.drawLine(12, 7, 12, 17);
  } else if (name == "list") {
    for (int y : {6, 12, 18}) {
      p.drawPoint(4, y);
      p.drawLine(9, y, 20, y);
    }
  } else if (name == "grid") {
    for (int x : {3, 14})
      for (int y : {3, 14})
        p.drawRoundedRect(QRectF(x, y, 7, 7), 1.5, 1.5);
  } else if (name == "bookmarks" || name == "star") {
    QPainterPath q;
    q.moveTo(12, 2);
    q.lineTo(15, 8);
    q.lineTo(22, 9);
    q.lineTo(17, 14);
    q.lineTo(18, 21);
    q.lineTo(12, 18);
    q.lineTo(6, 21);
    q.lineTo(7, 14);
    q.lineTo(2, 9);
    q.lineTo(9, 8);
    q.closeSubpath();
    p.drawPath(q);
  } else if (name == "favorites") {
    QPainterPath q;
    q.moveTo(12, 20);
    q.cubicTo(5, 15, 2, 11, 3, 7);
    q.cubicTo(4, 2, 10, 2, 12, 7);
    q.cubicTo(14, 2, 20, 2, 21, 7);
    q.cubicTo(22, 11, 19, 15, 12, 20);
    p.drawPath(q);
  } else if (name == "trash") {
    p.drawLine(4, 6, 20, 6);
    p.drawLine(9, 3, 15, 3);
    p.drawPolyline(QPolygonF({{6, 6}, {7, 21}, {17, 21}, {18, 6}}));
    p.drawLine(10, 10, 10, 17);
    p.drawLine(14, 10, 14, 17);
  } else if (name == "addPhoto") {
    p.drawLine(12, 5, 12, 19);
    p.drawLine(5, 12, 19, 12);
  } else if (name == "save") {
    p.drawRoundedRect(QRectF(3, 3, 18, 18), 3, 3);
    p.drawRect(QRectF(7, 3, 10, 6));
    p.drawRect(QRectF(7, 14, 10, 7));
  }
  p.restore();
}
class PrismIconEngine final : public QIconEngine {
  QString name;
  QColor ink, accent;

public:
  PrismIconEngine(QString n, QColor c, QColor a)
      : name(std::move(n)), ink(c), accent(a) {}
  QIconEngine *clone() const override {
    return new PrismIconEngine(name, ink, accent);
  }
  void paint(QPainter *p, const QRect &r, QIcon::Mode mode,
             QIcon::State) override {
    auto color = ink;
    if (mode == QIcon::Disabled)
      color.setAlpha(95);
    auto tint = accent;
    if (mode == QIcon::Disabled)
      tint.setAlpha(70);
    drawPrismIcon(*p, name, r, color, tint);
  }
  QPixmap pixmap(const QSize &size, QIcon::Mode mode,
                 QIcon::State state) override {
    QPixmap out(size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    paint(&p, QRect(QPoint(), size), mode, state);
    return out;
  }
};
inline QIcon prismIcon(const QString &name, const QColor &ink,
                       QColor accent = {}) {
  return QIcon(new PrismIconEngine(name, ink, accent));
}
