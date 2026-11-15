#include "print.h"
#include "document.h"
#include <gtk/gtkunixprint.h>
#include <cairo-pdf.h>
#include <cairo-ps.h>
#include "language.h"
#include "ui_text.h"
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMap>
#include <QStandardPaths>
#include <QThreadPool>
#include <QtConcurrent>
#include <cmath>
#include <cstring>
#include <numbers>

namespace {
constexpr double captionPoints = 11.0;

// Polish plural: 1 strona, 2–4 strony (but 12–14 stron), 5+ stron.
int pluralForm(int n) {
  if (n == 1)
    return 0;
  const int last = n % 10, lastTwo = n % 100;
  return last >= 2 && last <= 4 && !(lastTwo >= 12 && lastTwo <= 14) ? 1 : 2;
}
QString counted(int n, const char *one, const char *few, const char *many) {
  const char *forms[] = {one, few, many};
  return QString::fromUtf8(forms[pluralForm(n)]).arg(n);
}
QString sentMessage(int pages) {
  return counted(pages, "Wysłano do drukarki: %1 strona", "Wysłano do drukarki: %1 strony",
                 "Wysłano do drukarki: %1 stron");
}
QString pdfMessage(int pages) {
  return counted(pages, "Zapisano PDF: %1 strona", "Zapisano PDF: %1 strony", "Zapisano PDF: %1 stron");
}

struct Preset {
  const char *label;
  PrintOptions::Size size;
  double widthMm, heightMm;
};
// Order of the "Rozmiar zdjęcia" list.
const Preset presets[] = {
    {"Dopasuj do strony", PrintOptions::Fit, 0, 0},
    {"Wypełnij stronę (przycięcie)", PrintOptions::Fill, 0, 0},
    {"Rozmiar rzeczywisty (DPI z pliku)", PrintOptions::Actual, 0, 0},
    {"9 × 13 cm", PrintOptions::Fixed, 90, 130},
    {"10 × 15 cm", PrintOptions::Fixed, 100, 150},
    {"13 × 18 cm", PrintOptions::Fixed, 130, 180},
    {"15 × 21 cm", PrintOptions::Fixed, 150, 210},
    {"20 × 30 cm", PrintOptions::Fixed, 200, 300},
    {"A5 (14,8 × 21 cm)", PrintOptions::Fixed, 148, 210},
    {"A4 (21 × 29,7 cm)", PrintOptions::Fixed, 210, 297},
    {"Własny rozmiar", PrintOptions::Custom, 0, 0},
};
const int perPageChoices[] = {1, 2, 4, 6, 9};
const int dpiChoices[] = {150, 300, 600};

int presetIndex(const PrintOptions &o) {
  for (int i = 0; i < int(std::size(presets)); ++i) {
    const auto &p = presets[i];
    if (p.size != o.size)
      continue;
    if (p.size != PrintOptions::Fixed ||
        (std::abs(p.widthMm - o.widthMm) < 0.5 &&
         std::abs(p.heightMm - o.heightMm) < 0.5))
      return i;
  }
  return 0;
}

struct Thumb {
  QSize size;
  double dpi = 0;
  QImage image;
  QString name;
};

GtkWidget *dropDown(const QStringList &items, int selected) {
  std::vector<QByteArray> storage;
  std::vector<const char *> strings;
  for (const auto &item : items)
    storage.push_back(Gx::u8(PrismLanguage::text(item)));
  for (auto &s : storage)
    strings.push_back(s.constData());
  strings.push_back(nullptr);
  auto *widget = gtk_drop_down_new_from_strings(strings.data());
  gtk_drop_down_set_selected(GTK_DROP_DOWN(widget), guint(std::max(0, selected)));
  return widget;
}
GtkWidget *spin(double low, double high, double step, double value, int digits) {
  auto *widget = gtk_spin_button_new_with_range(low, high, step);
  gtk_spin_button_set_digits(GTK_SPIN_BUTTON(widget), guint(digits));
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), value);
  return widget;
}
cairo_surface_t *surfaceOf(const QImage &image) {
  // QImage::Format_RGB32/ARGB32_Premultiplied share Cairo's pixel layout.
  QImage pixels = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pixels.width(),
                                             pixels.height());
  const int stride = cairo_image_surface_get_stride(surface);
  unsigned char *target = cairo_image_surface_get_data(surface);
  for (int y = 0; y < pixels.height(); ++y)
    std::memcpy(target + y * stride, pixels.constScanLine(y), size_t(pixels.width()) * 4);
  cairo_surface_mark_dirty(surface);
  return surface;
}
void paintPhoto(cairo_t *cr, const PrintPlacement &p, const QImage &image) {
  if (image.isNull() || p.clip.isEmpty())
    return;
  cairo_save(cr);
  cairo_rectangle(cr, p.clip.x(), p.clip.y(), p.clip.width(), p.clip.height());
  cairo_clip(cr);
  cairo_translate(cr, p.image.center().x(), p.image.center().y());
  double w = p.image.width(), h = p.image.height();
  if (p.rotate) {
    cairo_rotate(cr, std::numbers::pi / 2);
    std::swap(w, h);
  }
  cairo_scale(cr, w / image.width(), h / image.height());
  auto *surface = surfaceOf(image);
  cairo_set_source_surface(cr, surface, -image.width() / 2.0, -image.height() / 2.0);
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
  cairo_paint(cr);
  cairo_surface_destroy(surface);
  cairo_restore(cr);
}
// Draws the requested pages onto a PDF/PostScript surface (any thread: one
// surface, one thread). Returns an error text, empty on success.
QString renderPages(cairo_surface_t *surface, const QList<int> &pageList, QSizeF paper,
                    QRectF printable, const PrintOptions &o, const QStringList &photos,
                    const std::shared_ptr<std::atomic_bool> &token) {
  cairo_t *cr = cairo_create(surface);
  const bool ps = cairo_surface_get_type(surface) == CAIRO_SURFACE_TYPE_PS;
  const auto cells = PrintLayout::cells(printable.size(), o.perPage,
                                        o.marginMm * PrintLayout::pointsPerMm);
  QString error;
  for (const int page : pageList) {
    if (token->load()) {
      error = "Anulowano";
      break;
    }
    if (ps)
      cairo_ps_surface_set_size(surface, paper.width(), paper.height());
    else
      cairo_pdf_surface_set_size(surface, paper.width(), paper.height());
    cairo_save(cr);
    cairo_translate(cr, printable.left(), printable.top());
    QMap<int, QPair<QImage, QSizeF>> decoded; // one repeated photo: decode once
    for (int slot = 0; slot < cells.size(); ++slot) {
      const int index = PrintLayout::photoAt(int(photos.size()), o.perPage, page, slot);
      if (index < 0 || token->load())
        continue;
      const QString name = QFileInfo(photos[index]).fileName();
      PrintPlacement placement;
      QImage pixels;
      QString failure;
      try {
        Document doc;
        doc.open(photos[index], {}, token, {}, 256);
        placement = PrintLayout::place(cells[slot], doc.size(), doc.fileDpi(), o, captionPoints);
        if (decoded.contains(index) && decoded[index].second == placement.image.size())
          pixels = decoded[index].first;
        else {
          pixels = doc.renderForPrint(PrintLayout::renderPixels(placement, doc.size(), o.dpi),
                                      o.grayscale);
          decoded[index] = {pixels, placement.image.size()};
        }
      } catch (const std::exception &e) {
        failure = QString::fromUtf8(e.what());
      }
      PangoLayout *layout = pango_cairo_create_layout(cr);
      PangoFontDescription *font = pango_font_description_from_string(failure.isEmpty() ? "Sans 8" : "Sans 9");
      pango_layout_set_font_description(layout, font);
      pango_font_description_free(font);
      if (!failure.isEmpty()) {
        const auto text = Gx::u8(PrismLanguage::text("Nie można odczytać: ") + name);
        pango_layout_set_text(layout, text.constData(), -1);
        pango_layout_set_width(layout, int(cells[slot].width() * PANGO_SCALE));
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_move_to(cr, cells[slot].x(), cells[slot].y());
        pango_cairo_show_layout(cr, layout);
      } else {
        paintPhoto(cr, placement, pixels);
        if (!placement.caption.isEmpty()) {
          const auto text = Gx::u8(name);
          pango_layout_set_text(layout, text.constData(), -1);
          pango_layout_set_width(layout, int(placement.caption.width() * PANGO_SCALE));
          pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);
          pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
          cairo_set_source_rgb(cr, 0, 0, 0);
          cairo_move_to(cr, placement.caption.x(), placement.caption.y() + 1);
          pango_cairo_show_layout(cr, layout);
        }
      }
      g_object_unref(layout);
    }
    cairo_restore(cr);
    cairo_show_page(cr);
  }
  if (error.isEmpty() && cairo_status(cr) != CAIRO_STATUS_SUCCESS)
    error = QString::fromUtf8(cairo_status_to_string(cairo_status(cr)));
  cairo_destroy(cr);
  cairo_surface_finish(surface);
  if (error.isEmpty() && cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
    error = QString::fromUtf8(cairo_status_to_string(cairo_surface_status(surface)));
  return error;
}
} // namespace

