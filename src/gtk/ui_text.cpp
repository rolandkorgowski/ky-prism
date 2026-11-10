#include "ui_text.h"
#include "icons.h"
#include "language.h"
#include <QHash>
#include <vector>

namespace {
struct Entry {
  QString text, tip, placeholder, title, accessible, metadata;
  bool hasText = false, hasTip = false, hasPlaceholder = false,
       hasTitle = false, hasAccessible = false, hasMetadata = false;
  std::vector<Gx::Callback> dynamic;
};
QHash<GtkWidget *, Entry> &registry() {
  static QHash<GtkWidget *, Entry> value;
  return value;
}
void forget(gpointer, GObject *object) {
  registry().remove(reinterpret_cast<GtkWidget *>(object));
}
Entry &entry(GtkWidget *widget) {
  auto &all = registry();
  auto it = all.find(widget);
  if (it == all.end()) {
    g_object_weak_ref(G_OBJECT(widget), forget, nullptr);
    it = all.insert(widget, Entry{});
  }
  return it.value();
}
void applyText(GtkWidget *widget, const QString &source) {
  const auto translated = PrismLanguage::text(source);
  if (!GTK_IS_LABEL(widget))
    return;
  if (gtk_label_get_use_underline(GTK_LABEL(widget)))
    gtk_label_set_text_with_mnemonic(
        GTK_LABEL(widget), Gx::u8(Ui::mnemonic(translated)).constData());
  else
    gtk_label_set_text(GTK_LABEL(widget), Gx::u8(translated).constData());
}
void applyMetadata(GtkWidget *widget, const QString &source) {
  if (!GTK_IS_TEXT_VIEW(widget))
    return;
  const auto bytes = PrismLanguage::metadata(source).toUtf8();
  gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget)),
                           bytes.constData(), bytes.size());
}
void applyAccessible(GtkWidget *widget, const QString &source) {
  gtk_accessible_update_property(
      GTK_ACCESSIBLE(widget), GTK_ACCESSIBLE_PROPERTY_LABEL,
      Gx::u8(PrismLanguage::text(source)).constData(), -1);
}
const char *iconKey = "prism-button-icon", *labelKey = "prism-button-label";
GtkWidget *decorate(GtkWidget *button, const QString &symbol,
                    const QString &caption, const QString &tooltip) {
  auto *content = Gx::hbox(7);
  gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
  if (!symbol.isEmpty()) {
    auto *icon = Icons::create(symbol);
    gtk_box_append(GTK_BOX(content), icon);
    g_object_set_data(G_OBJECT(button), iconKey, icon);
  }
  if (!caption.isEmpty()) {
    auto *label = gtk_label_new(nullptr);
    gtk_label_set_use_underline(GTK_LABEL(label), TRUE);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), button);
    gtk_box_append(GTK_BOX(content), label);
    g_object_set_data(G_OBJECT(button), labelKey, label);
    Ui::text(label, caption);
  } else
    Gx::addClass(button, "icon");
  gtk_button_set_child(GTK_BUTTON(button), content);
  if (!tooltip.isEmpty()) {
    Ui::tip(button, tooltip);
    if (caption.isEmpty())
      Ui::accessible(button, tooltip.section(" • ", 0, 0));
  }
  return button;
}
} // namespace

