#pragma once
#include "appearance.h"
#include "localized_widgets.h"
#include "pointer_gesture.h"
#include "ui_icons.h"
#include <QDialog>
#include <QElapsedTimer>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QStyleOptionButton>
#include <QTimer>
#include <QWindow>
#include <algorithm>
#include <cmath>
// Analytic material shading, rendered only on damage/input/palette changes.
// No GL context per control, framebuffer readback, idle animation or photo
// blur.
inline void paintGlass(QPainter &p, QRectF r, const QPalette &palette,
                       bool hover, bool pressed, bool selected, bool focus,
                       bool enabled, qreal radius = 11) {
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  r.adjust(1.5, 1.5, -1.5, -2.5);
  const auto ink = palette.color(QPalette::WindowText);
  const auto base = palette.color(QPalette::Window);
  auto alpha = [](QColor c, int a) {
    c.setAlpha(a);
    return c;
  };
  auto tint = selected ? palette.color(QPalette::Highlight) : base;
  QPainterPath shape;
  shape.addRoundedRect(r, radius, radius);
  p.setPen(Qt::NoPen);
  p.setBrush(alpha(Qt::black, pressed ? 4 : 8));
  p.drawRoundedRect(r.translated(0, 1.5), radius, radius);
  QLinearGradient body(r.topLeft(), r.bottomLeft());
  body.setColorAt(0,
                  alpha(tint.lighter(125), enabled ? (pressed ? 65 : 38) : 30));
  body.setColorAt(0.52, alpha(tint, selected ? 78 : (hover ? 40 : 18)));
  body.setColorAt(1, alpha(tint.darker(112), pressed ? 95 : 32));
  p.fillPath(shape, body);
  p.save();
  p.setClipPath(shape);
  QRadialGradient highlight(r.topLeft() + QPointF(r.width() * .24, -4),
                            std::max(r.width(), r.height()) * .82);
  highlight.setColorAt(0, alpha(Qt::white, enabled ? (pressed ? 14
                                                      : hover ? 65
                                                              : 42)
                                                   : 12));
  highlight.setColorAt(.6, alpha(Qt::white, 9));
  highlight.setColorAt(1, alpha(Qt::white, 0));
  p.fillRect(r, highlight);
  p.restore();
  QLinearGradient rim(r.topLeft(), r.bottomRight());
  rim.setColorAt(0, alpha(ink, enabled ? 72 : 30));
  rim.setColorAt(.46, alpha(ink, 22));
  rim.setColorAt(.8, alpha(ink, 32));
  rim.setColorAt(1, alpha(ink, enabled ? 48 : 20));
  p.setPen(QPen(rim, 1));
  p.setBrush(Qt::NoBrush);
  p.drawPath(shape);
  if (selected || focus) {
    auto accent = palette.color(QPalette::Highlight);
    p.setPen(QPen(alpha(accent, focus ? 220 : 150), focus ? 1.6 : 1));
    p.drawRoundedRect(r.adjusted(-.5, -.5, .5, .5), radius, radius);
  }
  p.restore();
}
class GlassButton : public UiPushButton {
public:
  explicit GlassButton(const QString &text = {}, QWidget *parent = nullptr)
      : UiPushButton(text, parent) {
    setProperty("prismGlass", true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover);
    setAutoDefault(false);
    setMinimumHeight(34);
  }
  void setSymbol(const QString &symbol, const QString &label = {}) {
    setProperty("prismSymbol", symbol);
    if (!label.isEmpty())
      setAccessibleName(label);
    setIcon(prismIcon(symbol, window()->palette().color(QPalette::WindowText),
                      window()->palette().color(QPalette::Highlight)));
    setIconSize(QSize(20, 20));
    updateGeometry();
    update();
  }
  QSize sizeHint() const override {
    const auto textSize = fontMetrics().size(Qt::TextShowMnemonic, text());
    const int iconWidth = icon().isNull() ? 0 : iconSize().width();
    return {std::max(34, textSize.width() + iconWidth +
                             (iconWidth && !text().isEmpty() ? 7 : 0) + 22),
            36};
  }
  QSize minimumSizeHint() const override { return sizeHint(); }

protected:
  void changeEvent(QEvent *e) override {
    UiPushButton::changeEvent(e);
    if (e->type() == QEvent::PaletteChange) {
      const auto symbol = property("prismSymbol").toString();
      if (!symbol.isEmpty())
        setIcon(prismIcon(symbol,
                          window()->palette().color(QPalette::WindowText),
                          window()->palette().color(QPalette::Highlight)));
    }
  }
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    paintGlass(p, rect(), window()->palette(), underMouse(), isDown(),
               isChecked(), hasFocus(), isEnabled());
    p.setRenderHint(QPainter::Antialiasing);
    QColor ink = window()->palette().color(QPalette::WindowText);
    if (!isEnabled())
      ink.setAlpha(95);
    const int iconWidth = icon().isNull() ? 0 : iconSize().width();
    const int textWidth =
        fontMetrics().size(Qt::TextShowMnemonic, text()).width();
    const int gap = iconWidth && textWidth ? 7 : 0;
    const qreal x = (width() - iconWidth - textWidth - gap) / 2.0;
    const qreal shift = isDown() ? 1 : 0;
    if (iconWidth) {
      QRectF ir(x, (height() - iconSize().height()) / 2.0 + shift, iconWidth,
                iconSize().height());
      const auto symbol = property("prismSymbol").toString();
      if (!symbol.isEmpty())
        drawPrismIcon(
            p, symbol, ir, ink,
            isEnabled() ? window()->palette().color(QPalette::Highlight) : ink);
      else
        icon().paint(&p, ir.toRect(), Qt::AlignCenter,
                     isEnabled() ? QIcon::Normal : QIcon::Disabled);
    }
    p.setPen(ink);
    p.setFont(font());
    p.drawText(QRectF(x + iconWidth + gap, shift, textWidth, height()),
               Qt::AlignCenter | Qt::TextShowMnemonic, text());
  }
};
class GlassDialog : public UiDialog {
  PointerGesture pointerGesture;

public:
  explicit GlassDialog(QWidget *parent = nullptr) : UiDialog(parent) {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    auto base = palette().color(QPalette::Window);
    base.setAlpha(PrismAppearance::alpha(225));
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(base);
    p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 22, 22);
    paintGlass(p, rect(), palette(), false, false, false, false, true, 22);
  }
  void mousePressEvent(QMouseEvent *e) override {
    pointerGesture.press(e, e->position(), size(), 85, true, false, false);
    QDialog::mousePressEvent(e);
  }
  void mouseMoveEvent(QMouseEvent *e) override {
    if (windowHandle() &&
        pointerGesture.move(e, e->position(), this, [this](Qt::Edges) {
          return windowHandle()->startSystemMove();
        }))
      return;
    QDialog::mouseMoveEvent(e);
  }
  void mouseReleaseEvent(QMouseEvent *e) override {
    pointerGesture.cancel(this);
    QDialog::mouseReleaseEvent(e);
  }
  void hideEvent(QHideEvent *e) override {
    pointerGesture.cancel(this);
    PointerGesture::releaseOwnedGrab(this);
    QDialog::hideEvent(e);
  }
  bool event(QEvent *e) override {
    if (e->type() == QEvent::WindowDeactivate ||
        e->type() == QEvent::UngrabMouse)
      pointerGesture.cancel(this);
    return QDialog::event(e);
  }
};
// Small independent overlay: only its 136x140 logical pixels are invalidated.
// The decoder never waits for animation, and animation never refreshes a photo.
class LoadingGlass final : public QWidget {
  QTimer timer;
  QElapsedTimer elapsed;
  bool loading = false;
  QPointer<QWidget> observedWindow;
  void sync() {
    if (loading && isVisible() && !window()->isMinimized()) {
      if (!timer.isActive())
        timer.start();
    } else
      timer.stop();
  }

public:
  explicit LoadingGlass(QWidget *parent) : QWidget(parent) {
    setObjectName("loadingHourglass");
    uiAccessible(this,"Wczytywanie zdjęcia");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(136, 140);
    timer.setInterval(33); // 30 fps; no work at all after completion or hide.
    connect(&timer, &QTimer::timeout, this, [this] {
      sync();
      update();
    });
    hide();
  }
  void setLoading(bool value) {
    if (loading == value)
      return;
    loading = value;
    if (value)
      elapsed.start();
    setVisible(value);
    raise();
    sync();
    update();
  }
  bool animating() const { return timer.isActive(); }
  bool isLoading() const { return loading; }

protected:
  void showEvent(QShowEvent *e) override {
    QWidget::showEvent(e);
    if (observedWindow != window()) {
      if (observedWindow)
        observedWindow->removeEventFilter(this);
      observedWindow = window();
      observedWindow->installEventFilter(this);
    }
    sync();
  }
  bool eventFilter(QObject *o, QEvent *e) override {
    if (o == observedWindow && e->type() == QEvent::WindowStateChange)
      sync();
    return QWidget::eventFilter(o, e);
  }
  void hideEvent(QHideEvent *e) override {
    timer.stop();
    QWidget::hideEvent(e);
  }
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    paintGlass(p, rect(), palette(), false, false, false, false, true, 24);
    p.save();
    p.translate(width() / 2., 52);
    const double phase =
        elapsed.isValid() ? (elapsed.elapsed() % 2200) / 2200. : 0;
    const double turn = std::clamp((phase - .82) / .18, 0., 1.);
    const double sand = std::min(phase / .82, 1.);
    p.rotate(180 * turn * turn * (3 - 2 * turn));
    auto ink = palette().color(QPalette::WindowText);
    auto accent = palette().color(QPalette::Highlight);
    QPainterPath glass;
    glass.moveTo(-18, -25);
    glass.cubicTo(-18, -10, -4, -8, -3, 0);
    glass.cubicTo(-4, 8, -18, 10, -18, 25);
    glass.lineTo(18, 25);
    glass.cubicTo(18, 10, 4, 8, 3, 0);
    glass.cubicTo(4, -8, 18, -10, 18, -25);
    glass.closeSubpath();
    p.setPen(QPen(ink, 1.4));
    auto fill = ink;
    fill.setAlpha(12);
    p.setBrush(fill);
    p.drawPath(glass);
    p.save();
    p.setClipPath(glass);
    QLinearGradient grains(0, -24, 0, 24);
    auto bright = accent.lighter(145);
    bright.setAlpha(230);
    accent.setAlpha(210);
    grains.setColorAt(0, bright);
    grains.setColorAt(1, accent);
    p.setPen(Qt::NoPen);
    p.setBrush(grains);
    p.drawRect(QRectF(-19, -24 + sand * 24, 38, 24 * (1 - sand)));
    QPainterPath pile;
    pile.moveTo(-19, 26);
    pile.lineTo(0, 24 - sand * 23);
    pile.lineTo(19, 26);
    pile.closeSubpath();
    p.drawPath(pile);
    if (turn == 0) {
      p.setPen(QPen(bright, 1.2));
      p.drawLine(QPointF(0, 0), QPointF(0, 24 - sand * 23));
      for (int i = 0; i < 3; ++i) {
        double y = std::fmod(phase * 100 + i * 7, 22.);
        p.drawPoint(QPointF((i % 2 ? .8 : -.8), y));
      }
    }
    p.restore();
    p.setPen(QPen(ink, 2.3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(-21, -27, 21, -27);
    p.drawLine(-21, 27, 21, 27);
    p.restore();
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(QRect(4, 98, width() - 8, 30), Qt::AlignCenter, PrismLanguage::text("Wczytywanie"));
  }
};
