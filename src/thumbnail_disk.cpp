#include "thumbnail_disk.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtEndian>
#include <atomic>
#include <zlib.h>
namespace {
constexpr qsizetype headerSize = 56;
constexpr quint64 diskLimit = 1024ULL * 1024 * 1024;
QString filename(const QString &dir, const QString &key) {
  return dir + '/' +
         QString::fromLatin1(
             QCryptographicHash::hash(("ky-prism-thumb-v3/" + key).toUtf8(),
                                      QCryptographicHash::Sha256)
                 .toHex()) +
         ".kthumb";
}
void append(QByteArray &out, quint32 n) {
  auto big = qToBigEndian(n);
  out.append(reinterpret_cast<const char *>(&big), 4);
}
void prune(const QString &dir) {
  static QMutex lock;
  if (!lock.tryLock())
    return;
  struct Unlock {
    QMutex &lock;
    ~Unlock() { lock.unlock(); }
  } unlock{lock};
  auto entries = QDir(dir).entryInfoList(
      {"*.kthumb"}, QDir::Files | QDir::NoSymLinks, QDir::Time);
  quint64 bytes = 0;
  for (const auto &entry : entries) {
    bytes += quint64(entry.size());
    if (bytes > diskLimit)
      QFile::remove(entry.absoluteFilePath());
  }
}
} // namespace
QString ThumbnailDisk::root() {
  if (qEnvironmentVariable("PRISM_THUMB_DISK_CACHE") == "0")
    return {};
  return qEnvironmentVariable(
      "PRISM_THUMB_CACHE_DIR",
      QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
          "/ky-prism/thumbnails-v3");
}
QImage ThumbnailDisk::read(const QString &dir, const QString &key) {
  if (dir.isEmpty())
    return {};
  const auto name = filename(dir, key);
  QFileInfo info(name);
  if (!info.isFile() || info.isSymLink() || info.size() < headerSize ||
      info.size() > 2 * 1024 * 1024)
    return {};
  QFile file(name);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  const auto bytes = file.read(2 * 1024 * 1024 + 1);
  if (bytes.size() < headerSize || bytes.first(8) != QByteArray("KYTHM003", 8))
    return {};
  auto number = [&](int offset) {
    return qFromBigEndian<quint32>(bytes.constData() + offset);
  };
  const auto w = number(8), h = number(12), raw = number(16),
             compressed = number(20);
  if (!w || !h || w > 512 || h > 512 || raw != w * h * 4 ||
      compressed != quint32(bytes.size() - headerSize))
    return {};
  QImage image(int(w), int(h), QImage::Format_RGBA8888);
  if (image.isNull())
    return {};
  uLongf length = raw;
  if (uncompress(
          image.bits(), &length,
          reinterpret_cast<const Bytef *>(bytes.constData() + headerSize),
          compressed) != Z_OK ||
      length != raw)
    return {};
  const QByteArrayView pixels(reinterpret_cast<const char *>(image.constBits()),
                              raw);
  if (QCryptographicHash::hash(pixels, QCryptographicHash::Sha256) !=
      bytes.mid(24, 32))
    return {};
  return image;
}
void ThumbnailDisk::write(const QString &dir, const QString &key,
                          const QImage &image) {
  if (dir.isEmpty() || image.isNull() || image.width() > 512 ||
      image.height() > 512 || image.format() != QImage::Format_RGBA8888)
    return;
  const bool exists = QFileInfo::exists(dir);
  if (!QDir().mkpath(dir) || QFileInfo(dir).isSymLink())
    return;
  if (!exists &&
      !QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner |
                                      QFile::ExeOwner))
    return;
  const quint32 raw = image.width() * image.height() * 4;
  uLongf length = compressBound(raw);
  QByteArray compressed(qsizetype(length), Qt::Uninitialized);
  if (compress2(reinterpret_cast<Bytef *>(compressed.data()), &length,
                image.constBits(), raw, Z_BEST_SPEED) != Z_OK)
    return;
  compressed.resize(qsizetype(length));
  QByteArray bytes("KYTHM003", 8);
  append(bytes, image.width());
  append(bytes, image.height());
  append(bytes, raw);
  append(bytes, quint32(length));
  bytes += QCryptographicHash::hash(
      QByteArrayView(reinterpret_cast<const char *>(image.constBits()), raw),
      QCryptographicHash::Sha256);
  bytes += compressed;
  QSaveFile out(filename(dir, key));
  out.setDirectWriteFallback(false);
  if (!out.open(QIODevice::WriteOnly) ||
      !out.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
      out.write(bytes) != bytes.size() || !out.commit())
    return;
  static std::atomic_uint writes{0};
  if (writes.fetch_add(1) % 32 == 0)
    prune(dir);
}