QString PhotoPrint::settingsFile() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
         "/ky-prism/print.ini";
}

struct PhotoPrint::Impl : QObject {
  PhotoPrint *q = nullptr;
  GtkWindow *parent = nullptr;
  QStringList photos;
  GtkPrintSettings *settings = nullptr;
  GtkPageSetup *pageSetup = nullptr;
  PrintOptions options;
  QThreadPool pool;
  std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
  std::shared_ptr<bool> alive = std::make_shared<bool>(true);
  bool active = false;
  int pages = 0;
  // "Zdjęcie" tab
  GtkWidget *perPage = nullptr, *sizeList = nullptr, *customWidth = nullptr,
            *customHeight = nullptr, *crop = nullptr, *scale = nullptr,
            *margin = nullptr, *colour = nullptr, *quality = nullptr,
            *caption = nullptr, *rotate = nullptr, *preview = nullptr,
            *summary = nullptr;
  QList<Thumb> thumbs;
  QSizeF paper{595.28, 841.89};
  QRectF printable{QPointF(18, 18), QSizeF(559.28, 805.89)};

  void loadSettings() {
    g_autoptr(GKeyFile) file = g_key_file_new();
    const auto name = Gx::u8(settingsFile());
    if (QFileInfo(settingsFile()).isFile() && !QFileInfo(settingsFile()).isSymLink() &&
        g_key_file_load_from_file(file, name.constData(), G_KEY_FILE_NONE, nullptr)) {
      settings = gtk_print_settings_new_from_key_file(file, nullptr, nullptr);
      pageSetup = gtk_page_setup_new_from_key_file(file, nullptr, nullptr);
    }
    if (!settings)
      settings = gtk_print_settings_new();
    QList<QPair<QString, QString>> values;
    gtk_print_settings_foreach(
        settings,
        +[](const char *key, const char *value, gpointer data) {
          const QString k = QString::fromUtf8(key);
          if (k.startsWith("ky-prism-"))
            static_cast<QList<QPair<QString, QString>> *>(data)->append(
                {k.mid(9), QString::fromUtf8(value)});
        },
        &values);
    options = PrintOptions::fromValues(values);
  }
  void storeOptions() {
    for (const auto &[key, value] : options.values())
      gtk_print_settings_set(settings, Gx::u8("ky-prism-" + key).constData(),
                             Gx::u8(value).constData());
    gtk_print_settings_set_use_color(settings, !options.grayscale);
    gtk_print_settings_set_resolution(settings, options.dpi);
    gtk_print_settings_set_quality(settings, options.dpi >= 600   ? GTK_PRINT_QUALITY_HIGH
                                             : options.dpi <= 150 ? GTK_PRINT_QUALITY_DRAFT
                                                                  : GTK_PRINT_QUALITY_NORMAL);
  }
  void saveSettings() {
    if (!settings)
      return;
    const QFileInfo info(settingsFile());
    if (info.isSymLink() || !QDir().mkpath(info.absolutePath()))
      return;
    g_autoptr(GKeyFile) file = g_key_file_new();
    gtk_print_settings_to_key_file(settings, file, nullptr);
    if (pageSetup)
      gtk_page_setup_to_key_file(pageSetup, file, nullptr);
    g_key_file_save_to_file(file, Gx::u8(settingsFile()).constData(), nullptr);
  }

