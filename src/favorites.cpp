#include "favorites.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent>
#include <sys/stat.h>
#include <stdexcept>
namespace {
void fail(const QString&s){throw std::runtime_error(s.toStdString());}
void cancelled(const std::shared_ptr<std::atomic_bool>&t){if(t->load())fail("Anulowano operację ulubionych.");}
QString absolute(const QString&p){return QDir::cleanPath(QFileInfo(p).absoluteFilePath());}
void prepare(const QString&file){if(!QDir().mkpath(QFileInfo(file).absolutePath()))fail("Nie można utworzyć katalogu ulubionych.");if(QFileInfo(file).isSymLink())fail("Plik ulubionych jest dowiązaniem — przerwano zapis.");}
}
QString FavoriteStore::defaultFile(){return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/omarchy-prism/favorites.json";}
QList<Favorite> FavoriteStore::load() const {
 QFile f(file);if(!f.exists())return {};
 if(f.size()>8*1024*1024 || !f.open(QIODevice::ReadOnly))fail("Nie można odczytać ulubionych (limit 8 MiB).");
 QJsonParseError error;auto json=QJsonDocument::fromJson(f.readAll(),&error);
 auto root=json.object();if(error.error!=QJsonParseError::NoError || root.value("version").toInt()!=1 || !root.value("photos").isArray())fail("Uszkodzony format ulubionych. Oryginalny plik pozostawiono bez zmian.");
 auto rows=root.value("photos").toArray();if(rows.size()>10000)fail("Przekroczono limit 10 000 ulubionych.");
 QList<Favorite> result;QSet<QString> ids,paths;QRegularExpression md5("^[0-9a-f]{32}$");
 for(const auto&value:rows){auto o=value.toObject();bool ok=false;Favorite e{o.value("id").toString(),o.value("path").toString(),o.value("name").toString(),o.value("md5").toString(),o.value("size").toString().toLongLong(&ok)};
  if(QUuid(e.id).isNull() || !QDir::isAbsolutePath(e.path) || e.path.size()>8192 || e.path.contains(QChar(0)) || e.name.isEmpty() || e.name.size()>1024 || e.name.contains(QChar(0)) || !md5.match(e.md5).hasMatch() || !ok || e.size<0 || ids.contains(e.id) || paths.contains(e.path))fail("Nieprawidłowy wpis ulubionych. Pliku nie nadpisano.");
  ids.insert(e.id);paths.insert(e.path);result.append(e);
 }return result;
}
void FavoriteStore::save(const QList<Favorite>&rows) const {
 if(rows.size()>10000)fail("Limit ulubionych: 10 000 zdjęć.");
 QJsonArray list;for(const auto&e:rows)list.append(QJsonObject{{"id",e.id},{"path",e.path},{"name",e.name},{"md5",e.md5},{"size",QString::number(e.size)}});
 QByteArray data=QJsonDocument(QJsonObject{{"version",1},{"photos",list}}).toJson();if(data.size()>8*1024*1024)fail("Ulubione przekraczają limit 8 MiB.");
 QSaveFile out(file);out.setDirectWriteFallback(false);
 if(!out.open(QIODevice::WriteOnly))fail("Nie można zapisać ulubionych.");
 out.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
 if(out.write(data)!=data.size() || !out.commit())fail("Zapis ulubionych nie powiódł się — poprzednia lista pozostała bez zmian.");
}
QString FavoriteStore::hash(const QString&path,const std::shared_ptr<std::atomic_bool>&token,const Progress&progress,qint64 *size) {
 cancelled(token);QFile f(path);if(!QFileInfo(path).isFile() || !f.open(QIODevice::ReadOnly))fail("Nie można odczytać zdjęcia: "+path.left(512));
 struct stat before{},after{},named{};if(fstat(f.handle(),&before))fail("Nie można sprawdzić pliku.");
 QCryptographicHash md5(QCryptographicHash::Md5);QByteArray buffer(1024*1024,Qt::Uninitialized);qint64 count=0;QElapsedTimer clock;clock.start();
 while(true){cancelled(token);auto n=f.read(buffer.data(),buffer.size());if(n<0)fail("Błąd odczytu podczas liczenia MD5.");if(n==0)break;md5.addData(QByteArrayView(buffer.constData(),n));count+=n;if(progress && clock.elapsed()>=100){progress(count,before.st_size);clock.restart();}}
 if(progress)progress(count,before.st_size);
 if(fstat(f.handle(),&after) || stat(QFile::encodeName(path).constData(),&named) || before.st_size!=after.st_size || before.st_mtim.tv_sec!=after.st_mtim.tv_sec || before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec || before.st_ctim.tv_sec!=after.st_ctim.tv_sec || before.st_ctim.tv_nsec!=after.st_ctim.tv_nsec || after.st_ino!=named.st_ino || after.st_dev!=named.st_dev || count!=after.st_size)fail("Zdjęcie zmieniło się podczas odczytu — spróbuj ponownie.");
 cancelled(token);if(size)*size=after.st_size;return QString::fromLatin1(md5.result().toHex());
}
QList<Favorite> FavoriteStore::add(const QString&path,const std::shared_ptr<std::atomic_bool>&token,const Progress&progress) const {
 qint64 size=0;QString full=absolute(path),digest=hash(full,token,progress,&size);Favorite entry{QUuid::createUuid().toString(QUuid::WithoutBraces),full,QFileInfo(full).fileName(),digest,size};
 prepare(file);QLockFile lock(file+".lock");if(!lock.tryLock(1500))fail("Ulubione zapisuje inne okno. Spróbuj za chwilę.");
 cancelled(token);auto rows=load();for(auto&e:rows)if(e.path==full){entry.id=e.id;e=entry;save(rows);return rows;}
 rows.append(entry);save(rows);return rows;
}
QList<Favorite> FavoriteStore::remove(const QString&id) const {
 prepare(file);QLockFile lock(file+".lock");if(!lock.tryLock(1500))fail("Ulubione zapisuje inne okno.");
 auto rows=load();rows.removeIf([&](const Favorite&e){return e.id==id;});save(rows);return rows;
}
QList<Favorite> FavoriteStore::relocate(const QString&id,const QString&path,const std::shared_ptr<std::atomic_bool>&token,const Progress&progress) const {
 qint64 size=0;QString full=absolute(path),digest=hash(full,token,progress,&size);
 prepare(file);QLockFile lock(file+".lock");if(!lock.tryLock(1500))fail("Ulubione zapisuje inne okno.");cancelled(token);auto rows=load();
 for(auto&e:rows)if(e.id!=id && e.path==full)fail("Ta ścieżka już znajduje się w ulubionych.");
 for(auto&e:rows)if(e.id==id){if(e.md5!=digest || e.size!=size)fail("Wybrany plik ma inną zawartość niż zapisane zdjęcie.");e.path=full;e.name=QFileInfo(full).fileName();save(rows);return rows;}
 fail("Wpis usunięto w innym oknie.");return {};
}
FavoriteResult FavoriteStore::find(const Favorite&entry,const QString&root,const std::shared_ptr<std::atomic_bool>&token,const Progress&progress) {
 if(!QFileInfo(root).isDir())fail("Wskaż istniejący katalog do przeszukania.");
 FavoriteResult result;result.search=true;result.id=entry.id;QElapsedTimer clock;clock.start();qint64 seen=0;
 QDirIterator it(root,QDir::Files|QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot|QDir::NoSymLinks,QDirIterator::Subdirectories);
 while(it.hasNext()){
  cancelled(token);QString candidate=it.next();++seen;
  if(progress && clock.elapsed()>=100){progress(seen,-1);clock.restart();}
  if(it.fileInfo().isFile() && it.fileInfo().isReadable() && it.fileInfo().size()==entry.size){try{if(hash(candidate,token,[&](qint64,qint64){if(progress)progress(seen,-1);})==entry.md5)result.matches.append(absolute(candidate));}catch(const std::exception&){cancelled(token);}}
  if(seen>=500000 || result.matches.size()>=20){result.limited=true;break;}
 }cancelled(token);return result;
}
Favorites::Favorites(QObject *parent,QString file):QObject(parent),filename(std::move(file)) {
 pool.setMaxThreadCount(1);pool.setThreadPriority(QThread::LowPriority);
 connect(&watcher,&QFutureWatcher<FavoriteResult>::finished,this,[this]{
  auto result=watcher.result();active=false;Q_EMIT working(false);
  if(!result.error.isEmpty())Q_EMIT error(result.error);
  else if(result.search){if(token->load())Q_EMIT error("Anulowano wyszukiwanie zdjęcia.");else Q_EMIT found(result.id,result.matches,result.limited);}
  else {entries=result.entries;Q_EMIT changed();if(result.added)Q_EMIT added(result.path);}
  watch();if(reloadPending && !active){reloadPending=false;reload();}
 });
 connect(&changes,&QFileSystemWatcher::directoryChanged,this,[this]{if(active)reloadPending=true;else reload();});watch();
}
Favorites::~Favorites(){shutdown();}
void Favorites::stopWork(){stopping=true;token->store(true);reloadPending=false;changes.disconnect(this);watcher.disconnect(this);}
void Favorites::shutdown(){stopWork();pool.waitForDone();}
void Favorites::watch(){QString parent=QFileInfo(filename).absolutePath();if(QDir(parent).exists()&&!changes.directories().contains(parent))changes.addPath(parent);}
bool Favorites::contains(const QString&path)const {for(const auto&e:entries)if(e.path==absolute(path))return true;return false;}
void Favorites::start(std::function<FavoriteResult(const std::shared_ptr<std::atomic_bool>&,const Progress&)> job){
 if(stopping)return;
 if(active){Q_EMIT error("Trwa operacja ulubionych. Możesz ją anulować.");return;}
 active=true;token=std::make_shared<std::atomic_bool>(false);auto t=token;Q_EMIT working(true);
 watcher.setFuture(QtConcurrent::run(&pool,[this,t,job=std::move(job)]{FavoriteResult r;try{r=job(t,[this,t](qint64 n,qint64 total){QMetaObject::invokeMethod(this,[this,t,n,total]{if(t==token && active && !t->load())Q_EMIT progress(n,total);},Qt::QueuedConnection);});}catch(const std::exception&e){r.error=QString::fromUtf8(e.what()).left(2048);}return r;}));
}
void Favorites::reload(){QString f=filename;start([f](const auto&,const auto&){FavoriteResult r;r.entries=FavoriteStore(f).load();return r;});}
void Favorites::add(const QString&path){QString f=filename;start([f,path](const auto&t,const auto&p){FavoriteResult r;r.entries=FavoriteStore(f).add(path,t,p);r.added=true;r.path=absolute(path);return r;});}
void Favorites::remove(const QString&id){QString f=filename;start([f,id](const auto&,const auto&){FavoriteResult r;r.entries=FavoriteStore(f).remove(id);return r;});}
void Favorites::find(const QString&id,const QString&root){for(const auto&e:entries)if(e.id==id){start([e,root](const auto&t,const auto&p){return FavoriteStore::find(e,root,t,p);});return;}Q_EMIT error("Najpierw wybierz zdjęcie na liście ulubionych.");}
void Favorites::relocate(const QString&id,const QString&path){QString f=filename;start([f,id,path](const auto&t,const auto&p){FavoriteResult r;r.entries=FavoriteStore(f).relocate(id,path,t,p);return r;});}
