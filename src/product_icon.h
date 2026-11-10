#pragma once
#include <QGuiApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <algorithm>

// Static final pose of ProductMark in About; scalable at every icon size.
class ProductIconEngine final : public QIconEngine {
public:
  QIconEngine *clone() const override { return new ProductIconEngine; }
  void paint(QPainter *p,const QRect &rect,QIcon::Mode mode,QIcon::State) override {
    p->save(); p->setRenderHint(QPainter::Antialiasing);
    p->translate(QRectF(rect).center());
    const auto scale=std::min(rect.width(),rect.height())/160.0;
    p->scale(scale,scale);
    const auto palette=QGuiApplication::palette();
    auto ink=palette.color(QPalette::WindowText),accent=palette.color(QPalette::Highlight);
    if (mode==QIcon::Disabled) p->setOpacity(.45);
    for(int i=0;i<7;++i) {
      p->save(); p->rotate(i*360.0/7);
      QPainterPath blade; blade.moveTo(0,-60); blade.cubicTo(32,-56,57,-17,36,17);
      blade.lineTo(28,5); blade.cubicTo(30,-23,18,-40,0,-60);
      auto fill=accent; fill.setAlpha(18+i*4);
      p->setPen(QPen(ink,1.3));p->setBrush(fill);p->drawPath(blade);p->restore();
    }
    p->setBrush(Qt::NoBrush);p->setPen(QPen(accent,1.6));p->drawEllipse(QPointF(),66,66);
    p->setPen(QPen(ink,.8));p->drawEllipse(QPointF(),73,73);p->restore();
  }
  QPixmap pixmap(const QSize &size,QIcon::Mode mode,QIcon::State state) override {
    QPixmap result(size);result.fill(Qt::transparent);QPainter p(&result);
    paint(&p,QRect(QPoint(),size),mode,state);return result;
  }
};
inline QIcon productIcon() { return QIcon(new ProductIconEngine); }
