#pragma once
#include "appearance.h"
#include <QColor>
#include <QPalette>
#include <QString>
#include <algorithm>
#include <cmath>

namespace PrismDialogTheme {
inline double luminance(const QColor &c) {
  auto linear = [](double value) {
    return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
  };
  return .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
}
inline double contrast(const QColor &a, const QColor &b) {
  const auto x = luminance(a), y = luminance(b);
  return (std::max(x, y) + .05) / (std::min(x, y) + .05);
}
inline QColor readable(const QColor &background, const QColor &preferred) {
  if (contrast(background, preferred) >= 4.5)
    return preferred;
  return luminance(background) > .179 ? QColor(Qt::black) : QColor(Qt::white);
}
inline QPalette palette(const QPalette &fallback, QColor bg, QColor fg, QColor accent) {
  QPalette p = fallback;
  fg = readable(bg, fg);
  const auto selected = readable(accent, fg);
  for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
    for (auto role : {QPalette::Window, QPalette::Base, QPalette::AlternateBase, QPalette::Button})
      p.setColor(group, role, bg);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText})
      p.setColor(group, role, fg);
    p.setColor(group, QPalette::ToolTipBase, bg);
    p.setColor(group, QPalette::Highlight, accent);
    p.setColor(group, QPalette::HighlightedText, selected);
    p.setColor(group, QPalette::Link, readable(bg, accent));
    p.setColor(group, QPalette::PlaceholderText, fg);
  }
  return p;
}
inline QString fileDialogStyle(QColor bg, QColor fg, QColor accent) {
  fg = readable(bg, fg);
  const auto selected = readable(accent, fg);
  auto rgba = [](QColor c, int alpha) {
    return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
  };
  return QString(
      "QFileDialog {background:%1;color:%2;border:1px solid %3;border-radius:16px;} "
      "QFileDialog QAbstractItemView {background:transparent;color:%2;"
      "alternate-background-color:%4;selection-background-color:%5;selection-color:%6;"
      "border:0;outline:0;} QFileDialog QAbstractItemView::item {min-height:26px;padding:3px;} "
      "QFileDialog QAbstractItemView::item:selected {background:%5;color:%6;} "
      "QFileDialog QHeaderView::section {background:%4;color:%2;border:0;"
      "border-bottom:1px solid %3;padding:7px;} "
      "QFileDialog QLineEdit,QFileDialog QComboBox {background:%4;color:%2;"
      "selection-background-color:%5;selection-color:%6;border:1px solid %3;"
      "border-radius:8px;padding:6px;} "
      "QFileDialog QLineEdit:focus,QFileDialog QComboBox:focus {border-color:%5;} "
      "QFileDialog QLabel,QFileDialog QToolButton {color:%2;} "
      "QFileDialog QToolButton {background:%4;border:1px solid %3;border-radius:6px;padding:5px;} "
      "QFileDialog QToolButton:hover {border-color:%5;} "
      "QFileDialog QSplitter::handle {background:%3;}")
      .arg(rgba(bg, PrismAppearance::alpha(228)), fg.name(), rgba(fg, 52), rgba(bg, 110), accent.name(), selected.name());
}
} // namespace PrismDialogTheme