  // ------------------------------------------------------ "Zdjęcie" tab ---
  void readTab() {
    if (!perPage)
      return;
    options.perPage = perPageChoices[gtk_drop_down_get_selected(GTK_DROP_DOWN(perPage))];
    const auto &preset = presets[gtk_drop_down_get_selected(GTK_DROP_DOWN(sizeList))];
    options.size = preset.size;
    if (preset.size == PrintOptions::Fixed) {
      options.widthMm = preset.widthMm;
      options.heightMm = preset.heightMm;
    } else if (preset.size == PrintOptions::Custom) {
      options.widthMm = gtk_spin_button_get_value(GTK_SPIN_BUTTON(customWidth)) * 10;
      options.heightMm = gtk_spin_button_get_value(GTK_SPIN_BUTTON(customHeight)) * 10;
    }
    options.cropToFormat = gtk_check_button_get_active(GTK_CHECK_BUTTON(crop));
    options.scalePercent = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(scale));
    options.marginMm = gtk_spin_button_get_value(GTK_SPIN_BUTTON(margin));
    options.grayscale = gtk_drop_down_get_selected(GTK_DROP_DOWN(colour)) == 1;
    options.dpi = dpiChoices[gtk_drop_down_get_selected(GTK_DROP_DOWN(quality))];
    options.caption = gtk_check_button_get_active(GTK_CHECK_BUTTON(caption));
    options.autoRotate = gtk_check_button_get_active(GTK_CHECK_BUTTON(rotate));
  }
  void syncSensitivity() {
    const auto size = presets[gtk_drop_down_get_selected(GTK_DROP_DOWN(sizeList))].size;
    const bool custom = size == PrintOptions::Custom;
    gtk_widget_set_sensitive(customWidth, custom);
    gtk_widget_set_sensitive(customHeight, custom);
    gtk_widget_set_sensitive(crop, size == PrintOptions::Fixed || custom);
  }
  void changed() {
    readTab();
    syncSensitivity();
    const int sheets = PrintLayout::pages(int(photos.size()), options.perPage);
    const int count = int(photos.size());
    Ui::literal(summary,
                count == 1 && options.perPage > 1
                    ? PrismLanguage::text(QString("1 zdjęcie powtórzone %1 razy na kartce").arg(options.perPage))
                    : PrismLanguage::text(counted(count, "%1 zdjęcie", "%1 zdjęcia", "%1 zdjęć")) + " • " +
                          PrismLanguage::text(counted(sheets, "%1 strona", "%1 strony", "%1 stron")));
    gtk_widget_queue_draw(preview);
  }
  GtkWidget *row(GtkWidget *grid, int line, const QString &text, GtkWidget *control) {
    auto *label = Ui::label(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_grid_attach(GTK_GRID(grid), label, 0, line, 1, 1);
    gtk_widget_set_hexpand(control, TRUE);
    gtk_grid_attach(GTK_GRID(grid), control, 1, line, 1, 1);
    return control;
  }
  GtkWidget *buildTab() {
    auto *box = Gx::hbox(18);
    Gx::margins(box, 18, 18, 18, 18);
    auto *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    int line = 0;
    int perIndex = 0;
    for (int i = 0; i < int(std::size(perPageChoices)); ++i)
      if (perPageChoices[i] == options.perPage)
        perIndex = i;
    perPage = row(grid, line++, "Zdjęć na kartce",
                  dropDown({"1", "2", "4", "6", "9"}, perIndex));
    QStringList sizes;
    for (const auto &p : presets)
      sizes << QString::fromUtf8(p.label);
    sizeList = row(grid, line++, "Rozmiar zdjęcia", dropDown(sizes, presetIndex(options)));
    auto *custom = Gx::hbox(6);
    const bool customSize = options.size == PrintOptions::Custom;
    customWidth = spin(1, 200, 0.5, customSize ? options.widthMm / 10 : 10, 1);
    customHeight = spin(1, 200, 0.5, customSize ? options.heightMm / 10 : 15, 1);
    gtk_box_append(GTK_BOX(custom), customWidth);
    gtk_box_append(GTK_BOX(custom), Ui::label("×"));
    gtk_box_append(GTK_BOX(custom), customHeight);
    gtk_box_append(GTK_BOX(custom), Ui::label("cm"));
    row(grid, line++, "Własny rozmiar", custom);
    crop = gtk_check_button_new();
    gtk_check_button_set_child(GTK_CHECK_BUTTON(crop), Ui::label("Przytnij do formatu (bez białych pasków)"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(crop), options.cropToFormat);
    gtk_grid_attach(GTK_GRID(grid), crop, 1, line++, 1, 1);
    scale = row(grid, line++, "Skala (%)", spin(10, 400, 5, options.scalePercent, 0));
    margin = row(grid, line++, "Margines i odstęp (mm)", spin(0, 50, 1, options.marginMm, 0));
    colour = row(grid, line++, "Kolor", dropDown({"Kolor", "Odcienie szarości"}, options.grayscale ? 1 : 0));
    int dpiIndex = 1;
    for (int i = 0; i < int(std::size(dpiChoices)); ++i)
      if (dpiChoices[i] == options.dpi)
        dpiIndex = i;
    quality = row(grid, line++, "Jakość obrazu",
                  dropDown({"Szkic (150 dpi)", "Normalna (300 dpi)", "Wysoka (600 dpi)"}, dpiIndex));
    caption = gtk_check_button_new();
    gtk_check_button_set_child(GTK_CHECK_BUTTON(caption), Ui::label("Nazwa pliku pod zdjęciem"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(caption), options.caption);
    gtk_grid_attach(GTK_GRID(grid), caption, 1, line++, 1, 1);
    rotate = gtk_check_button_new();
    gtk_check_button_set_child(GTK_CHECK_BUTTON(rotate), Ui::label("Obracaj do orientacji kartki"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(rotate), options.autoRotate);
    gtk_grid_attach(GTK_GRID(grid), rotate, 1, line++, 1, 1);
    auto *hint = Ui::label("Drukarkę, papier, orientację, liczbę kopii i opcje sterownika "
                           "wybierasz na pozostałych kartach.");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(hint), 44);
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_grid_attach(GTK_GRID(grid), hint, 0, line++, 2, 1);
    gtk_box_append(GTK_BOX(box), grid);

    auto *side = Gx::vbox(8);
    preview = gtk_drawing_area_new();
    gtk_widget_set_size_request(preview, 260, 340);
    gtk_widget_set_vexpand(preview, TRUE);
    gtk_drawing_area_set_draw_func(
        GTK_DRAWING_AREA(preview),
        +[](GtkDrawingArea *, cairo_t *cr, int w, int h, gpointer data) {
          static_cast<Impl *>(data)->paintPreview(cr, w, h);
        },
        this, nullptr);
    Ui::accessible(preview, "Podgląd pierwszej kartki");
    gtk_box_append(GTK_BOX(side), preview);
    summary = Ui::label("");
    gtk_box_append(GTK_BOX(side), summary);
    gtk_box_append(GTK_BOX(box), side);

    auto onChange = [this] { changed(); };
    for (auto *w : {perPage, sizeList, colour, quality})
      Gx::on(w, "notify::selected", onChange);
    for (auto *w : {customWidth, customHeight, scale, margin})
      Gx::on(w, "value-changed", onChange);
    for (auto *w : {crop, caption, rotate})
      Gx::on(w, "toggled", onChange);
    changed();
    loadThumbs();
    return box;
  }
  void loadThumbs() {
    // Previews for the first sheet, decoded off the main thread.
    QStringList first = photos.mid(0, 9);
    auto token = cancelled;
    auto future = QtConcurrent::run(&pool, [first, token] {
      QList<Thumb> result;
      for (const auto &path : first) {
        if (token->load())
          break;
        Thumb t;
        t.name = QFileInfo(path).fileName();
        try {
          Document doc;
          doc.open(path, {}, token, {}, 256);
          t.size = doc.size();
          t.dpi = doc.fileDpi();
          t.image = doc.renderForPrint(QSize(512, 512), false);
        } catch (const std::exception &) {
        }
        result.append(t);
      }
      return result;
    });
    auto *watcher = new QFutureWatcher<QList<Thumb>>(this);
    QObject::connect(watcher, &QFutureWatcher<QList<Thumb>>::finished, this,
                     [this, watcher, guard = alive] {
                       watcher->deleteLater();
                       if (!*guard)
                         return;
                       thumbs = watcher->result();
                       if (preview)
                         gtk_widget_queue_draw(preview);
                     });
    watcher->setFuture(future);
  }
  void paintPreview(cairo_t *cr, int w, int h) {
    const double s = std::min((w - 16) / paper.width(), (h - 16) / paper.height());
    if (s <= 0)
      return;
    cairo_translate(cr, (w - paper.width() * s) / 2, (h - paper.height() * s) / 2);
    cairo_scale(cr, s, s);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.25);
    cairo_rectangle(cr, 3 / s, 3 / s, paper.width(), paper.height());
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_rectangle(cr, 0, 0, paper.width(), paper.height());
    cairo_fill(cr);
    cairo_translate(cr, printable.left(), printable.top());
    const double dash[] = {3 / s, 3 / s};
    cairo_set_dash(cr, dash, 2, 0);
    cairo_set_line_width(cr, 1 / s);
    cairo_set_source_rgba(cr, 0.4, 0.4, 0.4, 0.6);
    cairo_rectangle(cr, 0, 0, printable.width(), printable.height());
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);
    const double gap = options.marginMm * PrintLayout::pointsPerMm;
    const auto cells = PrintLayout::cells(printable.size(), options.perPage, gap);
    for (int slot = 0; slot < cells.size(); ++slot) {
      const int index = PrintLayout::photoAt(int(photos.size()), options.perPage, 0, slot);
      if (index < 0)
        continue;
      const Thumb *thumb = index < thumbs.size() ? &thumbs[index] : nullptr;
      if (!thumb || thumb->size.isEmpty()) {
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.25);
        cairo_rectangle(cr, cells[slot].x(), cells[slot].y(), cells[slot].width(), cells[slot].height());
        cairo_fill(cr);
        continue;
      }
      const auto p = PrintLayout::place(cells[slot], thumb->size, thumb->dpi, options, captionPoints);
      QImage image = thumb->image;
      if (options.grayscale)
        image = image.convertToFormat(QImage::Format_Grayscale8);
      paintPhoto(cr, p, image);
      if (!p.caption.isEmpty()) {
        cairo_set_source_rgba(cr, 0.3, 0.3, 0.3, 1);
        cairo_rectangle(cr, p.caption.center().x() - p.caption.width() * 0.3,
                        p.caption.center().y() - 1.5, p.caption.width() * 0.6, 3);
        cairo_fill(cr);
      }
    }
  }
  void pageChanged(GtkPageSetup *setup) {
    if (!setup)
      return;
    paper = QSizeF(gtk_page_setup_get_paper_width(setup, GTK_UNIT_POINTS),
                   gtk_page_setup_get_paper_height(setup, GTK_UNIT_POINTS));
    printable = QRectF(gtk_page_setup_get_left_margin(setup, GTK_UNIT_POINTS),
                       gtk_page_setup_get_top_margin(setup, GTK_UNIT_POINTS),
                       gtk_page_setup_get_page_width(setup, GTK_UNIT_POINTS),
                       gtk_page_setup_get_page_height(setup, GTK_UNIT_POINTS));
    if (preview)
      gtk_widget_queue_draw(preview);
  }

  // --------------------------------------------------------- printing ---
  GtkWidget *dialog = nullptr;
  GtkPrintJob *job = nullptr;
  guint pagePoll = 0;
  QString outcome;

  QList<int> selectedPages(GtkPrintJob *printJob) const {
    QList<int> list;
    const int total = std::max(1, pages);
    if (printJob && gtk_print_job_get_pages(printJob) == GTK_PRINT_PAGES_RANGES) {
      int count = 0;
      GtkPageRange *ranges = gtk_print_job_get_page_ranges(printJob, &count);
      for (int i = 0; i < count; ++i)
        for (int p = std::max(0, ranges[i].start); p <= std::min(total - 1, ranges[i].end); ++p)
          if (!list.contains(p))
            list.append(p);
    } else if (printJob && gtk_print_job_get_pages(printJob) == GTK_PRINT_PAGES_CURRENT) {
      list.append(0);
    } else {
      for (int p = 0; p < total; ++p)
        list.append(p);
    }
    return list;
  }
  static QSizeF paperOf(GtkPageSetup *setup) {
    return {gtk_page_setup_get_paper_width(setup, GTK_UNIT_POINTS),
            gtk_page_setup_get_paper_height(setup, GTK_UNIT_POINTS)};
  }
  static QRectF printableOf(GtkPageSetup *setup) {
    return {gtk_page_setup_get_left_margin(setup, GTK_UNIT_POINTS),
            gtk_page_setup_get_top_margin(setup, GTK_UNIT_POINTS),
            gtk_page_setup_get_page_width(setup, GTK_UNIT_POINTS),
            gtk_page_setup_get_page_height(setup, GTK_UNIT_POINTS)};
  }
  // Renders off the main thread; `then` runs on the main thread with the error.
  void render(cairo_surface_t *surface, const QList<int> &list, GtkPageSetup *setup,
              std::function<void(QString)> then) {
    const QSizeF paperSize = paperOf(setup);
    const QRectF area = printableOf(setup);
    const auto o = options;
    const auto list2 = photos;
    auto token = cancelled;
    auto future = QtConcurrent::run(&pool, [surface, list, paperSize, area, o, list2, token] {
      return renderPages(surface, list, paperSize, area, o, list2, token);
    });
    auto *watcher = new QFutureWatcher<QString>(this);
    QObject::connect(watcher, &QFutureWatcher<QString>::finished, this,
                     [watcher, then, guard = alive] {
                       watcher->deleteLater();
                       if (*guard)
                         then(watcher->result());
                     });
    watcher->setFuture(future);
  }
  void finish(const QString &message) {
    active = false;
    outcome = message;
    if (q->status)
      q->status(message);
    Gx::later([self = this, guard = alive] {
      if (*guard && self->q->finished)
        self->q->finished();
    });
  }
  void closeDialog() {
    if (pagePoll) {
      g_source_remove(pagePoll);
      pagePoll = 0;
    }
    if (dialog) {
      gtk_window_destroy(GTK_WINDOW(dialog));
      dialog = nullptr;
    }
    perPage = sizeList = customWidth = customHeight = crop = scale = margin = colour =
        quality = caption = rotate = preview = summary = nullptr;
  }
  void response(int answer) {
    if (answer != GTK_RESPONSE_OK) {
      closeDialog();
      finish("Drukowanie anulowane.");
      return;
    }
    auto *printDialog = GTK_PRINT_UNIX_DIALOG(dialog);
    readTab();
    GtkPrinter *printer = gtk_print_unix_dialog_get_selected_printer(printDialog);
    if (!printer) {
      closeDialog();
      finish("Nie wybrano drukarki.");
      return;
    }
    g_object_ref(printer);
    GtkPrintSettings *chosen = gtk_print_unix_dialog_get_settings(printDialog); // new reference
    GtkPageSetup *setup = gtk_print_unix_dialog_get_page_setup(printDialog);
    g_object_ref(setup);
    g_clear_object(&settings);
    settings = chosen;
    g_clear_object(&pageSetup);
    pageSetup = setup;
    storeOptions();
    saveSettings();
    closeDialog();
    pages = PrintLayout::pages(int(photos.size()), options.perPage);
    const QString title = photos.size() == 1 ? QFileInfo(photos.first()).fileName()
                                             : QString("ky-prism • %1 zdjęć").arg(photos.size());
    job = gtk_print_job_new(Gx::u8(title).constData(), printer, settings, pageSetup);
    g_object_unref(printer);
    g_autoptr(GError) error = nullptr;
    cairo_surface_t *surface = gtk_print_job_get_surface(job, &error);
    if (!surface) {
      finish(QString("Błąd drukowania: %1").arg(error ? QString::fromUtf8(error->message) : QString()));
      return;
    }
    const auto list = selectedPages(job);
    sent = int(list.size());
    render(surface, list, pageSetup, [this](QString failure) {
      if (!failure.isEmpty()) {
        finish(QString("Błąd drukowania: %1").arg(failure));
        return;
      }
      gtk_print_job_send(
          job,
          +[](GtkPrintJob *, gpointer data, const GError *error) {
            auto *self = static_cast<Impl *>(data);
            self->finish(error ? QString("Błąd drukowania: %1").arg(QString::fromUtf8(error->message))
                               : sentMessage(self->sent));
          },
          this, nullptr);
    });
  }
  int sent = 0;
};

