#include "dialog_theme.h"
#include "menu_glass.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QFileDialog>
#include <QFile>
#include <QDir>
#include <QFont>
#include <QHeaderView>
#include <QTreeView>
#include <QDirIterator>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest/QTest>
#include <iostream>
#include <stdexcept>
static void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  QTemporaryDir config;
  qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
  QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  QApplication app(argc, argv);
  app.setStyle(new MenuGlassStyle);
  QFont font; font.setFamilies({"Inter","Adwaita Sans","Noto Sans"});font.setPointSizeF(10.5);app.setFont(font);
  try {
    QTemporaryDir folder;
    QDir(folder.path()).mkdir(QString::fromUtf8("Zdjęcia — zażółć gęślą"));
    QFile file(folder.filePath(QString::fromUtf8("Łódź — długie zdjęcie.jpg")));
    check(file.open(QIODevice::WriteOnly), "fixture failed"); file.close();
    int themeCount=0;
    for (const auto &directory : {QString("/usr/share/omarchy/themes"), QDir::homePath()+"/.config/omarchy/themes"}) {
    QDirIterator themes(directory,{"colors.toml"},QDir::Files,QDirIterator::Subdirectories);
    while(themes.hasNext()) {
      QFile input(themes.next());
      if(!input.open(QIODevice::ReadOnly)) continue;
      const auto data=QString::fromUtf8(input.read(65536));
      auto color=[&](const QString &key) {
        return QColor(QRegularExpression("(?:^|\\n)\\s*"+key+"\\s*=\\s*[\"'](#[0-9a-fA-F]{6})[\"']").match(data).captured(1));
      };
      auto bg=color("background"),fg=color("foreground"),accent=color("accent");
      if(!bg.isValid()||!fg.isValid()||!accent.isValid()) continue;
      auto p=PrismDialogTheme::palette(app.palette(),bg,fg,accent);
      check(PrismDialogTheme::contrast(p.color(QPalette::Text),bg)>=4.5,"theme filenames contrast");
      check(PrismDialogTheme::contrast(p.color(QPalette::HighlightedText),accent)>=4.5,"theme selection contrast");
      app.setPalette(p);
      QWidget owner;
      owner.setStyleSheet("QWidget{color:#111111;} QTreeView,QListView{color:#111111;background:#fefefe;}");
      QFileDialog probe(&owner, QString::fromUtf8("Otwórz zdjęcie"), folder.path());
      probe.setOption(QFileDialog::DontUseNativeDialog);
      probe.setViewMode(QFileDialog::Detail);
      probe.resize(820,520); probe.show(); QTest::qWait(20);
      for(auto *view:probe.findChildren<QAbstractItemView *>()) {
        check(PrismDialogTheme::contrast(bg,view->palette().color(QPalette::Text))>=4.5,"installed theme: invisible file list");
        check(PrismDialogTheme::contrast(view->palette().color(QPalette::Highlight),view->palette().color(QPalette::HighlightedText))>=4.5,"installed theme: invisible selection");
        if (auto *tree=qobject_cast<QTreeView *>(view); tree && tree->isVisible())
          check(tree->header()->viewport()->height()>=tree->header()->fontMetrics().height(),"installed theme: clipped header labels");
      }
      const auto rendered=probe.grab().toImage();
      const auto body=rendered.pixelColor(probe.width()*3/4,probe.height()/2);
      check(body.alpha()>=220 && body.alpha()<255,"installed theme: alpha backing lost");
      check(std::abs(body.red()-bg.red())<5 && std::abs(body.green()-bg.green())<5,"installed theme: wrong backing color");
      probe.reject();
      ++themeCount;
    }
    }
    QWidget parent;
    for (bool dark : {true, false}) {
      QColor bg(dark ? "#15241d" : "#f2e6d5"), fg(dark ? "#e4f1e8" : "#352218"), accent(dark ? "#90d6ac" : "#99522f");
      const auto palette = PrismDialogTheme::palette(app.palette(), bg, fg, accent);
      app.setPalette(palette); parent.setPalette(palette);
      // A dialog-local style must win even when its owner applies a
      // conflicting stylesheet. Merely checking QPalette missed this bug.
      parent.setStyleSheet("QWidget{color:#111111;} QTreeView,QListView{color:#111111;background:#fefefe;}");
      QFileDialog dialog(&parent, QString::fromUtf8("Otwórz zdjęcie"), folder.path());
      dialog.setOption(QFileDialog::DontUseNativeDialog);
      dialog.setViewMode(QFileDialog::Detail);
      dialog.resize(820,520); dialog.show();
      QTest::qWait(100);
      check(dialog.testAttribute(Qt::WA_TranslucentBackground), "file dialog alpha missing");
      for (auto group : {QPalette::Active,QPalette::Inactive}) {
        const auto p = dialog.palette();
        check(PrismDialogTheme::contrast(p.color(group,QPalette::Text),p.color(group,QPalette::Base)) >= 4.5, "unreadable filenames");
        check(PrismDialogTheme::contrast(p.color(group,QPalette::HighlightedText),p.color(group,QPalette::Highlight)) >= 4.5, "unreadable selection");
      }
      for (auto view : dialog.findChildren<QAbstractItemView *>()) {
        const auto p=view->palette();
        check(PrismDialogTheme::contrast(p.color(QPalette::Text),bg) >= 4.5, "child view lost foreground");
      }
      int vectorIcons=0;
      for(auto button:dialog.findChildren<QToolButton *>()) {
        if(!button->property("prismVectorIcon").toBool()) continue;
        auto icon=button->icon().pixmap(24,24).toImage();int visible=0;
        for(int y=0;y<icon.height();++y) for(int x=0;x<icon.width();++x)
          if(icon.pixelColor(x,y).alpha()>0) ++visible;
        check(visible>10 && visible<icon.width()*icon.height()*.8,"navigation icon became an opaque block");
        ++vectorIcons;
      }
      check(vectorIcons==6,"file chooser navigation controls changed");
      const auto rendered = dialog.grab().toImage();
      const auto body = rendered.pixelColor(dialog.width()*3/4,dialog.height()/2);
      check(body.alpha() >= 220 && body.alpha() < 255, "dialog body is fully transparent or opaque");
      check(std::abs(body.red()-bg.red()) < 5 && std::abs(body.green()-bg.green()) < 5,
            "dialog backing lost the theme color");
      const auto snapshots=qEnvironmentVariable("PRISM_DIALOG_SNAPSHOTS");
      if (!snapshots.isEmpty()) dialog.grab().save(snapshots+(dark?"/file-dialog-dark.png":"/file-dialog-light.png"));
      dialog.reject();
    }
    std::cout << "PASS Qt file dialog: light/dark active/inactive palettes, readable rows and selection, alpha, Unicode paths; installed palettes checked=" << themeCount << '\n';
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
