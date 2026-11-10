#pragma once
#include "appearance.h"
#include "file_dialog_glass.h"
#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QStyleOptionMenuItem>
#include <QToolButton>
#include "ui_icons.h"

// Alpha surfaces for popup menus and our Qt file chooser. Photographs retain
// their own opaque renderer. No input grabs, geometry changes or idle work.
class MenuGlassStyle final : public QProxyStyle {
  static void fileDialogIcons(QFileDialog *dialog) {
    const auto ink = dialog->palette().color(QPalette::WindowText);
    for (auto *button : dialog->findChildren<QToolButton *>()) {
      const auto name = button->objectName();
      QString symbol;
      if (name == "backButton") symbol = "previous";
      else if (name == "forwardButton") symbol = "next";
      else if (name == "toParentButton") symbol = "up";
      else if (name == "newFolderButton") symbol = "newFolder";
      else if (name == "listModeButton") symbol = "grid";
      else if (name == "detailModeButton") symbol = "list";
      if (symbol.isEmpty()) continue;
      // Use scalable outlines, not tinting an opaque stock icon bitmap.
      button->setIcon(prismIcon(symbol, ink, dialog->palette().color(QPalette::Highlight)));
      button->setProperty("prismVectorIcon", true);
    }
  }
  static QPalette menuPalette(const QPalette &fallback) {
    QPalette p = fallback;
    QFile file(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
               "/omarchy/menu-glass/palette.json");
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384)
      return p;
    QJsonParseError error;
    const auto data = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !data.isObject())
      return p;
    const auto object = data.object();
    auto color = [&](const char *key, QPalette::ColorRole role) {
      const QString text = object.value(QLatin1String(key)).toString();
      const QColor c(text);
      if (text.size() == 7 && text.startsWith('#') && c.isValid())
        p.setColor(role, c);
    };
    color("background", QPalette::Window);
    color("foreground", QPalette::WindowText);
    color("selection", QPalette::Highlight);
    color("selectedText", QPalette::HighlightedText);
    p.setColor(QPalette::Text, p.color(QPalette::WindowText));
    p.setColor(QPalette::ButtonText, p.color(QPalette::WindowText));
    p.setColor(QPalette::Disabled, QPalette::WindowText,
               p.color(QPalette::WindowText).darker(150));
    return p;
  }

protected:
  bool eventFilter(QObject *object, QEvent *event) override {
    if (auto *dialog = qobject_cast<QFileDialog *>(object)) {
      if (event->type() == QEvent::Show || event->type() == QEvent::ApplicationPaletteChange) {
        KyFileDialogGlass::refresh(dialog);
        fileDialogIcons(dialog);
      }
      if (event->type() == QEvent::Paint) KyFileDialogGlass::paint(dialog);
    }
    if (event->type() == QEvent::Show) {
      if (auto *menu = qobject_cast<QMenu *>(object)) {
        // Read at open, once; an already running app follows the next theme.
        const auto p = menuPalette(menu->palette());
        if (menu->palette() != p)
          menu->setPalette(p);
      }
    }
    return QProxyStyle::eventFilter(object, event);
  }

public:
  MenuGlassStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {
    if (qApp) qApp->setProperty("kyPrismUiAlpha", PrismAppearance::alpha(228));
  }
  void polish(QWidget *widget) override {
    QProxyStyle::polish(widget);
    KyFileDialogGlass::polish(widget, this);
    if (qobject_cast<QMenu *>(widget)) {
      widget->setAttribute(Qt::WA_TranslucentBackground);
      widget->setAttribute(Qt::WA_NoSystemBackground);
      widget->setAutoFillBackground(false);
      widget->installEventFilter(this);
    }
  }
  void unpolish(QWidget *widget) override {
    widget->removeEventFilter(this);
    QProxyStyle::unpolish(widget);
  }
  int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr) const override {
    if (metric == PM_MenuPanelWidth)
      return 6;
    if (metric == PM_MenuHMargin || metric == PM_MenuVMargin)
      return 3;
    return QProxyStyle::pixelMetric(metric, option, widget);
  }
  void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                     QPainter *p, const QWidget *widget = nullptr) const override {
    if (element != PE_PanelMenu) {
      QProxyStyle::drawPrimitive(element, option, p, widget);
      return;
    }
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(option->rect).adjusted(.5, .5, -.5, -.5);
    QColor body = option->palette.color(QPalette::Window);
    body.setAlpha(PrismAppearance::alpha(228)); // Background only; glyphs stay opaque.
    QLinearGradient glass(r.topLeft(), r.bottomLeft());
    auto shine = body.lighter(108);
    shine.setAlpha(PrismAppearance::alpha(232));
    glass.setColorAt(0, shine);
    glass.setColorAt(1, body);
    QColor rim = option->palette.color(QPalette::WindowText);
    rim.setAlpha(52);
    p->setPen(QPen(rim, 1));
    p->setBrush(glass);
    p->drawRoundedRect(r, 10, 10);
    p->restore();
  }
  void drawControl(ControlElement element, const QStyleOption *option,
                   QPainter *p, const QWidget *widget = nullptr) const override {
    if (element == CE_MenuItem) {
      if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option)) {
        auto copy = *item;
        // Fusion must not repaint an opaque panel over our alpha background.
        copy.palette.setColor(QPalette::Window, Qt::transparent);
        copy.palette.setColor(QPalette::Base, Qt::transparent);
        QProxyStyle::drawControl(element, &copy, p, widget);
        return;
      }
    }
    QProxyStyle::drawControl(element, option, p, widget);
  }
};
