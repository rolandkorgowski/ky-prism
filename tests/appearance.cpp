#include "appearance.h"
#include "menu_glass.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

static void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  QTemporaryDir config;
  qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
  QApplication app(argc, argv);
  try {
    const auto path = config.filePath("ky-prism/appearance.json");
    QDir().mkpath(QFileInfo(path).absolutePath());
    auto write = [&](const QByteArray &value) {
      QFile file(path);
      check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "configuration write");
      check(file.write(value) == value.size(), "configuration incomplete");
    };
    check(PrismAppearance::readOpacity({}, path) == -1, "missing config changed defaults");
    for (const auto &bad : {QByteArray("{}"), QByteArray("null"), QByteArray("false"),
                            QByteArray("{\"ui_opacity_percent\":\"95\"}"),
                            QByteArray("{\"ui_opacity_percent\":101}"),
                            QByteArray("{\"ui_opacity_percent\":94.5}"), QByteArray(5000, 'x')}) {
      write(bad);
      check(PrismAppearance::readOpacity({}, path) == -1, "invalid config accepted");
    }
    write("{\"ui_opacity_percent\":95}");
    check(PrismAppearance::readOpacity({}, path) == 95, "persistent opacity lost");
    for (const auto &bad : {QByteArray("-1"), QByteArray("101"), QByteArray("x"), QByteArray("nan")})
      check(PrismAppearance::readOpacity(bad, path) == 95, "invalid environment lost config fallback");
    check(PrismAppearance::readOpacity("0", path) == 0, "zero opacity");
    check(PrismAppearance::readOpacity("100", path) == 100, "full opacity");
    check(PrismAppearance::readOpacity("90", path) == 90, "environment precedence");

    const int percent = qEnvironmentVariable("PRISM_UI_OPACITY").isEmpty()
                            ? 95 : qEnvironmentVariableIntValue("PRISM_UI_OPACITY");
    const int expected = (percent * 255 + 50) / 100;
    check(PrismAppearance::alpha(175) == expected, "panel alpha did not change");
    check(PrismAppearance::alpha(228) == expected, "menu alpha did not change");
    MenuGlassStyle style;
    QStyleOption option;
    option.rect = QRect(0,0,160,80);
    option.palette.setColor(QPalette::Window, QColor("#18212e"));
    QImage image(160,80,QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    style.drawPrimitive(QStyle::PE_PanelMenu, &option, &painter);
    painter.end();
    check(image.pixelColor(80,40).alpha() == expected, "rendered menu opacity differs");
    // Editing config must not cause disk I/O or mid-frame changes on each paint.
    write("{\"ui_opacity_percent\":10}");
    for (int i=0;i<1000;++i)
      check(PrismAppearance::alpha(175) == expected, "cached opacity changed during repaint");
    std::cout << "PASS appearance: bounded config, validation, override, rendered alpha, cached reads\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
