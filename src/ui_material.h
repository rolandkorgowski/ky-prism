#pragma once
#include "appearance.h"
#include <QHeaderView>
#include <QPainter>
#include <QStyleOptionHeader>
// Paint the entire header explicitly: no inherited native white panel.
class GlassHeader final : public QHeaderView {
protected:
  void paintSection(QPainter *p, const QRect &r, int section) const override {
    if (!r.isValid())
      return;
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    QColor base = palette().color(QPalette::Window),
           ink = palette().color(QPalette::WindowText);
    QLinearGradient glass(r.topLeft(), r.bottomLeft());
    auto top = base.lighter(112);
    top.setAlpha(PrismAppearance::alpha(175));
    auto bottom = base.darker(106);
    bottom.setAlpha(PrismAppearance::alpha(175));
    glass.setColorAt(0, top);
    glass.setColorAt(1, bottom);
    p->fillRect(r, glass);
    auto edge = ink;
    edge.setAlpha(40);
    p->setPen(edge);
    p->drawLine(r.bottomLeft(), r.bottomRight());
    edge.setAlpha(20);
    p->setPen(edge);
    p->drawLine(r.topLeft(), r.topRight());
    p->drawLine(r.topRight(), r.bottomRight());
    p->setPen(ink);
    auto font = p->font();
    font.setPointSizeF(9);
    font.setWeight(QFont::DemiBold);
    p->setFont(font);
    auto text = model() ? model()->headerData(section, orientation()).toString()
                        : QString();
    p->drawText(r.adjusted(9, 0, -9, 0),
                Qt::AlignVCenter |
                    (section == 1 ? Qt::AlignRight : Qt::AlignLeft),
                text);
    p->restore();
  }

public:
  explicit GlassHeader(QWidget *parent = nullptr)
      : QHeaderView(Qt::Horizontal, parent) {
    setFixedHeight(33);
    setAutoFillBackground(false);
    viewport()->setAutoFillBackground(false);
    setSectionsClickable(false);
  }
};
inline QString glassButtons(const QColor &bg, const QColor &fg,
                            const QColor &accent) {
  auto rgba = [](QColor color, int a) {
    return QString("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(a);
  };
  const auto top = rgba(bg.lighter(135), 70), bottom = rgba(bg.darker(110), 50),
             edge = rgba(fg, 55), shine = rgba(fg, 85), dim = rgba(fg, 90),
             tint = rgba(accent, 75), pressed = rgba(bg.darker(140), 100);
  return QString(
             "QPushButton "
             "{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 %1,stop:1 "
             "%2);border:1px solid "
             "%3;border-top-color:%4;border-radius:10px;padding:5px 6px;} "
             "QPushButton:hover "
             "{border-color:%5;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
             "stop:0 %1,stop:1 %6);} QPushButton:checked "
             "{background:%6;border-color:%5;color:%7;} QPushButton:pressed "
             "{background:%8;border-top-color:%3;border-bottom-color:%4;"
             "padding-top:6px;padding-bottom:4px;color:%7;} QPushButton:focus "
             "{border:1px solid %5;} QPushButton:disabled "
             "{color:%9;border-color:%3;background:%2;} QFrame "
             "{border-color:%3;} QPushButton[prismGlass=\"true\"] "
             "{background:transparent;border:0;padding:0;}")
      .arg(top, bottom, edge, shine, accent.name(), tint, fg.name(), pressed,
           dim);
}
