#pragma once
#include <QObject>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QFileSystemWatcher>
#include <QList>
#include <QStringList>
#include <atomic>
#include <functional>
#include <memory>

struct Favorite { QString id,path,name,md5; qint64 size=0; };
using Progress=std::function<void(qint64,qint64)>;
struct FavoriteResult { QList<Favorite> entries; QString error,id,path; QStringList matches; bool limited=false,search=false,added=false; };
class FavoriteStore {
 QString file;
 void save(const QList<Favorite>&) const;
public:
 explicit FavoriteStore(QString filename):file(std::move(filename)){}
 static QString defaultFile();
 QList<Favorite> load() const;
 QList<Favorite> add(const QString&,const std::shared_ptr<std::atomic_bool>&,const Progress& = {}) const;
 QList<Favorite> remove(const QString&) const;
 QList<Favorite> relocate(const QString&,const QString&,const std::shared_ptr<std::atomic_bool>&,const Progress& = {}) const;
 static QString hash(const QString&,const std::shared_ptr<std::atomic_bool>&,const Progress& = {},qint64 *size=nullptr);
 static FavoriteResult find(const Favorite&,const QString&,const std::shared_ptr<std::atomic_bool>&,const Progress& = {});
};
class Favorites final: public QObject {
 Q_OBJECT
 QString filename;
 QList<Favorite> entries;
 QFutureWatcher<FavoriteResult> watcher;
 QThreadPool pool;
 QFileSystemWatcher changes;
 std::shared_ptr<std::atomic_bool> token=std::make_shared<std::atomic_bool>(false);
 bool active=false,reloadPending=false,stopping=false;
 void start(std::function<FavoriteResult(const std::shared_ptr<std::atomic_bool>&,const Progress&)>);
 void watch();
public:
 explicit Favorites(QObject *parent=nullptr,QString filename=FavoriteStore::defaultFile());
 ~Favorites() override;
 const QList<Favorite>&all() const{return entries;}
 bool contains(const QString&) const;
 bool busy() const{return active;}
 void reload();
 void add(const QString&);
 void remove(const QString&);
 void find(const QString&,const QString&);
 void relocate(const QString&,const QString&);
 void cancel(){token->store(true);}
 void shutdown();
 void stopWork();
Q_SIGNALS:
 void changed();
 void working(bool);
 void progress(qint64,qint64);
 void error(const QString&);
 void added(const QString&);
 void found(const QString&,const QStringList&,bool);
};
