#define main prismApplicationMain
#include "../src/main.cpp"
#undef main
#include <QtTest/QTest>
#include <QProcessEnvironment>
#include <iostream>
#include <stdexcept>
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
template<class Predicate> void waitFor(Predicate ready) {
  QElapsedTimer clock;clock.start();
  while(!ready() && clock.elapsed()<6000){QCoreApplication::processEvents();QTest::qWait(5);}
  check(ready(),"language test timed out");
}
int main(int argc,char **argv) {
  QTemporaryDir config,data;
  if(argc>1 && QString::fromLocal8Bit(argv[1])=="--probe") {
    QApplication app(argc,argv);PrismLanguage::initialize();std::cout<<(PrismLanguage::english()?"EN":"PL");return 0;
  }
  qputenv("XDG_CONFIG_HOME",config.path().toUtf8());qputenv("XDG_DATA_HOME",data.path().toUtf8());
  qputenv("PRISM_LANGUAGE","pl");qputenv("PRISM_THUMB_CACHE_DIR",(data.path()+"/cache").toUtf8());
  QApplication app(argc,argv);app.setStyle(new MenuGlassStyle);
  app.setApplicationVersion("0.12.2");
  if(VIPS_INIT(argv[0]))return 2;
  int result=0;
  try {
    Window window({});window.show();QCoreApplication::processEvents();
    QTemporaryDir empty;window.browse(empty.path());
    auto *button=dynamic_cast<LanguageButton *>(window.findChild<QPushButton *>("languageToggle"));
    auto *load=window.findChild<QPushButton *>("loadPhoto");
    check(button && load && load->text()=="Załaduj zdjęcie" && button->text()=="PL","initial Polish UI");
    check(window.findChild<QLabel *>("hello")->font().pixelSize()==21,"localized widgets changed the existing theme stylesheet");
    const auto size=button->size();
    window.showAbout(0);auto *about=window.findChild<AboutDialog *>();
    check(about,"about dialog missing");
    button->click();check(button->animating(),"language button did not flip");
    waitFor([&]{return !button->animating() && load->text()=="Load photo";});
    check(button->text()=="EN" && about->windowTitle()=="About · ky-prism","live About translation");
    check(window.findChild<QPushButton *>("openPhoto")->text()=="Open","toolbar translation");
    check(button->size()==size,"language changes button geometry");
    UiLineEdit userInput;userInput.setText("Pliki");check(userInput.text()=="Pliki","language translated user input");
    UiFileListEdit paths;
    paths.setPlainText("2 plików • Ctrl/Shift w bibliotece: zaznacz wiele\n/tmp/Pliki\n/tmp/Załaduj zdjęcie %1.jpg");
    check(paths.toPlainText().startsWith("Files: 2") && paths.toPlainText().endsWith("/tmp/Załaduj zdjęcie %1.jpg"),"copy list translated a path or missed its header");
    check(about==window.findChild<AboutDialog *>(),"language switch rebuilt About");
    check(PrismLanguage::text("Nie można odczytać zdjęcia: /tmp/Załaduj zdjęcie %1.jpg")=="Cannot read the photo: /tmp/Załaduj zdjęcie %1.jpg","diagnostic changed a literal path");
    check(PrismLanguage::text("Wolne: 12.0 GiB / 50.0 GiB")=="Free: 12.0 GiB / 50.0 GiB","formatted status translation");
    check(PrismLanguage::text("Wersja 0.12.2 · przeglądarka zdjęć GPU")=="Version 0.12.2 · GPU photo viewer","composed About version");
    QProcess process;auto env=QProcessEnvironment::systemEnvironment();env.remove("PRISM_LANGUAGE");process.setProcessEnvironment(env);
    process.start(QCoreApplication::applicationFilePath(),{"--probe"});check(process.waitForFinished(6000),"preference probe hung");
    check(process.exitCode()==0 && process.readAllStandardOutput()=="EN","language was not saved for restart");
    const auto snapshots=qEnvironmentVariable("PRISM_LANGUAGE_SNAPSHOTS");
    if(!snapshots.isEmpty()){window.grab().save(snapshots+"/main-en.png");about->grab().save(snapshots+"/about-en.png");button->grab().save(snapshots+"/button-en.png");}
    about->hide();
    QTemporaryDir photos;
    const auto name=QString::fromUtf8("Załaduj zdjęcie %1 [PL].jpg"),path=photos.filePath(name);
    vips::VImage::black(300,200,vips::VImage::option()->set("bands",3)).jpegsave(path.toUtf8().constData());
    std::cerr<<"stage: opening photo\n";
    window.open(path);auto *view=window.findChild<View *>("photoView");
    check(view,"photo renderer missing");
    waitFor([&]{return !view->busy() && !view->imageSize().isEmpty();});
    std::cerr<<"stage: keyboard toggle\n";
    const auto imageSize=view->imageSize();const auto title=window.windowTitle();
    auto *tabs=window.findChild<QTabBar *>();const auto tabName=tabs->tabText(tabs->currentIndex());
    window.activateWindow();QTest::qWait(30);button->setFocus();QTest::keyClick(button,Qt::Key_Space);
    waitFor([&]{return !button->animating() && button->text()=="PL";});
    check(load->text()=="Załaduj zdjęcie" && window.windowTitle()==title,"Polish return changed the image title");
    check(tabs->tabText(tabs->currentIndex())==tabName && tabName==name,"language changed a filename tab");
    check(view->imageSize()==imageSize && window.findChildren<View *>().contains(view),"language reloaded or replaced the renderer");
    std::cerr<<"stage: round trips\n";
    const auto children=window.findChildren<QObject *>().size();
    for(int n=0;n<100;++n){PrismLanguage::setEnglish(true,false);QCoreApplication::processEvents();PrismLanguage::setEnglish(false,false);QCoreApplication::processEvents();}
    check(window.findChildren<QObject *>().size()<=children+2,"language switches accumulated QObject instances");
    check(load->text()=="Załaduj zdjęcie" && !button->animating(),"repeated switch left stale text or idle animation");
    if(!snapshots.isEmpty())button->grab().save(snapshots+"/button-pl.png");
    window.close();
    std::cout<<"PASS language: live PL/EN, About, keyboard activation, flip stops, stable geometry, 100 round trips, filenames and renderer preserved, preference survives process restart\n";
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';result=1;}
  vips_shutdown();return result;
}
