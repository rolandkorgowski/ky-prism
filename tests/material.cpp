#include "glass_widgets.h"
#include <QApplication>
#include <QtTest/QTest>
#include <iostream>
#include <stdexcept>
static void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
class PaintCounter : public QObject {
public:
  int frames = 0;
  bool eventFilter(QObject *, QEvent *event) override {
    if (event->type() == QEvent::Paint)
      ++frames;
    return false;
  }
};
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  try {
    QWidget root;
    root.resize(560, 320);
    LoadingGlass loader(&root);
    loader.move(210, 90);
    PaintCounter count;
    loader.installEventFilter(&count);
    root.show();
    loader.setLoading(true);
    QTest::qWait(150);
    check(loader.animating() && count.frames > 1, "loader did not animate");
    root.hide();
    QTest::qWait(80);
    check(!loader.animating(), "hidden loader timer running");
    int frames = count.frames;
    QTest::qWait(100);
    check(count.frames == frames, "hidden loader repaint");
    root.show();
    QTest::qWait(90);
    check(loader.animating(), "loader did not resume after show");
    root.showMinimized();
    QTest::qWait(70);
    check(!loader.animating(), "minimized loader timer running");
    root.showNormal();
    QTest::qWait(70);
    check(loader.animating(), "loader did not resume after restore");
    loader.setLoading(false);
    QTest::qWait(50);
    check(!loader.isVisible() && !loader.animating(),
          "completed loader retained animation");
    auto timers = loader.findChildren<QTimer *>().size();
    for (int i = 0; i < 200; ++i) {
      loader.setLoading(true);
      loader.setLoading(false);
    }
    check(loader.findChildren<QTimer *>().size() == timers,
          "timer allocation growth");
    QImage previous;
    for (bool dark : {false, true}) {
      QPalette palette;
      palette.setColor(QPalette::Window, QColor(dark ? "#18212e" : "#f3e6d4"));
      palette.setColor(QPalette::WindowText,
                       QColor(dark ? "#edf2ff" : "#3b2517"));
      palette.setColor(QPalette::Highlight,
                       QColor(dark ? "#94bcf4" : "#a4533e"));
      QImage sheet(720, 360, QImage::Format_ARGB32_Premultiplied);
      sheet.fill(palette.color(QPalette::Window));
      QPainter painter(&sheet);
      QStringList symbols{"openPhoto",  "folder",  "copyFiles",
                          "duplicates", "openUsb", "comparePhotos",
                          "zoomIn",     "zoomOut", "bookmarks",
                          "favorites",  "created", "modified"};
      for (int state = 0; state < 4; ++state)
        for (int i = 0; i < symbols.size(); ++i) {
          QRectF r(10 + i * 59, 12 + state * 60, 50, 46);
          paintGlass(painter, r, palette, state == 1, state == 2, state == 3,
                     state == 3, true, 13);
          drawPrismIcon(painter, symbols.at(i), r.adjusted(10, 8, -10, -8),
                        palette.color(QPalette::WindowText),
                        palette.color(QPalette::Highlight));
        }
      painter.end();
      if (!previous.isNull())
        check(previous != sheet, "theme did not affect material");
      previous = sheet;
      const auto folder = qEnvironmentVariable("PRISM_MATERIAL_SNAPSHOTS");
      if (!folder.isEmpty())
        sheet.save(folder +
                   (dark ? "/material-dark.png" : "/material-light.png"));
      QImage transparent(100, 40, QImage::Format_ARGB32_Premultiplied);
      transparent.fill(Qt::transparent);
      QPainter t(&transparent);
      paintGlass(t, transparent.rect(), palette, false, false, false, false,
                 true);
      t.end();
      check(transparent.pixelColor(50, 20).alpha() > 0 &&
                transparent.pixelColor(50, 20).alpha() < 200,
            "opaque glass surface");
    }
    GlassButton button("Kopia");
    button.setSymbol("copyFiles");
    button.show();
    auto size = button.sizeHint();
    button.setChecked(true);
    button.setDown(true);
    button.setEnabled(false);
    check(size == button.sizeHint(), "button state changed layout");
    QImage target(100, 40, QImage::Format_ARGB32_Premultiplied);
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 3000; ++i) {
      target.fill(Qt::transparent);
      QPainter p(&target);
      paintGlass(p, target.rect(), button.palette(), i % 2, false, false, false,
                 true);
    }
    std::cout << "PASS material: theme, alpha, stable geometry, "
                 "show/hide/minimize/restore, 200 cancellations; paint_us="
              << timer.nsecsElapsed() / 3000000.0 << '\n';
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