PhotoPrint::PhotoPrint(GtkWindow *parent, const QStringList &photos) : d(new Impl) {
  d->q = this;
  d->parent = parent;
  d->photos = photos;
  d->pool.setMaxThreadCount(1);
  d->loadSettings();
}
PhotoPrint::~PhotoPrint() {
  *d->alive = false;
  d->cancelled->store(true);
  d->pool.waitForDone();
  d->closeDialog();
  g_clear_object(&d->job);
  g_clear_object(&d->settings);
  g_clear_object(&d->pageSetup);
}
void PhotoPrint::run() {
  if (d->active || d->photos.isEmpty())
    return;
  d->active = true;
  d->pages = PrintLayout::pages(int(d->photos.size()), d->options.perPage);
  // The in-process GTK dialog: the print portal cannot show the "Zdjęcie" tab.
  auto *dialog = gtk_print_unix_dialog_new(Gx::u8(PrismLanguage::text("Drukuj zdjęcia")).constData(),
                                           d->parent);
  d->dialog = dialog;
  auto *printDialog = GTK_PRINT_UNIX_DIALOG(dialog);
  gtk_print_unix_dialog_set_settings(printDialog, d->settings);
  if (d->pageSetup)
    gtk_print_unix_dialog_set_page_setup(printDialog, d->pageSetup);
  gtk_print_unix_dialog_set_embed_page_setup(printDialog, TRUE);
  // Copies, collation, order and n-up stay with the printer (CUPS); the
  // program offers PDF/PostScript so "Print to File" works too.
  gtk_print_unix_dialog_set_manual_capabilities(
      printDialog, GtkPrintCapabilities(GTK_PRINT_CAPABILITY_GENERATE_PDF | GTK_PRINT_CAPABILITY_GENERATE_PS));
  gtk_print_unix_dialog_add_custom_tab(printDialog, d->buildTab(),
                                       gtk_label_new(Gx::u8(PrismLanguage::text("Zdjęcie")).constData()));
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  Gx::connect(dialog, "response",
              +[](GtkDialog *, int answer, gpointer data) {
                static_cast<Impl *>(data)->response(answer);
              },
              d.get());
  // The tab's preview follows the paper chosen on the other tabs.
  d->pagePoll = g_timeout_add(
      300,
      +[](gpointer data) -> gboolean {
        auto *self = static_cast<Impl *>(data);
        if (self->dialog)
          self->pageChanged(gtk_print_unix_dialog_get_page_setup(GTK_PRINT_UNIX_DIALOG(self->dialog)));
        return G_SOURCE_CONTINUE;
      },
      d.get());
  d->pageChanged(gtk_print_unix_dialog_get_page_setup(printDialog));
  gtk_window_present(GTK_WINDOW(dialog));
}
void PhotoPrint::exportPdf(const QString &file, const PrintOptions &options) {
  if (d->active || d->photos.isEmpty())
    return;
  d->active = true;
  d->options = options;
  d->pages = PrintLayout::pages(int(d->photos.size()), options.perPage);
  // Deterministic sheet for automation: A4 portrait with GTK's default margins.
  g_autoptr(GtkPageSetup) setup = gtk_page_setup_new();
  GtkPaperSize *a4 = gtk_paper_size_new(GTK_PAPER_NAME_A4);
  gtk_page_setup_set_paper_size_and_default_margins(setup, a4);
  gtk_paper_size_free(a4);
  const QSizeF paper = Impl::paperOf(setup);
  cairo_surface_t *surface = cairo_pdf_surface_create(Gx::u8(file).constData(), paper.width(), paper.height());
  QList<int> list;
  for (int p = 0; p < d->pages; ++p)
    list.append(p);
  auto *self = d.get();
  self->render(surface, list, setup, [self, surface](QString failure) {
    cairo_surface_destroy(surface);
    self->finish(failure.isEmpty() ? pdfMessage(self->pages)
                                   : QString("Błąd drukowania: %1").arg(failure));
  });
}
void PhotoPrint::cancel() {
  d->cancelled->store(true);
  if (d->dialog) {
    d->closeDialog();
    d->finish("Drukowanie anulowane.");
  }
}
bool PhotoPrint::running() const { return d->active; }
int PhotoPrint::pageCount() const { return d->pages; }
