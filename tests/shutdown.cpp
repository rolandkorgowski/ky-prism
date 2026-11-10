#define main prismApplicationMain
#include "../src/main.cpp"
#undef main
#include <QtTest/QTest>
#include <QProcessEnvironment>
#include <stdexcept>

// Each scenario must exit its real Qt event loop and destroy all workers.
// A timeout is a failure; killing a stuck child is test cleanup, not success.
int main(int argc,char **argv) {
  if(argc==4 && QByteArray(argv[1])=="--child") {
    QApplication app(argc,argv);app.setStyle(new MenuGlassStyle);
    if(VIPS_INIT(argv[0]))return 2;
    vips_concurrency_set(4);vips_cache_set_max(0);
    const QString scenario=QString::fromLocal8Bit(argv[2]),dir=QString::fromLocal8Bit(argv[3]);
    int rc;
    {
      const auto photo=QFileInfo(dir).isFile()?dir:dir+"/000.jpg";
      Window window({});window.show();window.browse(QFileInfo(photo).absolutePath());
      QTimer::singleShot(0,&window,[&] {
        if(scenario!="empty")window.open(photo);
        if(scenario=="comparison")window.showComparison(photo);
        if(scenario=="thumbnails") {
          for(auto *button:window.findChildren<QPushButton *>())
            if(button->objectName()=="thumbnailMode")button->click();
          auto *cache=window.findChild<ThumbnailCache *>();
          if(cache) {cache->setActive(true);for(const auto &file:QDir(dir).entryInfoList({"*.jpg"},QDir::Files))cache->request(file,512);}
        }
        if(scenario=="duplicates")window.showFileDialog(true);
        if(scenario=="about")window.showAbout(0);
        if(scenario=="favorites")window.showFavorites();
        if(scenario=="language")window.findChild<QPushButton *>("languageToggle")->click();
        if(scenario=="chooser") {
          QTimer::singleShot(30,&window,[&]{window.close();});
          window.findChild<QPushButton *>("openPhoto")->click();
        }
      });
      QTimer::singleShot(45,&window,[&] {std::cout<<"closing\n"<<std::flush;window.close();});
      rc=app.exec();std::cout<<"event loop returned\n"<<std::flush;
    }
    Document::clearSharedPreviews();vips_shutdown();
    std::cout<<"workers and vips stopped\n"<<std::flush;return rc;
  }
  QCoreApplication app(argc,argv);
  if(VIPS_INIT(argv[0]))return 2;
  QTemporaryDir fixture,home;
  try {
    {
      auto image=vips::VImage::black(4096,2732,vips::VImage::option()->set("bands",3)).new_from_image({40,120,200});
      image.jpegsave(fixture.filePath("000.jpg").toUtf8().constData());
      for(int n=1;n<96;++n)if(!QFile::copy(fixture.filePath("000.jpg"),fixture.filePath(QString("%1.jpg").arg(n,3,10,QChar('0')))))throw std::runtime_error("fixture copy");
    }
    for(const auto &scenario:{"empty","loading","comparison","thumbnails","duplicates","language","chooser","about","favorites"}) {
      QProcess child;auto env=QProcessEnvironment::systemEnvironment();
      env.insert("HOME",home.path());env.insert("XDG_DATA_HOME",home.path()+"/data");
      env.insert("XDG_CONFIG_HOME",home.path()+"/config");env.insert("XDG_CACHE_HOME",home.path()+"/cache");
      env.insert("PRISM_JPEG_BACKEND","cpu");env.insert("PRISM_LANGUAGE","pl");
      child.setProcessEnvironment(env);QElapsedTimer timer;timer.start();
      child.start(QCoreApplication::applicationFilePath(),{"--child",scenario,fixture.path()});
      if(!child.waitForFinished(15000)) {
        child.kill();child.waitForFinished(3000);
        std::cerr<<scenario<<": did not exit\n"<<child.readAllStandardOutput().constData()<<child.readAllStandardError().constData();
        throw std::runtime_error("shutdown timeout");
      }
      const auto out=child.readAllStandardOutput(),err=child.readAllStandardError();
      if(child.exitStatus()!=QProcess::NormalExit || child.exitCode()!=0 || !out.contains("workers and vips stopped")) {
        std::cerr<<out.constData()<<err.constData();throw std::runtime_error("shutdown child failed");
      }
      std::cout<<"PASS "<<scenario<<" exit_ms="<<timer.elapsed()<<'\n'<<std::flush;
    }
  } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';vips_shutdown();return 1;}
  vips_shutdown();return 0;
}
