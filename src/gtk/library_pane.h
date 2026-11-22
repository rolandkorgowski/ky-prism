#pragma once
#include "gtkbase.h"
#include <QStringList>
#include <functional>
#include <memory>

class ThumbnailCache;

// Directory browser: asynchronous listing, natural sorting, a detail list and
// a GPU-composited thumbnail grid sharing one multi-selection. Listing and
// thumbnail decoding never run on the interface thread.
class LibraryPane {
  struct Impl;
  std::unique_ptr<Impl> d;

public:
  enum Mode { List, Thumbnails };
  // photosOnly: restrict files to the supported photograph extensions.
  explicit LibraryPane(ThumbnailCache *thumbnails, bool photosOnly = true);
  ~LibraryPane();
  LibraryPane(const LibraryPane &) = delete;

  GtkWidget *widget() const;     // stack holding both views
  GtkWidget *focusWidget() const; // the visible view
  void setMode(Mode mode);
  Mode mode() const;
  void setDirectory(const QString &absolutePath);
  QString directory() const;
  void setSort(int column, bool descending);
  void setPhotosOnly(bool enabled);
  void refresh();
  QStringList selectedFiles() const;
  int selectedFolderCount() const;
  // Single selected entry (file or directory); empty otherwise.
  QString selectedPath(bool *isDirectory = nullptr) const;
  void select(const QString &path);
  // Hands the keyboard to the visible view: to the first selected entry,
  // else to the view's own cursor, else to the first entry.
  void focusEntries();
  void stop();
  void refreshThumbnails();
  // Test / diagnostics access.
  int count() const;
  QStringList names() const;
  bool scanning() const;
  bool activateRow(int row);
  // Whether a point (in the visible view's coordinates) lies on an entry
  // rather than on the header, a scrollbar or empty space.
  bool entryAt(double x, double y) const;
  void selectRows(const QList<int> &rows);
  // Entry at a point of the visible view: its path, empty on the header, a
  // scrollbar or empty space.
  QString pathAt(double x, double y, bool *isDirectory = nullptr) const;
  // Centre of the row or thumbnail showing `path`, in the visible view's
  // coordinates; false when it is not on screen.
  bool pointOf(const QString &path, double *x, double *y) const;

  // Drag and drop. A drag started on a photo carries that photo, and the
  // rest of the selection when the photo is part of it; folders never drag.
  // Other programs receive a file list (text/uri-list), copy only.
  QStringList dragFiles(double x, double y) const;
  GdkContentProvider *dragContent(double x, double y) const; // new reference
  // True while `drag` is a drag that a library pane started.
  static bool ownsDrag(GdkDrag *drag);
  // Marks the folder at (x, y) as the drop target; anything else clears it.
  void showDropFolder(double x, double y);
  void clearDropFolder();

  // Plain primary click on a file (no Ctrl/Shift): open it.
  std::function<void(const QString &path)> fileClicked;
  // Double click / Enter.
  std::function<void(const QString &path, bool isDirectory)> activated;
  std::function<void()> selectionChanged;
  // Delete on the list or the thumbnails (the window asks before anything
  // moves to the trash).
  std::function<void()> deleteRequested;
  // Right click on a file: it is selected first unless it already is part of
  // the selection. (x, y) are in the visible view's coordinates.
  std::function<void(double x, double y)> menuRequested;
  std::function<void()> listed;
};
