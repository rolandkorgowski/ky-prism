#pragma once
#include "localized_widgets.h"
#include "favorites.h"
#include <QAbstractButton>
#include <QDialog>
#include <QAbstractTableModel>
#include <QVariantAnimation>
#include <QTableView>
#include <QSortFilterProxyModel>
#include <QLabel>
class HeartButton final: public UiAbstractButton {
 Q_OBJECT
 bool selected=false;
 QVariantAnimation pulse;
protected:
 void paintEvent(QPaintEvent*) override;
public:
 explicit HeartButton(QWidget *parent=nullptr);
 void setSelected(bool,bool animate=false);
 bool isSelected()const{return selected;}
 bool animating()const{return pulse.state()==QAbstractAnimation::Running;}
};
class FavoriteModel final: public QAbstractTableModel {
 QList<Favorite> rows;
public:
 using QAbstractTableModel::QAbstractTableModel;
 int rowCount(const QModelIndex&parent={})const override{return parent.isValid()?0:rows.size();}
 int columnCount(const QModelIndex&parent={})const override{return parent.isValid()?0:4;}
 QVariant data(const QModelIndex&,int role=Qt::DisplayRole)const override;
 QVariant headerData(int,Qt::Orientation,int role=Qt::DisplayRole)const override;
 void replace(const QList<Favorite>&value){beginResetModel();rows=value;endResetModel();}
 Favorite at(int index)const{return index>=0&&index<rows.size()?rows.at(index):Favorite{};}
};
class FavoritesDialog final: public UiDialog {
 Q_OBJECT
 Favorites *favorites;
 FavoriteModel model;
 QSortFilterProxyModel proxy;
 QTableView *table;
 UiLabel *message;
 Favorite selected()const;
 void openSelected();
public:
 explicit FavoritesDialog(Favorites*,QWidget*parent=nullptr);
signals:
 void openRequested(const QString&);
};
