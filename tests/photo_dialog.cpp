#include "photo_dialog.h"
#include "menu_glass.h"
#include "product_icon.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFileSystemModel>
#include <QListView>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest/QTest>
#include <vips/vips8>
#include <iostream>
#include <stdexcept>

static void check(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
template<class Predicate> void waitFor(Predicate done) {
  QElapsedTimer timer;timer.start();
  while(!done() && timer.elapsed()<5000) {QCoreApplication::processEvents();QTest::qWait(5);}
  check(done(),"chooser asynchronous timeout");
}
int main(int argc,char **argv) {
  QTemporaryDir config;
  qputenv("XDG_CONFIG_HOME",config.path().toUtf8());
  qputenv("PRISM_JPEG_BACKEND","cpu");
  QApplication app(argc,argv);app.setStyle(new MenuGlassStyle);
  auto theme=app.palette();
  theme.setColor(QPalette::Window,QColor("#181a1f"));
  theme.setColor(QPalette::WindowText,QColor("#b9bec6"));
  theme.setColor(QPalette::Highlight,QColor("#ad2222"));
  theme.setColor(QPalette::HighlightedText,Qt::white);
  app.setPalette(theme);
  if(VIPS_INIT(argv[0])) return 2;
  int result=0;
  try {
    QTemporaryDir dir;
    const auto photo=dir.filePath(QString::fromUtf8("Łódź [zdjęcie] — lato.jpg"));
    auto raster=vips::VImage::black(900,300,vips::VImage::option()->set("bands",3));
    raster=raster.new_from_image({30,140,230}).copy(vips::VImage::option()->set("interpretation",VIPS_INTERPRETATION_sRGB));
    raster.jpegsave(photo.toUtf8().constData());
    QFile broken(dir.filePath("broken.jpg"));check(broken.open(QIODevice::WriteOnly),"fixture");broken.write("not a jpeg");broken.close();
    QDir(dir.path()).mkdir(QString::fromUtf8("Katalog — zdjęcia"));
    PhotoDialogCache cache(nullptr);cache.setActive(true);
    QImage decoded;
    waitFor([&] {decoded=cache.request(QFileInfo(photo));return !decoded.isNull();});
    check(decoded.width()==512 && decoded.height()>=170 && decoded.height()<=171,"thumbnail aspect/limit");
    check(decoded.pixelColor(200,80).blue()>200,"thumbnail pixels missing");
    check(cache.cachedKiB()<=48*1024,"unbounded chooser cache");
    cache.setActive(false);
    waitFor([&] {return !cache.busy();});

    for(int repetition=0;repetition<12;++repetition) {
      if(repetition==6) {
        theme.setColor(QPalette::Window,QColor("#f2e6d5"));
        theme.setColor(QPalette::WindowText,QColor("#352218"));
        theme.setColor(QPalette::Highlight,QColor("#99522f"));app.setPalette(theme);
      }
      PhotoFileDialog dialog(nullptr,dir.path(),"Zdjęcia (*.jpg)");
      dialog.show();QTest::qWait(25);
      auto *list=dialog.findChild<QListView *>("listView");
      auto *preview=dialog.findChild<QWidget *>("photoChooserPreview");
      check(list && preview && preview->isVisible(),"chooser grid or preview missing");
      check(list->viewMode()==QListView::IconMode,"chooser not in thumbnails mode");
      dialog.findChild<QToolButton *>("detailModeButton")->click();
      dialog.findChild<QToolButton *>("listModeButton")->click();
      check(list->viewMode()==QListView::IconMode,"Qt view switch lost thumbnails");
      auto select=[&](const QString &path) {
        waitFor([&] {
          for(int r=0;r<list->model()->rowCount(list->rootIndex());++r) {
            auto index=list->model()->index(r,0,list->rootIndex());
            if(index.data(QFileSystemModel::FilePathRole).toString()==path) {
              list->setCurrentIndex(index);return true;
            }
          }
          return false;
        });
      };
      select(photo);
      QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,list->visualRect(list->currentIndex()).center());
      check(dialog.selectedFiles().contains(photo),"mouse selection did not select the real file");
      waitFor([&] {return preview->property("hasPreview").toBool();});
      QStyleOptionViewItem option;option.initFrom(list);option.rect=QRect(0,0,160,132);
      option.widget=list;option.decorationSize=list->iconSize();
      option.decorationPosition=QStyleOptionViewItem::Top;
      option.state|=QStyle::State_Selected|QStyle::State_Enabled;
      QImage icon(160,132,QImage::Format_ARGB32_Premultiplied);icon.fill(Qt::transparent);
      QPainter painter(&icon);
      list->itemDelegate()->paint(&painter,option,list->currentIndex());painter.end();
      int blue=0;
      for(int y=0;y<icon.height();++y) for(int x=0;x<icon.width();++x) {
        auto c=icon.pixelColor(x,y);if(c.blue()>180 && c.blue()>c.red()+100) ++blue;
      }
      check(blue>500,"file chooser still renders generic icons instead of photo pixels");
      if(repetition==0 && qEnvironmentVariableIsSet("PRISM_CHOOSER_SNAPSHOTS"))
        dialog.grab().save(qEnvironmentVariable("PRISM_CHOOSER_SNAPSHOTS")+"/photo-chooser.png");
      if(repetition==6 && qEnvironmentVariableIsSet("PRISM_CHOOSER_SNAPSHOTS"))
        dialog.grab().save(qEnvironmentVariable("PRISM_CHOOSER_SNAPSHOTS")+"/photo-chooser-light.png");
      // Rapid changes must not display the previous file after a bad JPEG.
      for(int n=0;n<20;++n) {select(broken.fileName());select(photo);}
      select(broken.fileName());
      waitFor([&] {return preview->accessibleDescription().startsWith("Brak podglądu");});
      check(!preview->property("hasPreview").toBool(),"stale preview after corrupt JPEG");
      QTest::keyClick(&dialog,Qt::Key_Escape);
      check(!dialog.isVisible() && QWidget::mouseGrabber()==nullptr,"chooser close/grab regression");
    }
    for(int n=0;n<20;++n) {
      PhotoFileDialog dialog(nullptr,dir.path(),"Zdjęcia (*.jpg)");
      dialog.show();
      // Exercise cancellation before a queued decode completion is handled.
      QMetaObject::invokeMethod(&dialog,"currentChanged",Qt::DirectConnection,Q_ARG(QString,photo));
      dialog.reject();
    }
    for(int edge:{16,32,64,256,1024}) {
      auto image=productIcon().pixmap(edge,edge).toImage();
      check(!image.isNull() && image.pixelColor(0,0).alpha()==0,"app icon background/size");
      if(edge==256 && qEnvironmentVariableIsSet("PRISM_CHOOSER_SNAPSHOTS"))
        image.save(qEnvironmentVariable("PRISM_CHOOSER_SNAPSHOTS")+"/product-icon.png");
    }
    std::cout<<"PASS photo picker: real JPEG grid pixels and selected preview, Unicode, malformed JPEG, rapid selection, 12 close/destroy cycles, 48 MiB cache, scalable About icon\n";
  } catch(const std::exception &error) {std::cerr<<error.what()<<'\n';result=1;}
  vips_shutdown();return result;
}
