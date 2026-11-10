#pragma once
#include "localized_widgets.h"
#include "favorites.h"
#include "file_ops_core.h"
#include "glass_widgets.h"
#include <QDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QThreadPool>
#include <QTreeView>
#include <functional>
class DuplicateModel;
class FileDialog final : public GlassDialog {
  Q_OBJECT
  QThreadPool pool;
  QFutureWatcher<FileResult> watcher;
  std::shared_ptr<std::atomic_bool> token =
      std::make_shared<std::atomic_bool>(false);
  QTreeView *tree;
  DuplicateModel *model;
  UiLabel *message, *destinationLabel;
  QProgressBar *progress;
  UiPushButton *action, *cancelButton, *choose;
  QString source, destination, root;
  QStringList sources;
  UiFileListEdit *copyList = nullptr;
  QList<DuplicateGroup> groups;
  bool searching = false, active = false, shuttingDown = false;
  void start(std::function<FileResult(const std::shared_ptr<std::atomic_bool> &,
                                      const Progress &)>);
  void chooseDestination();
  void search();
  void removeSelected();
  void updateSelection();

public:
  FileDialog(const QString &photo, bool duplicates, const QString &directory,
             QWidget *parent = nullptr);
  ~FileDialog() override;
  void cancelForShutdown();
  void setSources(const QStringList &);

protected:
  void reject() override;
  void showEvent(QShowEvent *) override;
};
