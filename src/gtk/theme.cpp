#include "theme.h"
#include "settings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>
#include <memory>
#include <vector>

namespace {
struct State {
  Theme::Palette palette;
  int opacity = 94;
  bool persisted = false, installed = false;
  GtkCssProvider *provider = nullptr;
  GSettings *desktop = nullptr;
  std::unique_ptr<QFileSystemWatcher> watcher;
  QString systemTheme;
  std::vector<GtkWidget *> redraw;
  std::vector<std::pair<GtkWidget *, Gx::Callback>> listeners;
};
State &state() {
  static State value;
  return value;
}
QString omarchyBase() {
  return QDir::homePath() + "/.local/state/omarchy/current";
}
QString omarchyFile() { return omarchyBase() + "/theme/colors.toml"; }
double luminance(const QColor &c) {
  return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
}
QColor mix(const QColor &a, const QColor &b, double t) {
  return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                          a.greenF() + (b.greenF() - a.greenF()) * t,
                          a.blueF() + (b.blueF() - a.blueF()) * t);
}
QString css(const QColor &c, double alpha = 1.0) {
  return QString("rgba(%1,%2,%3,%4)")
      .arg(c.red())
      .arg(c.green())
      .arg(c.blue())
      .arg(std::clamp(alpha, 0.0, 1.0), 0, 'f', 3);
}
GSettings *desktopSettings() {
  auto *source = g_settings_schema_source_get_default();
  if (!source)
    return nullptr;
  auto *schema = g_settings_schema_source_lookup(
      source, "org.gnome.desktop.interface", TRUE);
  if (!schema)
    return nullptr;
  g_settings_schema_unref(schema);
  return g_settings_new("org.gnome.desktop.interface");
}
bool hasKey(GSettings *settings, const char *key) {
  GSettingsSchema *schema = nullptr;
  g_object_get(settings, "settings-schema", &schema, nullptr);
  const bool found = schema && g_settings_schema_has_key(schema, key);
  if (schema)
    g_settings_schema_unref(schema);
  return found;
}
QString settingString(GSettings *settings, const char *key) {
  if (!settings || !hasKey(settings, key))
    return {};
  g_autofree char *value = g_settings_get_string(settings, key);
  return Gx::qs(value);
}
Theme::Palette desktopPalette() {
  auto &s = state();
  Theme::Palette p;
  const auto scheme = settingString(s.desktop, "color-scheme");
  const auto theme = s.systemTheme.toLower();
  p.dark = scheme == "prefer-dark" ||
           (scheme != "prefer-light" && theme.endsWith("dark")) ||
           (scheme.isEmpty() && theme.isEmpty());
  if (scheme == "default" && !theme.endsWith("dark"))
    p.dark = false;
  static const QHash<QString, QString> yaru{
      {"bark", "#787859"},          {"sage", "#657b69"},
      {"olive", "#4b8501"},         {"viridian", "#03875b"},
      {"prussiangreen", "#308280"}, {"blue", "#0073e5"},
      {"purple", "#7764d8"},        {"magenta", "#b34cb3"},
      {"red", "#da3450"},           {"yellow", "#c88800"},
      {"wartybrown", "#b39169"}};
  static const QHash<QString, QString> gnome{
      {"blue", "#3584e4"},   {"teal", "#2190a4"}, {"green", "#3a944a"},
      {"yellow", "#c88800"}, {"orange", "#ed5b00"}, {"red", "#e62d42"},
      {"pink", "#d56199"},   {"purple", "#9141ac"}, {"slate", "#6f8396"}};
  QColor accent;
  if (theme.startsWith("yaru")) {
    accent = QColor("#e95420");
    for (auto it = yaru.cbegin(); it != yaru.cend(); ++it)
      if (theme.contains("-" + it.key()))
        accent = QColor(it.value());
  } else {
    const auto name = settingString(s.desktop, "accent-color");
    accent = QColor(gnome.value(name, p.dark ? "#8bb9ff" : "#1c71d8"));
  }
  if (p.dark) {
    p.background = QColor("#1c1f26");
    p.foreground = QColor("#e8eaee");
    // Keep the accent legible as an outline on the dark material.
    if (luminance(accent) < 0.32)
      accent = mix(accent, QColor(Qt::white), 0.38);
  } else {
    p.background = QColor("#f3f4f6");
    p.foreground = QColor("#1b1f27");
    if (luminance(accent) > 0.55)
      accent = mix(accent, QColor(Qt::black), 0.30);
  }
  p.accent = accent;
  return p;
}
void notify() {
  auto &s = state();
  for (auto *widget : s.redraw)
    gtk_widget_queue_draw(widget);
  // Listeners may register further listeners; iterate over a copy.
  const auto listeners = s.listeners;
  for (const auto &entry : listeners)
    entry.second();
}
void apply() {
  auto &s = state();
  if (!s.provider)
    return;
  const auto text = Theme::stylesheet(s.palette, s.opacity).toUtf8();
  gtk_css_provider_load_from_string(s.provider, text.constData());
  if (auto *settings = gtk_settings_get_default())
    g_object_set(settings, "gtk-application-prefer-dark-theme",
                 gboolean(s.palette.dark), nullptr);
  notify();
}
void forget(gpointer, GObject *object) {
  auto &s = state();
  auto *widget = reinterpret_cast<GtkWidget *>(object);
  s.redraw.erase(std::remove(s.redraw.begin(), s.redraw.end(), widget),
                 s.redraw.end());
  s.listeners.erase(std::remove_if(s.listeners.begin(), s.listeners.end(),
                                   [widget](const auto &entry) {
                                     return entry.first == widget;
                                   }),
                    s.listeners.end());
}
void watchOmarchy() {
  auto &s = state();
  if (!s.watcher)
    return;
  for (const QString &path :
       {omarchyBase(), omarchyBase() + "/theme", omarchyFile()})
    if (QFileInfo::exists(path) && !s.watcher->files().contains(path) &&
        !s.watcher->directories().contains(path))
      s.watcher->addPath(path);
}
} // namespace

