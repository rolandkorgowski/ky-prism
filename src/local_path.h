#pragma once
#include <QFile>
#include <QString>
inline bool validLocalPath(const QString &path) {
  return !path.isEmpty() && path.size() <= 8192 &&
         !path.contains(QChar::Null) &&
         QFile::decodeName(QFile::encodeName(path)) == path;
}
