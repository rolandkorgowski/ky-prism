#pragma once
#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
// Minimal valid TIFF IFD0 + IFD1 inside EXIF APP1. No production parser involved.
inline QByteArray withExifThumbnail(const QByteArray &jpeg,const QByteArray &thumbnail) {
 QByteArray tiff;QDataStream data(&tiff,QIODevice::WriteOnly);data.setByteOrder(QDataStream::LittleEndian);
 data.writeRawData("II",2);data<<quint16(42)<<quint32(8)<<quint16(0)<<quint32(14)<<quint16(2);
 data<<quint16(0x201)<<quint16(4)<<quint32(1)<<quint32(44);
 data<<quint16(0x202)<<quint16(4)<<quint32(1)<<quint32(thumbnail.size())<<quint32(0);
 QByteArray exif("Exif\0\0",6);exif+=tiff+thumbnail;int n=exif.size()+2;
 QByteArray marker=QByteArray::fromHex("ffe1");marker.append(char(n>>8));marker.append(char(n));marker+=exif;
 return jpeg.left(2)+marker+jpeg.mid(2);
}
