#include "language.h"
#include "language_catalog.h"
#include <QCoreApplication>
#include <QHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTranslator>
#include <algorithm>
#include <atomic>

namespace {
std::atomic_bool useEnglish{false};
class PolishQt final : public QTranslator {
  QTranslator system;
public:
  PolishQt() { (void)system.load("qtbase_pl",QLibraryInfo::path(QLibraryInfo::TranslationsPath)); }
  bool isEmpty() const override { return false; }
  QString translate(const char *context,const char *source,const char *comment,int n) const override {
    auto result=system.translate(context,source,comment,n);
    if(!result.isEmpty()) return result;
    static const QHash<QString,QString> fallback{
      {"Open","Otwórz"},{"&Open","&Otwórz"},{"Cancel","Anuluj"},{"&Cancel","&Anuluj"},
      {"Save","Zapisz"},{"&Save","&Zapisz"},{"Look in:","Szukaj w:"},{"File name:","Nazwa pliku:"},
      {"Files of type:","Typ plików:"},{"Name","Nazwa"},{"Size","Rozmiar"},{"Type","Typ"},
      {"Date Modified","Data modyfikacji"},{"Yes","Tak"},{"No","Nie"},{"Close","Zamknij"}};
    return fallback.value(QString::fromUtf8(source));
  }
};
struct State {
  PolishQt polish;
  State() {
    QFile f(PrismLanguage::preferenceFile());
    if(f.open(QIODevice::ReadOnly) && f.size()<=4096) {
      auto data=QJsonDocument::fromJson(f.readAll()).object();
      useEnglish.store(data.value("language").toString()=="en");
    }
    const auto forced=qgetenv("PRISM_LANGUAGE");
    if(forced=="en" || forced=="pl")useEnglish.store(forced=="en");
    if(!useEnglish.load()) QCoreApplication::installTranslator(&polish);
  }
};
State &state() { static State value;return value; }
struct Pattern { QString source,target,prefix;QRegularExpression regex;QList<int> arguments; };
const QList<Pattern> &patterns() {
  static const auto result=[] {
    QList<Pattern> result;
    const QRegularExpression variable("%([1-9][0-9]*)");
    for(auto it=prismEnglishCatalog().cbegin();it!=prismEnglishCatalog().cend();++it) {
      if(!it.key().contains(variable)) continue;
      Pattern p;p.source=it.key();p.target=it.value();QString expression="\\A";qsizetype offset=0;
      auto matches=variable.globalMatch(p.source);
      while(matches.hasNext()) {
        auto m=matches.next();
        expression+=QRegularExpression::escape(p.source.mid(offset,m.capturedStart()-offset))+"(.*?)";
        if(p.arguments.isEmpty())p.prefix=p.source.left(m.capturedStart());
        p.arguments.append(m.captured(1).toInt());offset=m.capturedEnd();
      }
      expression+=QRegularExpression::escape(p.source.mid(offset))+"\\z";
      p.regex=QRegularExpression(expression,QRegularExpression::DotMatchesEverythingOption);
      result.append(p);
    }
    std::sort(result.begin(),result.end(),[](const auto &a,const auto &b){return a.source.size()>b.source.size();});
    return result;
  }();
  return result;
}
QString render(const QString &source,int depth=0) {
  if(source.size()>131072 || depth>4)return source;
  auto exact=prismEnglishCatalog().constFind(source);
  if(exact!=prismEnglishCatalog().cend())return exact.value();
  bool letters=false;for(const auto c:source)if(c.isLetter()){letters=true;break;}
  if(!letters)return source;
  const QRegularExpression variable("%([1-9][0-9]*)");
  for(const auto &p:patterns()) {
    if(!source.startsWith(p.prefix))continue;
    auto m=p.regex.match(source);if(!m.hasMatch())continue;
    QHash<int,QString> args;bool valid=true;
    for(int i=0;i<p.arguments.size();++i) {
      auto value=m.captured(i+1),previous=args.value(p.arguments[i],value);
      if(previous!=value){valid=false;break;}args.insert(p.arguments[i],value);
    }
    if(!valid)continue;
    QString out;qsizetype offset=0;auto variables=variable.globalMatch(p.target);
    while(variables.hasNext()) {
      auto v=variables.next();out+=p.target.mid(offset,v.capturedStart()-offset)+args.value(v.captured(1).toInt(),v.captured());offset=v.capturedEnd();
    }
    return out+p.target.mid(offset);
  }
  // Authored diagnostic prefixes are translated; their arbitrary path/value
  // suffix is preserved byte for byte. Never run replacement across filenames.
  QString prefix,translated,suffix,translatedSuffix;
  for(auto it=prismEnglishCatalog().cbegin();it!=prismEnglishCatalog().cend();++it) {
    const auto &s=it.key();
    if((s.endsWith(": ") || s=="Wersja " || s.endsWith(" • ")) && source.startsWith(s) && s.size()>prefix.size()) {prefix=s;translated=it.value();}
    if((s.startsWith(" • ") || s.startsWith(". ") || s.startsWith(" · ")) && source.endsWith(s) && s.size()>suffix.size()) {suffix=s;translatedSuffix=it.value();}
  }
  if(!prefix.isEmpty() && !suffix.isEmpty() && prefix.size()+suffix.size()<=source.size())
    return translated+source.mid(prefix.size(),source.size()-prefix.size()-suffix.size())+translatedSuffix;
  if(!prefix.isEmpty())return translated+source.mid(prefix.size());
  if(!suffix.isEmpty())return source.left(source.size()-suffix.size())+translatedSuffix;
  if(source.contains('\n')) {
    auto lines=source.split('\n');for(auto &line:lines)line=render(line,depth+1);return lines.join('\n');
  }
  return source;
}
}
namespace PrismLanguage {
QString preferenceFile() {return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/omarchy-prism/language.json";}
void initialize() {if(qApp)(void)state();}
bool english(){return useEnglish.load();}
bool setEnglish(bool enabled,bool persist) {
  auto &s=state();bool saved=true;
  if(persist) {
    const auto path=preferenceFile();
    saved=!QFileInfo(path).isSymLink() && QDir().mkpath(QFileInfo(path).absolutePath());
    if(saved) {
      QSaveFile f(path);auto data=QJsonDocument(QJsonObject{{"version",1},{"language",enabled?"en":"pl"}}).toJson();
      saved=f.open(QIODevice::WriteOnly) && f.write(data)==data.size() && f.commit();
    }
  }
  if(useEnglish.exchange(enabled)==enabled)return saved;
  if(enabled)QCoreApplication::removeTranslator(&s.polish);
  else QCoreApplication::installTranslator(&s.polish);
  return saved;
}
QString text(const QString &source){return english()?render(source):source;}
QString metadata(const QString &source) {
  if(!english() || !source.contains('\n'))return text(source);
  auto rows=source.split('\n');
  // The first line is the literal image filename, not interface vocabulary.
  for(qsizetype i=1;i<rows.size();++i)rows[i]=render(rows[i]);
  return rows.join('\n');
}
}
