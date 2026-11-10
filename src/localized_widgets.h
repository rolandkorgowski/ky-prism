#pragma once
#include "language.h"
#include <QAbstractButton>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTabWidget>
#include <type_traits>

class UiAccessibilityFilter final:public QObject {
public:
  explicit UiAccessibilityFilter(QWidget *widget):QObject(widget){widget->installEventFilter(this);}
protected:
  bool eventFilter(QObject *object,QEvent *event) override {
    if(event->type()==QEvent::LanguageChange)
      if(auto *widget=qobject_cast<QWidget *>(object))widget->setAccessibleName(PrismLanguage::text(widget->property("prismAccessibleSource").toString()));
    return false;
  }
};
inline void uiAccessible(QWidget *widget,const QString &source) {
  if(!widget->property("prismAccessibleSource").isValid())new UiAccessibilityFilter(widget);
  widget->setProperty("prismAccessibleSource",source);
  widget->setAccessibleName(PrismLanguage::text(source));
}

// Source UI text is kept separately from its rendered translation. Updates
// happen on assignment / Qt LanguageChange, never on paint or a polling timer.
template<class Base> class UiTextWidget : public Base {
  QString rawText,rawTitle,rawTip,rawName,rawDescription,rawPlaceholder;
  bool hasText=false,hasTitle=false,hasTip=false,hasName=false,hasDescription=false,hasPlaceholder=false;
  void retranslate() {
    if constexpr(std::is_base_of_v<QLabel,Base> || std::is_base_of_v<QAbstractButton,Base>)
      if(hasText)Base::setText(PrismLanguage::text(rawText));
    if(hasTitle)Base::setWindowTitle(PrismLanguage::text(rawTitle));
    if(hasTip)Base::setToolTip(PrismLanguage::text(rawTip));
    if(hasName)Base::setAccessibleName(PrismLanguage::text(rawName));
    if(hasDescription)Base::setAccessibleDescription(PrismLanguage::text(rawDescription));
    if constexpr(std::is_base_of_v<QLineEdit,Base>)
      if(hasPlaceholder)Base::setPlaceholderText(PrismLanguage::text(rawPlaceholder));
  }
public:
  UiTextWidget(const UiTextWidget &)=delete;
  UiTextWidget(UiTextWidget &&)=delete;
  template<class...Args> explicit UiTextWidget(Args&&...args):Base(std::forward<Args>(args)...) {
    PrismLanguage::initialize();
    if constexpr(std::is_base_of_v<QLabel,Base> || std::is_base_of_v<QAbstractButton,Base>)setText(Base::text());
  }
  void setText(const QString &value){
    if constexpr(std::is_base_of_v<QLabel,Base> || std::is_base_of_v<QAbstractButton,Base>) {
      rawText=value;hasText=true;Base::setText(PrismLanguage::text(value));
    } else Base::setText(value); // User input and paths are never UI messages.
  }
  void setWindowTitle(const QString &value){rawTitle=value;hasTitle=true;Base::setWindowTitle(PrismLanguage::text(value));}
  void setToolTip(const QString &value){rawTip=value;hasTip=true;Base::setToolTip(PrismLanguage::text(value));}
  void setAccessibleName(const QString &value){rawName=value;hasName=true;Base::setAccessibleName(PrismLanguage::text(value));}
  void setAccessibleDescription(const QString &value){rawDescription=value;hasDescription=true;Base::setAccessibleDescription(PrismLanguage::text(value));}
  void setPlaceholderText(const QString &value){rawPlaceholder=value;hasPlaceholder=true;Base::setPlaceholderText(PrismLanguage::text(value));}
protected:
  void changeEvent(QEvent *event) override {
    Base::changeEvent(event);
    if(event->type()==QEvent::LanguageChange){retranslate();this->updateGeometry();this->update();}
  }
};
using UiLabel=UiTextWidget<QLabel>;
using UiPushButton=UiTextWidget<QPushButton>;
using UiAbstractButton=UiTextWidget<QAbstractButton>;
using UiDialog=UiTextWidget<QDialog>;
using UiWidget=UiTextWidget<QWidget>;
using UiLineEdit=UiTextWidget<QLineEdit>;

