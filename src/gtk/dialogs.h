#pragma once
#include "gtkbase.h"
#include <QStringList>
#include <functional>
#include <memory>

class Favorites;

namespace Dialogs {
// Frameless glass dialog window with a draggable header and a close button.
struct Shell {
  GtkWindow *window = nullptr;
  GtkWidget *root = nullptr, *header = nullptr, *body = nullptr,
            *heading = nullptr;
  // Invoked by the close button, Esc and Ctrl+W. Default: hide the window.
  std::shared_ptr<Gx::Callback> close = std::make_shared<Gx::Callback>();
};
Shell shell(GtkWindow *parent, const QString &title, const QString &eyebrow,
            const QString &heading, int width, int height, bool modal);
// Modal question. `done` receives the pressed button index, or `cancel` when
// the dialog is dismissed.
void message(GtkWindow *parent, const QString &title, const QString &text,
             const QString &detail, const QStringList &buttons,
             int defaultButton, int cancelButton,
             std::function<void(int)> done);
// Modal single choice; -1 when cancelled.
void chooseItem(GtkWindow *parent, const QString &title, const QString &label,
                const QStringList &items, std::function<void(int)> done);
// System (portal) choosers; empty string when cancelled.
void chooseFolder(GtkWindow *parent, const QString &title,
                  const QString &initial, std::function<void(QString)> done);
void chooseSaveFile(GtkWindow *parent, const QString &title,
                    const QString &folder, const QString &name,
                    std::function<void(QString)> done);
// Photo picker with thumbnails and a preview of the selected file.
void choosePhoto(GtkWindow *parent, const QString &title,
                 const QString &directory, std::function<void(QString)> done);
// Close every one-shot dialog (question, choice, photo picker) opened for
// `parent` without running its callback: used when the parent goes away.
void dismissAll(GtkWindow *parent);
// Test hook: number of dialog shells currently alive.
int openShells();
} // namespace Dialogs

class AboutDialog {
  struct Impl;
  std::unique_ptr<Impl> d;

public:
  explicit AboutDialog(GtkWindow *parent);
  ~AboutDialog();
  void showPage(int page);
  void hide();
  bool animating() const;
  int currentPage() const;
  bool visible() const;
};

class BookmarksDialog {
  struct Impl;
  std::unique_ptr<Impl> d;

public:
  BookmarksDialog(std::function<QString()> currentDirectory, GtkWindow *parent);
  ~BookmarksDialog();
  void present();
  void hide();
  void refresh();
  int rowCount() const;
  std::function<void(const QString &)> openRequested;
  std::function<void()> changed;
};

class FavoritesDialog {
  struct Impl;
  std::unique_ptr<Impl> d;

public:
  FavoritesDialog(Favorites *favorites, GtkWindow *parent);
  ~FavoritesDialog();
  void present();
  void hide();
  int rowCount() const;
  std::function<void(const QString &)> openRequested;
};

class FileOpsDialog {
  struct Impl;
  std::unique_ptr<Impl> d;

public:
  FileOpsDialog(const QString &photo, bool duplicates, const QString &directory,
                GtkWindow *parent);
  ~FileOpsDialog();
  void setSources(const QStringList &sources);
  void present();
  void hide();
  void cancelForShutdown();
  bool searching() const;
  bool active() const;
  // Duplicate search: a folder is selected and the start button is enabled.
  bool canStart() const;
  // Tests drive the worker without a system chooser.
  void selectFolderForTest(const QString &path);
  void startForTest();
  void setDestinationForTest(const QString &path);
  void runForTest();
  QString messageText() const;
  // The dialog finished and may be destroyed by its owner.
  std::function<void()> closed;
};

class UsbAccess {
  struct Impl;
  std::unique_ptr<Impl> d;

public:
  explicit UsbAccess(GtkWindow *parent);
  ~UsbAccess();
  void open();
  void shutdown();
  std::function<void(const QString &)> opened, failed;
};