namespace Theme {
GdkRGBA rgba(const QColor &color, double alpha) {
  return GdkRGBA{float(color.redF()), float(color.greenF()),
                 float(color.blueF()), float(alpha)};
}
const Palette &palette() { return state().palette; }
int opacity() { return state().opacity; }
bool opacityPersisted() { return state().persisted; }
bool compositorBlurs() {
  // Hyprland (Omarchy) blurs behind translucent surfaces; Mutter does not.
  return !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE");
}
Palette paletteFromToml(const QString &toml, const Palette &fallback) {
  auto color = [&](const QString &key, const QColor &otherwise) {
    QRegularExpression re("(?:^|\\n)\\s*" + key +
                          "\\s*=\\s*[\"'](#[0-9a-fA-F]{6})[\"']");
    const auto match = re.match(toml);
    return match.hasMatch() ? QColor(match.captured(1)) : otherwise;
  };
  Palette p = fallback;
  p.background = color("background", fallback.background);
  p.foreground = color("foreground", fallback.foreground);
  p.accent = color("accent", fallback.accent);
  p.dark = luminance(p.background) < 0.5;
  return p;
}
void reload(const QString &toml) {
  auto &s = state();
  QString data = toml;
  bool omarchy = !data.isEmpty();
  if (data.isEmpty()) {
    QFile file(omarchyFile());
    if (file.open(QIODevice::ReadOnly)) {
      data = QString::fromUtf8(file.read(65536));
      omarchy = true;
    }
  }
  if (omarchy) {
    Palette fallback;
    fallback.background = QColor("#1e222c");
    fallback.foreground = QColor("#e6e6e6");
    fallback.accent = QColor("#8bb9ff");
    s.palette = paletteFromToml(data, fallback);
    s.palette.omarchy = true;
  } else
    s.palette = desktopPalette();
  watchOmarchy();
  apply();
}
void setOpacity(int percent, bool persist) {
  auto &s = state();
  percent = std::clamp(percent, 0, 100);
  if (persist)
    s.persisted =
        PrismSettings::saveOpacity(PrismSettings::appearanceFile(), percent);
  if (s.opacity == percent)
    return;
  s.opacity = percent;
  apply();
}
void redrawOnChange(GtkWidget *widget) {
  auto &s = state();
  if (std::find(s.redraw.begin(), s.redraw.end(), widget) != s.redraw.end())
    return;
  s.redraw.push_back(widget);
  g_object_weak_ref(G_OBJECT(widget), forget, nullptr);
}
void onChange(GtkWidget *owner, Gx::Callback run) {
  auto &s = state();
  s.listeners.emplace_back(owner, std::move(run));
  g_object_weak_ref(G_OBJECT(owner), forget, nullptr);
}
void install() {
  auto &s = state();
  if (s.installed)
    return;
  s.installed = true;
  auto *settings = gtk_settings_get_default();
  if (settings) {
    g_autofree char *name = nullptr;
    g_object_get(settings, "gtk-theme-name", &name, nullptr);
    s.systemTheme = Gx::qs(name);
    // One deterministic base stylesheet on every desktop: the material below
    // restyles each control it uses, the built-in theme supplies the rest.
    g_object_set(settings, "gtk-theme-name", "Default", nullptr);
  }
  s.desktop = desktopSettings();
  if (s.desktop)
    Gx::connect(s.desktop, "changed",
                     +[](GSettings *settings, const char *key,
                                    gpointer) {
                       const QString name = Gx::qs(key);
                       if (name == "gtk-theme") {
                         g_autofree char *theme =
                             g_settings_get_string(settings, "gtk-theme");
                         state().systemTheme = Gx::qs(theme);
                       }
                       if (name == "color-scheme" || name == "accent-color" ||
                           name == "gtk-theme")
                         Theme::reload();
                     },
                     nullptr);
  s.watcher = std::make_unique<QFileSystemWatcher>();
  QObject::connect(s.watcher.get(), &QFileSystemWatcher::directoryChanged,
                   s.watcher.get(), [] { Theme::reload(); });
  QObject::connect(s.watcher.get(), &QFileSystemWatcher::fileChanged,
                   s.watcher.get(), [] { Theme::reload(); });
  const int stored = PrismSettings::storedOpacity(
      qgetenv("PRISM_UI_OPACITY"), PrismSettings::appearanceFile());
  s.persisted = stored >= 0;
  s.opacity =
      stored >= 0 ? stored : PrismSettings::defaultOpacity(compositorBlurs());
  s.provider = gtk_css_provider_new();
  if (auto *display = gdk_display_get_default())
    gtk_style_context_add_provider_for_display(
        display, GTK_STYLE_PROVIDER(s.provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
  reload();
}
void shutdown() {
  auto &s = state();
  s.watcher.reset();
  if (s.desktop) {
    g_signal_handlers_disconnect_by_data(s.desktop, nullptr);
    g_clear_object(&s.desktop);
  }
  if (s.provider) {
    if (auto *display = gdk_display_get_default())
      gtk_style_context_remove_provider_for_display(
          display, GTK_STYLE_PROVIDER(s.provider));
    g_clear_object(&s.provider);
  }
  s.redraw.clear();
  s.listeners.clear();
  s.installed = false;
}
QString stylesheet(const Palette &p, int opacityPercent) {
  const double a = PrismSettings::alphaFor(opacityPercent);
  // Dialogs, menus and tooltips carry text over arbitrary content: they stay
  // practically opaque, whatever the chrome slider says, so text behind them
  // never shows through.
  const double solid = std::max(a, 0.97);
  const QColor bg = p.background, fg = p.foreground, accent = p.accent;
  const QColor raised = mix(bg, fg, p.dark ? 0.08 : 0.04);
  const QColor onAccent =
      luminance(accent) > 0.55 ? QColor("#11151c") : QColor("#ffffff");
  QString t = QString::fromUtf8(R"CSS(
window.prism, window.prism-dialog { background: none; color: FG; }
window.prism.csd, window.prism-dialog.csd { border-radius: 14px; }
window.prism.maximized, window.prism.fullscreen, window.prism.tiled { border-radius: 0; }
.prism-root { background-color: GLASS; color: FG; border: 1px solid EDGE; border-radius: 14px; FONT }
.prism-dialog-root { background-color: SOLID; color: FG; border: 1px solid EDGE2; border-radius: 14px; FONT }
window.maximized .prism-root, window.fullscreen .prism-root, window.tiled .prism-root { border-radius: 0; border-width: 0; }
.prism-root label, .prism-dialog-root label { color: FG; }
.prism-root selection, .prism-dialog-root selection { background-color: ACCENT45; color: FG; }

.prism-header { padding: 7px 8px 3px 14px; }
.prism-title { font-weight: 600; font-size: 1.04em; }
.prism-subtitle { color: FG60; font-size: 0.9em; }
.prism-toolbar { padding: 4px 10px 9px 10px; border-bottom: 1px solid EDGE; }
.prism-tabs { padding: 4px 10px 0 10px; border-bottom: 1px solid EDGE; }
.prism-sidebar { padding: 10px 10px 8px 10px; }
.prism-footer { padding: 5px 14px 6px 14px; border-top: 1px solid EDGE; font-size: 0.92em; }
.prism-dim { color: FG70; }
.prism-section { font-size: 0.8em; font-weight: 700; letter-spacing: 1.4px; color: FG60; }
.prism-stage { background-color: #131313; }
.prism-caption { padding: 2px 4px 2px 10px; background-color: GLASS; border-bottom: 1px solid EDGE; font-size: 0.95em; }
.prism-welcome { background-color: WELCOME; }
.prism-hello { font-size: 1.55em; font-weight: 600; }
.prism-exif { border-left: 1px solid EDGE; }
.prism-exif textview, .prism-exif textview text { background: none; color: FG; }
.prism-exif textview { padding: 14px; font-size: 0.93em; }
.prism-eyebrow { font-size: 0.78em; font-weight: 700; letter-spacing: 1.2px; color: ACCENT; }
.prism-heading { font-size: 1.6em; font-weight: 600; }
.prism-product { font-size: 2.1em; font-weight: 600; }
.prism-author { font-size: 1.3em; font-weight: 500; }
.prism-card { background-color: FG06; border: 1px solid EDGE; border-radius: 12px; padding: 12px; }
.prism-dialog-body { padding: 18px 22px 18px 22px; }

.prism-root button, .prism-dialog-root button, popover.prism-popover button {
  all: unset; min-height: 32px; min-width: 32px; padding: 0 11px; border-radius: 10px;
  color: FG; border: 1px solid EDGE2;
  background-image: linear-gradient(to bottom, RAISED55, RAISED30);
  box-shadow: inset 0 1px FG10, 0 1px 2px rgba(0,0,0,0.12);
  transition: background-color 120ms ease-out, border-color 120ms ease-out, box-shadow 120ms ease-out;
}
.prism-root button:hover, .prism-dialog-root button:hover { border-color: ACCENT70; background-image: linear-gradient(to bottom, RAISED70, ACCENT18); }
.prism-root button:active, .prism-dialog-root button:active { background-image: none; background-color: ACCENT30; box-shadow: inset 0 1px 2px rgba(0,0,0,0.25); }
.prism-root button:checked, .prism-dialog-root button:checked { background-image: none; background-color: ACCENT30; border-color: ACCENT80; box-shadow: inset 0 1px FG10; }
.prism-root button:focus-visible, .prism-dialog-root button:focus-visible { outline: 2px solid ACCENT90; outline-offset: 1px; }
.prism-root button:disabled, .prism-dialog-root button:disabled { opacity: 0.42; }
.prism-root button.icon, .prism-dialog-root button.icon { padding: 0; }
.prism-root button.suggested, .prism-dialog-root button.suggested { background-image: none; background-color: ACCENT; border-color: ACCENT; color: ONACCENT; font-weight: 600; }
.prism-root button.suggested label, .prism-dialog-root button.suggested label { color: ONACCENT; }
.prism-root button.suggested:hover, .prism-dialog-root button.suggested:hover { box-shadow: 0 0 0 3px ACCENT30; }
.prism-root button.destructive:hover, .prism-dialog-root button.destructive:hover { border-color: #e24659; background-image: none; background-color: rgba(226,70,89,0.22); }

.toolgroup { background-color: FG05; border: 1px solid EDGE; border-radius: 12px; padding: 2px; }
.toolgroup button { border-color: transparent; background-image: none; box-shadow: none; min-height: 30px; min-width: 30px; border-radius: 9px; }
.toolgroup button:hover { background-image: none; background-color: FG10; border-color: transparent; }
.toolgroup button:checked { background-color: ACCENT30; border-color: ACCENT70; }
.toolgroup button:active { background-color: ACCENT30; }
.prism-zoom { min-width: 58px; font-feature-settings: "tnum"; font-weight: 600; }
.prism-root button.windowcontrol { border-color: transparent; background-image: none; box-shadow: none; min-height: 28px; min-width: 28px; border-radius: 9px; padding: 0; }
.prism-root button.windowcontrol:hover { background-color: FG10; }
.prism-root button.windowcontrol.close:hover, .prism-dialog-root button.windowcontrol.close:hover { background-color: #e24659; }
.prism-dialog-root button.windowcontrol { border-color: transparent; background-image: none; box-shadow: none; min-height: 30px; min-width: 30px; padding: 0; }
.prism-root button.flat, .prism-dialog-root button.flat { border-color: transparent; background-image: none; box-shadow: none; }
.prism-root button.flat:hover, .prism-dialog-root button.flat:hover { background-color: FG10; }
.prism-root button.small { min-height: 24px; min-width: 24px; border-radius: 7px; padding: 0 6px; }

.prism-root entry, .prism-dialog-root entry {
  all: unset; min-height: 32px; padding: 0 10px; border-radius: 10px; color: FG; caret-color: FG;
  border: 1px solid EDGE2; background-color: FG05; transition: border-color 120ms ease-out, box-shadow 120ms ease-out;
}
.prism-root entry:focus-within, .prism-dialog-root entry:focus-within { border-color: ACCENT; box-shadow: 0 0 0 3px ACCENT22; }
.prism-root entry placeholder, .prism-dialog-root entry placeholder { color: FG50; }

.prism-root scrolledwindow, .prism-dialog-root scrolledwindow { background: none; }
.prism-root listview, .prism-root columnview, .prism-root gridview,
.prism-dialog-root listview, .prism-dialog-root columnview, .prism-dialog-root gridview { background: none; color: FG; }
.prism-root columnview > header > button, .prism-dialog-root columnview > header > button {
  all: unset; padding: 6px 9px; font-size: 0.82em; font-weight: 700; letter-spacing: 0.4px; color: FG70;
  border-bottom: 1px solid EDGE; border-radius: 0; background: none; box-shadow: none; min-height: 20px;
}
.prism-root columnview > header > button:hover, .prism-dialog-root columnview > header > button:hover { color: FG; background-color: FG06; }
.prism-root columnview > listview > row, .prism-dialog-root columnview > listview > row { border-radius: 8px; padding: 0; color: FG; }
.prism-root columnview > listview > row > cell, .prism-dialog-root columnview > listview > row > cell { padding: 6px 9px; }
.prism-root columnview > listview > row:hover, .prism-dialog-root columnview > listview > row:hover { background-color: FG06; }
.prism-root columnview > listview > row:selected, .prism-dialog-root columnview > listview > row:selected { background-color: ACCENT30; color: FG; }
.prism-root columnview:focus-within > listview > row:selected, .prism-dialog-root columnview:focus-within > listview > row:selected { background-color: ACCENT45; }
.prism-root gridview > child { border-radius: 11px; padding: 5px; margin: 2px; border: 1px solid transparent; }
.prism-root gridview > child:hover, .prism-dialog-root gridview > child:hover { background-color: FG06; }
.prism-root gridview > child:selected, .prism-dialog-root gridview > child:selected { background-color: ACCENT22; border-color: ACCENT80; }
.prism-dialog-root gridview > child { border-radius: 11px; padding: 5px; margin: 2px; border: 1px solid transparent; }
.prism-root gridview > child box, .prism-dialog-root gridview > child box { margin: 0; border-spacing: 3px; }
.prism-rowhead { font-weight: 700; }
.prism-thumbname { font-size: 0.9em; }
.prism-thumbmeta { font-size: 0.8em; color: FG60; }

scrollbar { background: none; border: none; }
scrollbar slider { min-width: 6px; min-height: 6px; border-radius: 6px; border: 3px solid transparent; background-clip: padding-box; background-color: FG30; }
scrollbar slider:hover, scrollbar slider:active { background-color: ACCENT; }
.prism-root paned > separator, .prism-dialog-root paned > separator { min-width: 1px; min-height: 1px; background-image: none; background-color: EDGE; background-clip: content-box; }

scale.prism-scale > trough { background-color: FG18; }
scale.prism-scale > trough > highlight { background-color: ACCENT; }
scale.prism-scale > trough > slider { background-color: FG; border: 1px solid EDGE2; box-shadow: 0 1px 3px rgba(0,0,0,0.35); }
scale.prism-scale:focus-visible > trough > slider { box-shadow: 0 0 0 3px ACCENT45; }
.prism-root progressbar > trough, .prism-dialog-root progressbar > trough { min-height: 6px; border-radius: 4px; background-color: FG15; border: none; }
.prism-root progressbar > trough > progress, .prism-dialog-root progressbar > trough > progress { min-height: 6px; border-radius: 4px; background-color: ACCENT; border: none; }

popover.prism-popover > contents { background-color: SOLID; color: FG; border: 1px solid EDGE2; border-radius: 13px; padding: 14px; box-shadow: 0 6px 24px rgba(0,0,0,0.35); }
popover.prism-popover > arrow { background-color: SOLID; border: 1px solid EDGE2; }
popover.prism-popover label { color: FG; }
popover.prism-menu > contents { padding: 6px; }
popover.prism-menu button { min-width: 230px; min-height: 34px; padding: 0 10px; }
popover.prism-menu separator { margin: 4px 6px; background-color: FG10; }
tooltip { background-color: TIP; border: 1px solid EDGE2; border-radius: 8px; padding: 4px 8px; box-shadow: none; }
tooltip, tooltip label { color: FG; }

.prism-tab { padding: 2px 4px 2px 12px; border-radius: 9px 9px 0 0; border-bottom: 2px solid transparent; }
.prism-tab:hover { background-color: FG06; }
.prism-tab.active { background-color: FG10; border-bottom-color: ACCENT; }
.prism-root .prism-tab button.tabtitle { border-color: transparent; background-image: none; box-shadow: none; padding: 0 4px 0 0; min-width: 60px; }
.prism-root .prism-tab button.tabclose { border-color: transparent; background-image: none; box-shadow: none; min-height: 22px; min-width: 22px; padding: 0; border-radius: 7px; }
.prism-root .prism-tab button.tabclose:hover { background-color: FG18; }
.prism-root notebook, .prism-dialog-root stackswitcher { background: none; }
.prism-dialog-root stackswitcher button { border-radius: 9px; margin-right: 4px; }
)CSS");
  const QString font =
      p.omarchy
          ? "font-family: \"Inter\", \"Adwaita Sans\", \"Noto Sans\", sans-serif;"
          : "";
  const QList<QPair<QString, QString>> values{
      {"ONACCENT", onAccent.name()},
      {"WELCOME", css(bg, std::min(1.0, a * 0.35))},
      {"GLASS", css(bg, a)},
      {"SOLID", css(bg, solid)},
      {"TIP", css(mix(bg, fg, 0.06), 0.97)},
      {"RAISED70", css(raised, 0.70)},
      {"RAISED55", css(raised, 0.55)},
      {"RAISED30", css(raised, 0.30)},
      {"ACCENT90", css(accent, 0.90)},
      {"ACCENT80", css(accent, 0.80)},
      {"ACCENT70", css(accent, 0.70)},
      {"ACCENT45", css(accent, 0.45)},
      {"ACCENT30", css(accent, 0.30)},
      {"ACCENT22", css(accent, 0.22)},
      {"ACCENT18", css(accent, 0.18)},
      {"ACCENT", accent.name()},
      {"EDGE2", css(fg, 0.20)},
      {"EDGE", css(fg, 0.12)},
      {"FG70", css(fg, 0.74)},
      {"FG60", css(fg, 0.62)},
      {"FG50", css(fg, 0.50)},
      {"FG30", css(fg, 0.30)},
      {"FG18", css(fg, 0.18)},
      {"FG15", css(fg, 0.15)},
      {"FG10", css(fg, 0.10)},
      {"FG06", css(fg, 0.06)},
      {"FG05", css(fg, 0.05)},
      {"FONT", font},
      {"FG", fg.name()}};
  for (const auto &entry : values)
    t.replace(entry.first, entry.second);
  return t;
}
} // namespace Theme
