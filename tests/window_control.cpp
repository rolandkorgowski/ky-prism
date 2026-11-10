#include "window_control.h"
#include <QCoreApplication>
#include <QProcess>
#include <iostream>
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  for (const auto &title :
       {QString("zdjęcie ' [] — ky-prism"), QString("ky-prism • Zdjęcia"), QString("ky-prism • Photos"),
        QString("zdjęcie — Prism")})
    for (int mode : {0, 1, 2})
      for (bool refuse : {false, true}) {
        QString script =
            QString::fromUtf8(
                R"LUA(local target={class="omarchy-prism",pid=123,title="%4",mapped=true,fullscreen=%1}
local candidates={{class="omarchy-prism",mapped=true,pid=999,title="other — Prism"},{class="omarchy-prism",mapped=true,pid=123,title="Ulubione zdjęcia"},target}
hl={get_windows=function(filter)
 local out={} for _,w in ipairs(candidates) do
  -- Match the real v0.56.2 query API: literal equality, NOT regular expressions.
  if (filter.class==nil or w.class==filter.class) and (filter.mapped==nil or w.mapped==filter.mapped) then table.insert(out,w) end
 end return out
 end,
dsp={window={fullscreen=function(options)assert(options.window==target);assert(options.mode=="maximized");assert(options.action=="%2");assert(options.layout_aware==false);return options end}},
dispatch=function(options)if not %3 then target.fullscreen=options.action=="unset" and 0 or 1 end end}
)LUA")
                .arg(mode)
                .arg(mode == 1 ? "unset" : "set")
                .arg(refuse ? "true" : "false")
                .arg(title) +
            prismMaximizeCommand(123);
        QProcess process;
        process.start("lua", {"-"});
        if (!process.waitForStarted(2000))
          return 1;
        process.write(script.toUtf8());
        process.closeWriteChannel();
        if (!process.waitForFinished(3000) ||
            (process.exitCode() == 0) == refuse) {
          std::cerr << process.readAllStandardError().constData();
          return 1;
        }
      }
  std::cout << "PASS: literal class query regression, main window only, "
               "maximize/restore/fullscreen, detect no effect\n";
  return 0;
}
