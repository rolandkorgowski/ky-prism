#pragma once
#include "glass_widgets.h"
#include <QVariantAnimation>
class LanguageButton final:public GlassButton {
  QVariantAnimation flip;
  bool target=false,committed=false;
  void commit() {
    if(committed)return;
    committed=true;
    const bool saved=PrismLanguage::setEnglish(target);
    setText(target?"EN":"PL");
    setToolTip(saved?(target?"Przełącz na polski • Ctrl+Alt+L":"Przełącz na angielski • Ctrl+Alt+L"):"Nie można zapamiętać języka.");
    setAccessibleName("Język interfejsu");
  }
protected:
  void changeEvent(QEvent *event) override {
    GlassButton::changeEvent(event);
    if(event->type()==QEvent::LanguageChange) {
      setText(PrismLanguage::english()?"EN":"PL");
      setToolTip(PrismLanguage::english()?"Przełącz na polski • Ctrl+Alt+L":"Przełącz na angielski • Ctrl+Alt+L");
    }
  }
  void hideEvent(QHideEvent *event) override {
    if(animating()){flip.stop();commit();}
    GlassButton::hideEvent(event);
  }
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    const auto phase=animating()?flip.currentValue().toReal():0.;
    const auto scale=animating()?std::abs(std::cos(phase*std::acos(-1.))):1.;
    p.translate(width()/2.,height()/2.);p.scale(std::max(.015,scale),1);p.translate(-width()/2.,-height()/2.);
    paintGlass(p,rect(),window()->palette(),underMouse(),isDown(),false,hasFocus(),isEnabled(),9);
    const QRectF r=QRectF(rect()).adjusted(3,3,-3,-4);
    QPainterPath clip;clip.addRoundedRect(r,7,7);p.save();p.setClipPath(clip);
    p.setOpacity(.46);
    if(PrismLanguage::english()) {
      p.fillRect(r,QColor("#163b72"));
      p.setPen(QPen(Qt::white,r.height()*.24));
      p.drawLine(r.topLeft(),r.bottomRight());p.drawLine(r.topRight(),r.bottomLeft());
      p.setPen(QPen(QColor("#c8253b"),r.height()*.08));
      p.drawLine(r.topLeft(),r.bottomRight());p.drawLine(r.topRight(),r.bottomLeft());
      p.fillRect(QRectF(r.left(),r.center().y()-r.height()*.17,r.width(),r.height()*.34),Qt::white);
      p.fillRect(QRectF(r.center().x()-r.width()*.12,r.top(),r.width()*.24,r.height()),Qt::white);
      p.fillRect(QRectF(r.left(),r.center().y()-r.height()*.095,r.width(),r.height()*.19),QColor("#c8253b"));
      p.fillRect(QRectF(r.center().x()-r.width()*.065,r.top(),r.width()*.13,r.height()),QColor("#c8253b"));
    } else {
      p.fillRect(r,Qt::white);p.fillRect(QRectF(r.left(),r.center().y(),r.width(),r.height()/2),QColor("#dc2943"));
    }
    p.restore();
    auto veil=window()->palette().color(QPalette::Window);veil.setAlpha(105);p.fillPath(clip,veil);
    auto font=this->font();font.setWeight(QFont::DemiBold);p.setFont(font);
    auto shadow=window()->palette().color(QPalette::Window);shadow.setAlpha(230);
    p.setPen(QPen(shadow,3));QPainterPath letters;
    const auto bounds=QFontMetricsF(font).boundingRect(text());
    letters.addText(QPointF((width()-bounds.width())/2-bounds.left(),(height()-bounds.height())/2-bounds.top()),font,text());
    p.drawPath(letters);p.fillPath(letters,window()->palette().color(QPalette::WindowText));
  }
public:
  explicit LanguageButton(QWidget *parent=nullptr):GlassButton({},parent) {
    setObjectName("languageToggle");setFixedSize(58,34);target=PrismLanguage::english();
    setText(target?"EN":"PL");setAccessibleName("Język interfejsu");
    setToolTip(target?"Przełącz na polski • Ctrl+Alt+L":"Przełącz na angielski • Ctrl+Alt+L");
    flip.setDuration(340);flip.setStartValue(0.);flip.setEndValue(1.);
    connect(&flip,&QVariantAnimation::valueChanged,this,[this](const QVariant &value){if(value.toReal()>=.5)commit();update();});
    connect(&flip,&QVariantAnimation::finished,this,[this]{commit();update();});
    connect(this,&QPushButton::clicked,this,[this]{if(animating())return;target=!PrismLanguage::english();committed=false;flip.start();});
  }
  bool animating()const{return flip.state()==QAbstractAnimation::Running;}
};