class UiMetadataEdit:public UiTextWidget<QPlainTextEdit> {
  QString raw;
public:
  using UiTextWidget<QPlainTextEdit>::UiTextWidget;
  void setPlainText(const QString &value){raw=value;QPlainTextEdit::setPlainText(PrismLanguage::metadata(value));}
  void clear(){raw.clear();QPlainTextEdit::clear();}
protected:
  void changeEvent(QEvent *event) override {
    UiTextWidget<QPlainTextEdit>::changeEvent(event);
    if(event->type()==QEvent::LanguageChange)QPlainTextEdit::setPlainText(PrismLanguage::metadata(raw));
  }
};
class UiFileListEdit:public UiTextWidget<QPlainTextEdit> {
  QString raw;
  void render() {
    auto rows=raw.split('\n');
    if(!rows.isEmpty() && rows.first().endsWith(" plików • Ctrl/Shift w bibliotece: zaznacz wiele")) {
      rows.first()=PrismLanguage::text(rows.first()+"\n").chopped(1);
      if(rows.last().startsWith("… i ") && rows.last().endsWith(" dalszych plików"))
        rows.last()=PrismLanguage::text("\n"+rows.last()).mid(1);
    }
    QPlainTextEdit::setPlainText(rows.join('\n'));
  }
public:
  using UiTextWidget<QPlainTextEdit>::UiTextWidget;
  void setPlainText(const QString &value){raw=value;render();}
protected:
  void changeEvent(QEvent *event) override {UiTextWidget<QPlainTextEdit>::changeEvent(event);if(event->type()==QEvent::LanguageChange)render();}
};
class UiTabWidget:public QTabWidget {
public:
  using QTabWidget::QTabWidget;
  int addTab(QWidget *page,const QString &source){page->setProperty("prismTabSource",source);return QTabWidget::addTab(page,PrismLanguage::text(source));}
protected:
  void changeEvent(QEvent *event) override {
    QTabWidget::changeEvent(event);
    if(event->type()==QEvent::LanguageChange)
      for(int i=0;i<count();++i)setTabText(i,PrismLanguage::text(widget(i)->property("prismTabSource").toString()));
  }
};
constexpr int UiSourceRole=Qt::UserRole+31017;
inline QStandardItem *uiItem(const QString &source){auto *item=new QStandardItem(PrismLanguage::text(source));item->setData(source,UiSourceRole);return item;}
class UiStandardModel:public QStandardItemModel {
  QStringList headers;
public:
  explicit UiStandardModel(QObject *parent=nullptr):QStandardItemModel(parent){if(parent)parent->installEventFilter(this);}
  void setHorizontalHeaderLabels(const QStringList &source){headers=source;QStringList result;for(const auto &s:headers)result<<PrismLanguage::text(s);QStandardItemModel::setHorizontalHeaderLabels(result);}
protected:
  bool eventFilter(QObject *object,QEvent *event) override {
    if(event->type()==QEvent::LanguageChange) {
      setHorizontalHeaderLabels(headers);
      for(int row=0;row<rowCount();++row)for(int column=0;column<columnCount();++column)
        if(auto *cell=item(row,column);cell && cell->data(UiSourceRole).isValid())cell->setText(PrismLanguage::text(cell->data(UiSourceRole).toString()));
    }
    return QStandardItemModel::eventFilter(object,event);
  }
};
class UiMessageBox {
public:
  template<class...Args> static auto warning(QWidget *parent,const QString &title,const QString &body,Args&&...args){return QMessageBox::warning(parent,PrismLanguage::text(title),PrismLanguage::text(body),std::forward<Args>(args)...);}
  template<class...Args> static auto information(QWidget *parent,const QString &title,const QString &body,Args&&...args){return QMessageBox::information(parent,PrismLanguage::text(title),PrismLanguage::text(body),std::forward<Args>(args)...);}
  template<class...Args> static auto question(QWidget *parent,const QString &title,const QString &body,Args&&...args){return QMessageBox::question(parent,PrismLanguage::text(title),PrismLanguage::text(body),std::forward<Args>(args)...);}
  template<class...Args> static auto critical(QWidget *parent,const QString &title,const QString &body,Args&&...args){return QMessageBox::critical(parent,PrismLanguage::text(title),PrismLanguage::text(body),std::forward<Args>(args)...);}
};
class UiInputDialog {
public:
  template<class...Args> static auto getText(QWidget *parent,const QString &title,const QString &label,Args&&...args){return QInputDialog::getText(parent,PrismLanguage::text(title),PrismLanguage::text(label),std::forward<Args>(args)...);}
  template<class...Args> static QString getItem(QWidget *parent,const QString &title,const QString &label,const QStringList &items,Args&&...args){
    QStringList displayed;for(const auto &item:items)displayed<<PrismLanguage::text(item);
    const auto choice=QInputDialog::getItem(parent,PrismLanguage::text(title),PrismLanguage::text(label),displayed,std::forward<Args>(args)...);
    const auto index=displayed.indexOf(choice);return index>=0?items.at(index):choice;
  }
};
class UiFileDialog:public UiTextWidget<QFileDialog> {
public:
  using UiTextWidget<QFileDialog>::UiTextWidget;
  static QString getOpenFileName(QWidget *parent,const QString &title,const QString &directory={},const QString &filter={},QString *selected=nullptr,QFileDialog::Options options={}){return QFileDialog::getOpenFileName(parent,PrismLanguage::text(title),directory,PrismLanguage::text(filter),selected,options);}
  static QString getSaveFileName(QWidget *parent,const QString &title,const QString &directory={},const QString &filter={},QString *selected=nullptr,QFileDialog::Options options={}){return QFileDialog::getSaveFileName(parent,PrismLanguage::text(title),directory,PrismLanguage::text(filter),selected,options);}
  static QString getExistingDirectory(QWidget *parent,const QString &title,const QString &directory={},QFileDialog::Options options=QFileDialog::ShowDirsOnly){return QFileDialog::getExistingDirectory(parent,PrismLanguage::text(title),directory,options);}
};
