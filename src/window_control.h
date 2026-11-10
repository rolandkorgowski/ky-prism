#pragma once
#include <QString>
// Only a numeric PID enters Lua. The class can differ between XWayland/Wayland.
// Bypass layout-specific fullscreen handlers; maximize the actual native
// window.
inline QString prismMaximizeCommand(qint64 pid) {
  return QString::fromUtf8(
             R"LUA(for _, w in ipairs(hl.get_windows({mapped=true})) do
 if w.pid == %1 and (w.title == "ky-prism • Zdjęcia" or w.title == "ky-prism • Photos" or w.title:match(" — ky%-prism$") or w.title == "Prism • Omarchy Photos" or w.title:match(" — Prism$")) then
  local desired = w.fullscreen == 1 and 0 or 1
  hl.dispatch(hl.dsp.window.fullscreen({mode="maximized", action=desired==0 and "unset" or "set", layout_aware=false, window=w}))
  if w.fullscreen ~= desired then error("PRISM: Hyprland nie potwierdził maksymalizacji") end
  return
 end
end
error("PRISM: Nie znaleziono głównego okna dla PID %1"))LUA")
      .arg(pid);
}
