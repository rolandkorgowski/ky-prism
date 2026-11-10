#include "favorites.h"
#include "favorites_ui.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QTemporaryDir>
#include <QThread>
#include <future>
#include <iostream>
#include <stdexcept>
static void check(bool v,const char*s){if(!v)throw std::runtime_error(s);}
static void write(const QString&p,const QByteArray&data){QFile f(p);check(f.open(QIODevice::WriteOnly),"open fixture");check(f.write(data)==data.size(),"write fixture");}
template<class F>static bool rejects(F fn){try{fn();return false;}catch(const std::exception&){return true;}}
static void wait(const std::function<bool()>&done){QElapsedTimer timer;timer.start();while(!done()&&timer.elapsed()<5000){QCoreApplication::processEvents();QThread::msleep(2);}check(done(),"async timeout");}
int main(int argc,char**argv){QApplication app(argc,argv);try{
 QTemporaryDir tmp;QString json=tmp.path()+"/data/favorites.json",image=tmp.path()+"/żółć [#].jpg";write(image,"hello");
 auto token=std::make_shared<std::atomic_bool>(false);FavoriteStore store(json);check(store.load().isEmpty(),"empty store");
 auto rows=store.add(image,token);check(rows.size()==1&&rows.first().md5=="5d41402abc4b2a76b9719d911017c592"&&rows.first().size==5,"correct fingerprint");
 QString id=rows.first().id;check(store.add(image,token).size()==1,"no duplicate path");
 QString root=tmp.path()+"/new";QDir().mkpath(root);QString relocated=root+"/renamed.tiff";check(QFile::rename(image,relocated),"move fixture");write(root+"/wrong.jpg","world");
 auto found=FavoriteStore::find(rows.first(),root,token);check(found.matches==QStringList{relocated},"find renamed file by content");
 check(rejects([&]{store.relocate(id,root+"/wrong.jpg",token);}),"mismatch rejected");
 rows=store.relocate(id,relocated,token);check(rows.first().id==id&&rows.first().path==relocated&&rows.first().name=="renamed.tiff","relocation preserves identity");
 QString second=tmp.path()+"/second.jpg",third=tmp.path()+"/third.jpg";write(second,"second");write(third,"third");
 auto a=std::async(std::launch::async,[&]{return store.add(second,token);});auto b=std::async(std::launch::async,[&]{return store.add(third,token);});a.get();b.get();check(store.load().size()==3,"concurrent writers preserve entries");
 QLockFile lock(json+".lock");check(lock.tryLock(),"test lock");check(rejects([&]{store.remove(id);}),"locked store refused");lock.unlock();
 token->store(true);check(rejects([&]{FavoriteStore::hash(second,token);}),"hash cancellation");check(rejects([&]{FavoriteStore::find(rows.first(),root,token);}),"search cancellation");token->store(false);
 bool edited=false;check(rejects([&]{FavoriteStore::hash(second,token,[&](qint64,qint64){if(!edited){edited=true;write(second,"edited");}});}),"file change during hash rejected");
 FavoriteModel model;model.replace(store.load());check(model.rowCount()==3&&model.data(model.index(0,2)).toString().size()==32,"favorites table");
 {Favorites controller(nullptr,json);controller.reload();wait([&]{return !controller.busy()&&controller.all().size()==3;});controller.remove(id);wait([&]{return !controller.busy()&&controller.all().size()==2;});controller.shutdown();}
 HeartButton heart;heart.setSelected(true,true);check(heart.isSelected()&&heart.animating(),"heart selected and animated");wait([&]{return !heart.animating();});heart.setSelected(false);check(!heart.isSelected()&&!heart.animating(),"heart idle");
 QFile before(json);check(before.open(QIODevice::ReadOnly),"read store");QByteArray good=before.readAll();before.close();write(json,"{broken");check(rejects([&]{store.add(third,token);}),"corrupt store not overwritten");QFile corrupt(json);check(corrupt.open(QIODevice::ReadOnly)&&corrupt.readAll()=="{broken","corrupt original preserved");corrupt.close();write(json,good);
 std::cout<<"PASS: MD5, atomic persistence, deduplication, concurrent writers, cancellation, changed file, relocated filename, corrupt store, model, finite heart animation\n";
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
