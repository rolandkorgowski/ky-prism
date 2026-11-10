#pragma once
#include <QImage>
#include <QString>
namespace ThumbnailDisk {
QString root();
QImage read(const QString &directory, const QString &key);
void write(const QString &directory, const QString &key, const QImage &);
} // namespace ThumbnailDisk
