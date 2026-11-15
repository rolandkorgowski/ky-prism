// Print layout geometry: cells, sizes, cropping, rotation, pagination.
#include "print_layout.h"
#include <cmath>
#include <cstdio>

namespace {
int failures = 0;
void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}
bool near(double a, double b, double tolerance = 0.01) {
  return std::abs(a - b) <= tolerance;
}
} // namespace

int main() {
  const QSizeF a4(595.28, 841.89); // printable area in points (portrait)
  const double mm = PrintLayout::pointsPerMm;
  PrintOptions o;
  o.autoRotate = false;

  // Grid follows the page orientation; cells never overlap the margins.
  auto one = PrintLayout::cells(a4, 1, 10);
  check(one.size() == 1 && near(one[0].left(), 10) && near(one[0].right(), a4.width() - 10),
        "single cell with margins");
  auto two = PrintLayout::cells(a4, 2, 0);
  check(two.size() == 2 && near(two[0].width(), a4.width()), "2 per portrait page: stacked");
  auto twoWide = PrintLayout::cells(QSizeF(841.89, 595.28), 2, 0);
  check(twoWide.size() == 2 && near(twoWide[0].height(), 595.28), "2 per landscape page: side by side");
  check(PrintLayout::cells(a4, 4, 5).size() == 4 && PrintLayout::cells(a4, 6, 5).size() == 6 &&
            PrintLayout::cells(a4, 9, 5).size() == 9, "4, 6, 9 cells");
  auto six = PrintLayout::cells(a4, 6, 0);
  check(near(six[0].width(), a4.width() / 2) && near(six[0].height(), a4.height() / 3), "6 per page: 2×3");

  const QRectF cell(0, 0, 500, 700);
  const QSize landscape(6000, 4000);
  // Fit: whole photo, aspect kept, centred.
  auto fit = PrintLayout::place(cell, landscape, 300, o, 0);
  check(near(fit.clip.width(), 500) && near(fit.clip.height(), 500.0 * 4000 / 6000) &&
            near(fit.clip.center().y(), 350) && fit.clip == fit.image, "fit");
  // Fill: covers the cell; the photo extends beyond and is clipped.
  o.size = PrintOptions::Fill;
  auto fill = PrintLayout::place(cell, landscape, 300, o, 0);
  check(fill.clip == cell && near(fill.image.height(), 700) && fill.image.width() > 700, "fill crops");
  // Actual size from the file's resolution: 6000 px at 300 dpi = 20 inches.
  o.size = PrintOptions::Actual;
  auto actual = PrintLayout::place(QRectF(0, 0, 2000, 2000), landscape, 300, o, 0);
  check(near(actual.image.width(), 20 * 72) && near(actual.image.height(), 4000.0 / 300 * 72), "actual size");
  auto guessed = PrintLayout::place(QRectF(0, 0, 2000, 2000), landscape, 72, o, 0);
  check(near(guessed.image.width(), 20 * 72), "implausible 72 dpi treated as 300");
  // 10×15 cm print, cropped to the format and turned to the photo's orientation.
  o.size = PrintOptions::Fixed;
  o.widthMm = 100;
  o.heightMm = 150;
  auto postcard = PrintLayout::place(cell, landscape, 300, o, 0);
  check(near(postcard.clip.width(), 150 * mm) && near(postcard.clip.height(), 100 * mm), "10×15 landscape");
  o.cropToFormat = false;
  auto inside = PrintLayout::place(cell, landscape, 300, o, 0);
  check(near(inside.clip.width(), 150 * mm) && near(inside.clip.height(), 100 * mm), "3:2 photo fits 15×10 exactly");
  // Scale applies on top of the size.
  o.size = PrintOptions::Fit;
  o.scalePercent = 50;
  auto half = PrintLayout::place(cell, landscape, 300, o, 0);
  check(near(half.clip.width(), 250), "scale 50 %");
  o.scalePercent = 100;
  // Auto-rotate turns a landscape photo in a portrait cell.
  o.autoRotate = true;
  auto turned = PrintLayout::place(cell, landscape, 300, o, 0);
  check(turned.rotate && near(turned.clip.height(), 700) && near(turned.clip.width(), 700.0 * 4000 / 6000),
        "auto-rotate");
  // Caption takes the bottom of the cell.
  o.caption = true;
  auto captioned = PrintLayout::place(cell, landscape, 300, o, 20);
  check(near(captioned.caption.height(), 20) && captioned.clip.bottom() <= captioned.caption.top() + 0.01,
        "caption below the photo");
  // Pagination: one photo repeats over a sheet, many fill cells in order.
  check(PrintLayout::pages(1, 4) == 1 && PrintLayout::photoAt(1, 4, 0, 3) == 0, "repeat one photo");
  check(PrintLayout::pages(5, 4) == 2 && PrintLayout::photoAt(5, 4, 1, 0) == 4 &&
            PrintLayout::photoAt(5, 4, 1, 1) == -1, "5 photos, 4 per page");
  check(PrintLayout::pages(3, 1) == 3, "one per page");
  // Rendering never exceeds the source and follows the dpi.
  o = PrintOptions();
  o.autoRotate = false;
  auto small = PrintLayout::place(QRectF(0, 0, 72 * 4, 72 * 4), landscape, 300, o, 0);
  check(PrintLayout::renderPixels(small, landscape, 300) == QSize(1200, 800), "4 inch at 300 dpi");
  check(PrintLayout::renderPixels(small, QSize(600, 400), 300) == QSize(600, 400), "no upscaling");
  // Options survive the round trip through print settings.
  o.perPage = 6;
  o.size = PrintOptions::Custom;
  o.widthMm = 90;
  o.grayscale = true;
  o.dpi = 600;
  auto back = PrintOptions::fromValues(o.values());
  check(back.perPage == 6 && back.size == PrintOptions::Custom && near(back.widthMm, 90) &&
            back.grayscale && back.dpi == 600, "options round trip");
  check(PrintOptions::fromValues({{"per-page", "7"}, {"dpi", "5000"}}).perPage == 1, "invalid values ignored");
  if (failures)
    return 1;
  std::puts("print layout: ok");
  return 0;
}
