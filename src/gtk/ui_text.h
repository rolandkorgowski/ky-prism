#pragma once
#include "gtkbase.h"

// Source UI text (Polish) is stored next to each widget and rendered through
// the reviewed catalog. Switching language re-renders every registered widget
// once; nothing is translated on paint or by a timer.
namespace Ui {
QString tr(const QString &source);
// Qt-style "&Otwórz" accelerators → GTK "_Otwórz".
QString mnemonic(const QString &translated);
void text(GtkWidget *label, const QString &source);
// Verbatim text (file names, paths): shown as is and never re-translated.
void literal(GtkWidget *label, const QString &verbatim);
void literalTitle(GtkWindow *window, const QString &verbatim);
void metadata(GtkWidget *textView, const QString &source);
void tip(GtkWidget *widget, const QString &source);
void placeholder(GtkWidget *entry, const QString &source);
void title(GtkWindow *window, const QString &source);
void accessible(GtkWidget *widget, const QString &source);
QString sourceText(GtkWidget *label);
// Arbitrary re-render hook, removed automatically with `owner`.
void dynamic(GtkWidget *owner, Gx::Callback apply);
void retranslate();

// Small widget factories shared by the window and its dialogs.
GtkWidget *label(const QString &source, const char *cssClass = nullptr);
// Button with an optional vector icon and translated caption.
GtkWidget *button(const QString &symbol, const QString &caption,
                  const QString &tooltip = {}, Gx::Callback clicked = {});
GtkWidget *toggle(const QString &symbol, const QString &caption,
                  const QString &tooltip = {}, Gx::Callback toggled = {});
GtkWidget *buttonIcon(GtkWidget *button);
GtkWidget *buttonLabel(GtkWidget *button);
} // namespace Ui
