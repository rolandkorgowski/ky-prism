#pragma once
#include "gtkbase.h"
#include <QStringList>
#include <functional>
#include <memory>

class PhotoView;
class LibraryPane;

// Main ky-prism window (GTK 4). Behaviour mirrors the reviewed Qt window:
// tabs, comparison, library, favourites, bookmarks, file operations, EXIF,
// keyboard map and language switch — plus the adjustable chrome opacity.
class PrismWindow {
  struct Impl;
  std::unique_ptr<Impl> d;

public:
  explicit PrismWindow(const QString &displayProfile = {});
  ~PrismWindow();
  PrismWindow(const PrismWindow &) = delete;
  GtkWindow *window() const;
  void show(bool fullscreen = false);
  // Open a photograph, or browse when `path` is a directory.
  void open(const QString &path);
  void browse(const QString &directory);
  // Invoked once after the user closed the window and work was cancelled.
  std::function<void()> closed;
  void close();

  // Named interface actions: the single entry point used by buttons,
  // shortcuts and tests ("open", "toggleLibrary", "newTab", "compare", …).
  bool trigger(const QString &action);
  QStringList actions() const;

  // State inspection (tests, diagnostics).
  PhotoView *primaryView() const;
  PhotoView *secondaryView() const;
  LibraryPane *library() const;
  QString statusText() const;
  QString titleText() const;
  QString currentPhoto() const;
  QString comparedPhoto() const;
  QString currentDirectory() const;
  QString metadataText() const;
  QString zoomText() const;
  int tabCount() const;
  int activeTab() const;
  bool libraryVisible() const;
  bool exifVisible() const;
  bool compareVisible() const;
  bool welcomeVisible() const;
  bool heartSelected() const;
  bool bookmarkChecked() const;
  bool shuttingDown() const;
  void showComparison(const QString &path);
  void setSort(int column, bool descending);
  void setLibraryMode(bool thumbnails);
  void selectTab(int index);
  // Tests: export the displayed photo without the system chooser; worker
  // state of the export and the clipboard copy; right-click menu at a point.
  void exportTo(const QString &target);
  bool exporting() const;
  bool copyingToClipboard() const;
  void showPhotoMenu(double x, double y);
};
