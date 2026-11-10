#pragma once
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPalette>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

// Shared by the Qt5/6 style and ky-prism. Only QFileDialog is affected.
// No application palette changes, polling, window flags or input grabs.
namespace KyFileDialogGlass {
// App-local opt-in. Other applications retain the original material.
inline int surfaceAlpha() {
  bool valid = false;
  const int alpha = qApp ? qApp->property("kyPrismUiAlpha").toInt(&valid) : 228;
  return valid && alpha >= 0 && alpha <= 255 ? alpha : 228;
}
inline double luminance(const QColor &c) {
  auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
  return .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
}
inline double contrast(const QColor &a, const QColor &b) {
  const auto x = luminance(a), y = luminance(b);
  return (std::max(x, y) + .05) / (std::min(x, y) + .05);
}
inline QColor readable(const QColor &bg, const QColor &fg) {
  if (contrast(bg, fg) >= 4.5) return fg;
  return luminance(bg) > .179 ? QColor(Qt::black) : QColor(Qt::white);
}
inline QPalette themePalette(QPalette p) {
  auto bg = p.color(QPalette::Window), fg = p.color(QPalette::WindowText);
  auto selection = p.color(QPalette::Highlight), selected = p.color(QPalette::HighlightedText);
  QFile file(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/omarchy/menu-glass/palette.json");
  if (file.open(QIODevice::ReadOnly) && file.size() <= 16384) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error == QJsonParseError::NoError && document.isObject()) {
      auto get = [&](const char *key, QColor fallback) {
        const auto value = document.object().value(QLatin1String(key)).toString();
        const QColor color(value);
        return value.size() == 7 && value.startsWith('#') && color.isValid() ? color : fallback;
      };
      bg = get("background", bg); fg = get("foreground", fg);
      selection = get("selection", selection); selected = get("selectedText", selected);
    }
  }
  fg = readable(bg, fg); selected = readable(selection, selected);
  for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
    for (auto role : {QPalette::Window, QPalette::Base, QPalette::AlternateBase, QPalette::Button, QPalette::ToolTipBase})
      p.setColor(group, role, bg);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText, QPalette::PlaceholderText})
      p.setColor(group, role, fg);
    p.setColor(group, QPalette::Highlight, selection);
    p.setColor(group, QPalette::HighlightedText, selected);
    p.setColor(group, QPalette::Link, readable(bg, selection));
    p.setColor(group, QPalette::LinkVisited, readable(bg, selection));
    p.setColor(group, QPalette::Light, bg.lighter(130));
    p.setColor(group, QPalette::Midlight, bg.lighter(115));
    p.setColor(group, QPalette::Mid, bg.darker(115));
    p.setColor(group, QPalette::Dark, bg.darker(140));
    p.setColor(group, QPalette::Shadow, bg.darker(180));
    p.setColor(group, QPalette::BrightText, fg);
  }
  return p;
}
inline QString styleSheet(const QPalette &p) {
  const auto bg = p.color(QPalette::Window), fg = p.color(QPalette::WindowText);
  auto rgba = [](QColor c, int alpha) {
    return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
  };
  // Set a dialog-local sheet: inherited application QTreeView/QWidget rules
  // otherwise override a correct palette (including on the combo popup).
  return QString(
    "QFileDialog {background:%9;color:%1;} "
    "QFileDialog QWidget {color:%1;} "
    "QFileDialog QTreeView,QFileDialog QListView {background:transparent;color:%1;"
    "alternate-background-color:transparent;border:0;outline:0;"
    "selection-background-color:%3;selection-color:%4;} "
    "QFileDialog QAbstractItemView::item {min-height:25px;padding:3px;} "
    "QFileDialog QAbstractItemView::item:selected {background:%3;color:%4;} "
    "QFileDialog QTreeView::branch {background:transparent;} "
    "QFileDialog QHeaderView {background:transparent;border:0;padding:0;} "
    "QFileDialog QHeaderView::section {background:%5;color:%1;"
    "border:0;border-bottom:1px solid %6;padding:6px;} "
    "QFileDialog QLineEdit,QFileDialog QComboBox {background:%5;color:%1;"
    "selection-background-color:%3;selection-color:%4;border:1px solid %6;border-radius:7px;padding:6px;} "
    "QFileDialog QComboBox QAbstractItemView {background:%2;color:%1;"
    "selection-background-color:%3;selection-color:%4;border:1px solid %6;} "
    "QFileDialog QLineEdit:focus,QFileDialog QComboBox:focus {border-color:%3;} "
    "QFileDialog QPushButton,QFileDialog QToolButton {background:%5;color:%1;"
    "border:1px solid %6;border-radius:6px;padding:6px;} "
    "QFileDialog QPushButton:hover,QFileDialog QToolButton:hover {border-color:%3;background:%7;} "
    "QFileDialog QPushButton:pressed,QFileDialog QToolButton:pressed,QFileDialog QToolButton:checked "
    "{background:%3;color:%4;} QFileDialog QPushButton:disabled,QFileDialog QToolButton:disabled "
    "{color:%8;border-color:%5;} QFileDialog QPushButton:focus {border:1px solid %3;} "
    "QFileDialog QSplitter::handle {background:%6;} "
    "QFileDialog QScrollBar {background:transparent;} "
    "QFileDialog QScrollBar:vertical {width:10px;} QFileDialog QScrollBar:horizontal {height:10px;} "
    "QFileDialog QScrollBar::handle {background:%8;border-radius:4px;min-height:24px;min-width:24px;} "
    "QFileDialog QScrollBar::add-line,QFileDialog QScrollBar::sub-line {width:0;height:0;}")
    .arg(fg.name(), bg.name(), p.color(QPalette::Highlight).name(),
         p.color(QPalette::HighlightedText).name(), rgba(bg, 120), rgba(fg, 52), rgba(fg, 20), rgba(fg, 165), rgba(bg, surfaceAlpha()));
}
inline void polish(QWidget *widget, QObject *filter) {
  if (!qobject_cast<QFileDialog *>(widget)) return;
  widget->setAttribute(Qt::WA_TranslucentBackground);
  widget->setAttribute(Qt::WA_WindowPropagation);
  widget->setAutoFillBackground(false);
  widget->installEventFilter(filter);
}
inline void refresh(QFileDialog *dialog) {
  if (dialog->property("kyFileDialogUpdating").toBool()) return;
  dialog->setProperty("kyFileDialogUpdating", true);
  const auto p = themePalette(QApplication::palette());
  dialog->setProperty("kyFileDialogPalette", p);
  if (dialog->palette() != p) dialog->setPalette(p);
  const auto css = styleSheet(p);
  if (dialog->styleSheet() != css) dialog->setStyleSheet(css);
  // ComboBox creates a separate popup window; palette inheritance stops at
  // that boundary. This includes both the location and file-type selectors.
  for (auto *combo : dialog->findChildren<QComboBox *>()) {
    auto *view = combo->view();
    view->window()->setPalette(p);
    view->setPalette(p);
    view->setStyleSheet(QString("QAbstractItemView{background:%1;color:%2;selection-background-color:%3;selection-color:%4;}")
      .arg(p.color(QPalette::Window).name(), p.color(QPalette::Text).name(),
           p.color(QPalette::Highlight).name(), p.color(QPalette::HighlightedText).name()));
  }
  for (auto *view : dialog->findChildren<QAbstractItemView *>())
    view->setPalette(p);
  dialog->setProperty("kyFileDialogUpdating", false);
}
inline void paint(QFileDialog *dialog) {
  QPainter painter(dialog);
  painter.setRenderHint(QPainter::Antialiasing);
  const auto palette = dialog->property("kyFileDialogPalette").value<QPalette>();
  auto body = palette.color(QPalette::Window); body.setAlpha(surfaceAlpha());
  auto edge = palette.color(QPalette::WindowText); edge.setAlpha(52);
  painter.setPen(QPen(edge, 1)); painter.setBrush(body);
  painter.drawRoundedRect(QRectF(dialog->rect()).adjusted(.5,.5,-.5,-.5), 12, 12);
}
} // namespace KyFileDialogGlass