namespace Ui {
QString tr(const QString &source) { return PrismLanguage::text(source); }
QString mnemonic(const QString &translated) {
  QString out;
  out.reserve(translated.size() + 2);
  for (qsizetype i = 0; i < translated.size(); ++i) {
    const QChar c = translated.at(i);
    if (c == '_')
      out += "__";
    else if (c == '&' && i + 1 < translated.size() && translated.at(i + 1) == '&') {
      out += '&';
      ++i;
    } else if (c == '&')
      out += '_';
    else
      out += c;
  }
  return out;
}
void text(GtkWidget *label, const QString &source) {
  auto &e = entry(label);
  e.text = source;
  e.hasText = true;
  applyText(label, source);
}
void literal(GtkWidget *label, const QString &verbatim) {
  auto &e = entry(label);
  e.text.clear();
  e.hasText = false;
  if (GTK_IS_LABEL(label))
    gtk_label_set_text(GTK_LABEL(label), Gx::u8(verbatim).constData());
}
void literalTitle(GtkWindow *window, const QString &verbatim) {
  auto &e = entry(GTK_WIDGET(window));
  e.title.clear();
  e.hasTitle = false;
  gtk_window_set_title(window, Gx::u8(verbatim).constData());
}
QString sourceText(GtkWidget *label) {
  const auto it = registry().constFind(label);
  return it == registry().cend() ? QString() : it->text;
}
void metadata(GtkWidget *textView, const QString &source) {
  auto &e = entry(textView);
  e.metadata = source;
  e.hasMetadata = true;
  applyMetadata(textView, source);
}
void tip(GtkWidget *widget, const QString &source) {
  auto &e = entry(widget);
  e.tip = source;
  e.hasTip = true;
  gtk_widget_set_tooltip_text(
      widget, Gx::u8(PrismLanguage::text(source)).constData());
}
void placeholder(GtkWidget *widget, const QString &source) {
  auto &e = entry(widget);
  e.placeholder = source;
  e.hasPlaceholder = true;
  if (GTK_IS_ENTRY(widget))
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(widget), Gx::u8(PrismLanguage::text(source)).constData());
}
void title(GtkWindow *window, const QString &source) {
  auto &e = entry(GTK_WIDGET(window));
  e.title = source;
  e.hasTitle = true;
  gtk_window_set_title(window, Gx::u8(PrismLanguage::text(source)).constData());
}
void accessible(GtkWidget *widget, const QString &source) {
  auto &e = entry(widget);
  e.accessible = source;
  e.hasAccessible = true;
  applyAccessible(widget, source);
}
void dynamic(GtkWidget *owner, Gx::Callback apply) {
  entry(owner).dynamic.push_back(std::move(apply));
}
void retranslate() {
  // Hooks may create widgets (which registers them): walk a stable snapshot.
  const auto widgets = registry().keys();
  for (auto *widget : widgets) {
    const auto it = registry().constFind(widget);
    if (it == registry().cend())
      continue;
    const Entry e = *it;
    if (e.hasText)
      applyText(widget, e.text);
    if (e.hasMetadata)
      applyMetadata(widget, e.metadata);
    if (e.hasTip)
      gtk_widget_set_tooltip_text(
          widget, Gx::u8(PrismLanguage::text(e.tip)).constData());
    if (e.hasPlaceholder && GTK_IS_ENTRY(widget))
      gtk_entry_set_placeholder_text(
          GTK_ENTRY(widget),
          Gx::u8(PrismLanguage::text(e.placeholder)).constData());
    if (e.hasTitle && GTK_IS_WINDOW(widget))
      gtk_window_set_title(GTK_WINDOW(widget),
                           Gx::u8(PrismLanguage::text(e.title)).constData());
    if (e.hasAccessible)
      applyAccessible(widget, e.accessible);
    for (const auto &hook : e.dynamic)
      hook();
  }
}
GtkWidget *label(const QString &source, const char *cssClass) {
  auto *widget = gtk_label_new(nullptr);
  gtk_label_set_xalign(GTK_LABEL(widget), 0.0f);
  if (cssClass)
    Gx::addClass(widget, cssClass);
  text(widget, source);
  return widget;
}
GtkWidget *button(const QString &symbol, const QString &caption,
                  const QString &tooltip, Gx::Callback clicked) {
  auto *widget = decorate(gtk_button_new(), symbol, caption, tooltip);
  if (clicked)
    Gx::on(widget, "clicked", std::move(clicked));
  return widget;
}
GtkWidget *toggle(const QString &symbol, const QString &caption,
                  const QString &tooltip, Gx::Callback toggled) {
  auto *widget = decorate(gtk_toggle_button_new(), symbol, caption, tooltip);
  if (toggled)
    Gx::on(widget, "toggled", std::move(toggled));
  return widget;
}
GtkWidget *buttonIcon(GtkWidget *button) {
  return GTK_WIDGET(g_object_get_data(G_OBJECT(button), iconKey));
}
GtkWidget *buttonLabel(GtkWidget *button) {
  return GTK_WIDGET(g_object_get_data(G_OBJECT(button), labelKey));
}
} // namespace Ui
