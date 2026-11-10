#include "pointer_gesture.h"
#include <QtTest/QTest>
#include <iostream>
#include <stdexcept>
static void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
static QMouseEvent
mouse(QEvent::Type type, QPointF p, Qt::MouseButtons buttons,
      Qt::MouseEventSource source = Qt::MouseEventNotSynthesized) {
  return QMouseEvent(type, p, p, p,
                     type == QEvent::MouseButtonPress ? Qt::LeftButton
                                                      : Qt::NoButton,
                     buttons, Qt::NoModifier, source);
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  try {
    QWidget owner;
    owner.resize(600, 400);
    owner.show();
    owner.activateWindow();
    QTest::qWait(30);
    check(owner.isActiveWindow(), "test window not active");
    QSplitter splitter(&owner);
    splitter.addWidget(new QWidget);
    splitter.addWidget(new QWidget);
    check(PointerGesture::interactive(splitter.handle(1), &owner),
          "splitter handle interpreted as a window drag");
    PointerGesture gesture;
    int calls = 0;
    Qt::Edges requested;
    auto begin = [&](Qt::Edges edges) {
      ++calls;
      requested = edges;
      return true;
    };
    auto press = mouse(QEvent::MouseButtonPress, {200, 20}, Qt::LeftButton);
    auto drag = mouse(QEvent::MouseMove, {240, 20}, Qt::LeftButton);
    auto hover = mouse(QEvent::MouseMove, {240, 20}, Qt::NoButton);
    for (int i = 0; i < 100; ++i) {
      gesture.press(&press, {200, 20}, owner.size(), 80, true, true, false);
      check(calls == 0, "press started a native operation");
      gesture.cancel(&owner);
      check(!gesture.move(&drag, {240, 20}, &owner, begin),
            "release left armed gesture");
    }
    gesture.press(&press, {-2, -2}, owner.size(), 80, true, true, false);
    check(!gesture.pending(), "outside-window event started resize");
    auto synthetic = mouse(QEvent::MouseButtonPress, {200, 20}, Qt::LeftButton,
                           Qt::MouseEventSynthesizedByQt);
    gesture.press(&synthetic, {200, 20}, owner.size(), 80, true, true, false);
    check(!gesture.pending(), "synthetic activation armed move");
    gesture.press(&press, {200, 20}, owner.size(), 80, true, true, true);
    check(!gesture.pending(), "interactive control armed move");
    gesture.press(&press, {200, 20}, owner.size(), 80, false, false, false);
    check(!gesture.pending(), "fullscreen/maximized armed operation");
    gesture.press(&press, {200, 20}, owner.size(), 80, true, true, false);
    check(!gesture.move(&hover, {240, 20}, &owner, begin) && !gesture.pending(),
          "released button started move");
    gesture.press(&press, {200, 20}, owner.size(), 80, true, true, false);
    check(!gesture.move(&drag, {201, 20}, &owner, begin),
          "jitter started move");
    check(gesture.move(&drag, {240, 20}, &owner, begin) && calls == 1 &&
              !requested,
          "header drag not moved");
    check(!gesture.move(&drag, {250, 20}, &owner, begin) && calls == 1,
          "native operation repeated");
    calls = 0;
    gesture.press(&press, {1, 1}, owner.size(), 80, true, true, false);
    check(gesture.move(&drag, {30, 30}, &owner, begin) && calls == 1 &&
              requested == (Qt::LeftEdge | Qt::TopEdge),
          "corner drag not resized");
    calls = 0;
    gesture.press(&press, {200, 20}, owner.size(), 80, true, true, false);
    owner.hide();
    check(!gesture.move(&drag, {240, 20}, &owner, begin) && calls == 0,
          "hidden window started move");
    std::cout << "PASS pointer: 100 click/releases, synthetic/outside/control "
                 "events, threshold, resize, hide, single native operation\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
