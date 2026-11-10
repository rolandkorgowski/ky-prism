#pragma once
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QSizeGrip>
#include <QSplitter>
#include <QWidget>
#include <QWindow>
#include <functional>

// A click never starts a compositor move/resize. In particular, activation,
// propagated child presses and synthesized events cannot leave a native grab.
class PointerGesture {
  bool armed = false;
  QPointF origin;
  Qt::Edges edges;

public:
  bool pending() const { return armed; }
  void cancel(QWidget *owner) {
    if (!armed)
      return;
    armed = false;
    releaseOwnedGrab(owner);
  }
  static void releaseOwnedGrab(QWidget *owner) {
    auto grabber = QWidget::mouseGrabber();
    if (grabber && grabber->window() == owner)
      grabber->releaseMouse();
  }
  static bool interactive(QWidget *widget, QWidget *owner) {
    for (auto p = widget; p && p != owner; p = p->parentWidget())
      if (qobject_cast<QAbstractButton *>(p) ||
          qobject_cast<QAbstractItemView *>(p) ||
          qobject_cast<QSplitterHandle *>(p) || qobject_cast<QSizeGrip *>(p) ||
          qobject_cast<QLineEdit *>(p) || qobject_cast<QPlainTextEdit *>(p) ||
          p->objectName() == "photoView" || p->objectName() == "compareView")
        return true;
    return false;
  }
  void press(QMouseEvent *e, QPointF position, QSize size, int header,
             bool movable, bool resizable, bool control) {
    armed = false;
    if (control || e->button() != Qt::LeftButton ||
        !(e->buttons() & Qt::LeftButton) ||
        e->source() != Qt::MouseEventNotSynthesized ||
        !QRectF(QPointF(), size).contains(position))
      return;
    edges = {};
    if (resizable) {
      if (position.x() < 4)
        edges |= Qt::LeftEdge;
      if (position.x() >= size.width() - 4)
        edges |= Qt::RightEdge;
      if (position.y() < 4)
        edges |= Qt::TopEdge;
      if (position.y() >= size.height() - 4)
        edges |= Qt::BottomEdge;
    }
    if (!edges && (!movable || position.y() >= header))
      return;
    origin = position;
    armed = true;
  }
  bool move(QMouseEvent *e, QPointF position, QWidget *owner,
            const std::function<bool(Qt::Edges)> &begin) {
    if (!armed)
      return false;
    if (!(e->buttons() & Qt::LeftButton) ||
        e->source() != Qt::MouseEventNotSynthesized ||
        !owner->isActiveWindow() || !owner->isVisible() ||
        owner->isMinimized()) {
      cancel(owner);
      return false;
    }
    if ((position - origin).manhattanLength() <
        QApplication::startDragDistance())
      return false;
    const auto requestedEdges = edges;
    armed = false;
    releaseOwnedGrab(owner);
    return begin(requestedEdges);
  }
};
