#include "raster_source.h"
#include <QByteArray>
#include <QFile>
#include <QtEndian>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace {
struct Input {
  int fd = -1;
  qint64 size = 0, position = 0, patchOffset = 0;
  QByteArray patch;
  std::mutex mutex;
  std::shared_ptr<std::atomic_bool> cancel;
  ~Input() {
    if (fd >= 0)
      ::close(fd);
  }
  bool stopped() const { return cancel && cancel->load(); }
  QByteArray at(quint64 offset, quint64 count) const {
    if (offset > quint64(size) || count > quint64(size) - offset ||
        count > 100000)
      throw std::runtime_error("Nieprawidłowy nagłówek TIFF");
    QByteArray out(qsizetype(count), Qt::Uninitialized);
    quint64 done = 0;
    while (done < count) {
      auto n = ::pread(fd, out.data() + done, size_t(count - done),
                       off_t(offset + done));
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        throw std::runtime_error("Niepełny nagłówek TIFF");
      done += quint64(n);
    }
    return out;
  }
};
template <class T> T number(const char *p, bool little) {
  return little ? qFromLittleEndian<T>(p) : qFromBigEndian<T>(p);
}
template <class T> void put(char *p, T n, bool little) {
  if (little)
    qToLittleEndian<T>(n, p);
  else
    qToBigEndian<T>(n, p);
}
void prepare(Input &in, bool &rendered, bool &rgbPreview, bool &skipped) {
  if (in.size < 8)
    return;
  auto head = in.at(0, std::min<qint64>(16, in.size));
  bool little = head.startsWith("II"), big = head.startsWith("MM");
  if (!little && !big)
    return;
  auto magic = number<quint16>(head.data() + 2, little);
  if (magic != 42 && magic != 43)
    return;
  bool wide = magic == 43;
  if (wide &&
      (head.size() < 16 || number<quint16>(head.data() + 4, little) != 8 ||
       number<quint16>(head.data() + 6, little) != 0))
    throw std::runtime_error("Nieprawidłowy BigTIFF");
  quint64 offset = wide ? number<quint64>(head.data() + 8, little)
                        : number<quint32>(head.data() + 4, little);
  if (!offset)
    return;
  int countBytes = wide ? 8 : 2, entryBytes = wide ? 20 : 12,
      nextBytes = wide ? 8 : 4;
  auto countData = in.at(offset, countBytes);
  quint64 count = wide ? number<quint64>(countData.data(), little)
                       : number<quint16>(countData.data(), little);
  if (count > 4096)
    throw std::runtime_error("TIFF: zbyt wiele pól w katalogu");
  quint64 length = countBytes + count * entryBytes + nextBytes;
  auto table = in.at(offset, length);
  QByteArray entries;
  quint64 kept = 0;
  int photometric = -1, samples = -1;
  bool reduced = false, rawMetadata = false;
  for (quint64 i = 0; i < count; ++i) {
    auto e = table.constData() + countBytes + i * entryBytes;
    auto tag = number<quint16>(e, little),
         type = number<quint16>(e + 2, little);
    quint64 n =
        wide ? number<quint64>(e + 4, little) : number<quint32>(e + 4, little);
    const char *v = e + (wide ? 12 : 8);
    if (n == 1 && type == 3) {
      if (tag == 262)
        photometric = number<quint16>(v, little);
      if (tag == 277)
        samples = number<quint16>(v, little);
    }
    if (n == 1 && tag == 254 && type == 4)
      reduced = (number<quint32>(v, little) & 1) != 0;
    if (n == 1 && tag == 255 && type == 3)
      reduced = reduced || number<quint16>(v, little) == 2;
    if (tag == 50706 || tag == 33421 || tag == 33422)
      rawMetadata = true;
    // These BYTE/UNDEFINED blobs contain private editing/layer information,
    // never pixel storage or the ICC profile. Check byte offsets before
    // ignoring.
    bool drop = (tag == 37724 || tag == 34377) && (type == 1 || type == 7) &&
                n > 1024 * 1024;
    if (drop) {
      auto at = wide ? number<quint64>(v, little) : number<quint32>(v, little);
      if (at > quint64(in.size) || n > quint64(in.size) - at)
        throw std::runtime_error("TIFF: uszkodzone dane warstw");
      skipped = true;
    } else {
      entries.append(e, entryBytes);
      ++kept;
    }
  }
  rgbPreview = (photometric == 2 || photometric == 5 ||
                (photometric >= 8 && photometric <= 10)) &&
               samples >= 3 && samples <= 5;
  rendered = rgbPreview && !reduced && !rawMetadata;
  if (!skipped)
    return;
  in.patch = QByteArray(qsizetype(length), 0);
  in.patchOffset = qint64(offset);
  if (wide)
    put<quint64>(in.patch.data(), kept, little);
  else
    put<quint16>(in.patch.data(), quint16(kept), little);
  memcpy(in.patch.data() + countBytes, entries.constData(),
         size_t(entries.size()));
  memcpy(in.patch.data() + countBytes + kept * entryBytes,
         table.constData() + countBytes + count * entryBytes,
         size_t(nextBytes));
}
} // namespace
RasterSource::RasterSource(const QString &path,
                           const std::shared_ptr<std::atomic_bool> &cancel) {
  auto input = std::make_unique<Input>();
  input->cancel = cancel;
  input->fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC);
  struct stat st{};
  if (input->fd < 0 || fstat(input->fd, &st) || !S_ISREG(st.st_mode) ||
      st.st_size < 0)
    throw std::runtime_error("Nie można otworzyć źródła obrazu");
  input->size = st.st_size;
  prepare(*input, renderedTiff, rgbPreview, skippedLayers);
  auto source = vips_source_custom_new();
  if (!source)
    throw std::bad_alloc();
  auto state = input.release();
  g_object_set_data_full(
      G_OBJECT(source), "prism-read-only-input", state,
      +[](void *p) { delete static_cast<Input *>(p); });
  g_signal_connect(
      source, "read",
      G_CALLBACK((+[](VipsSourceCustom *, void *buffer, gint64 requested,
                      void *p) -> gint64 {
        auto &s = *static_cast<Input *>(p);
        std::lock_guard guard(s.mutex);
        if (s.stopped() || requested < 0)
          return -1;
        auto count = std::min<qint64>(requested, s.size - s.position);
        if (!count)
          return 0;
        ssize_t n;
        do {
          n = pread(s.fd, buffer, size_t(count), s.position);
        } while (n < 0 && errno == EINTR);
        if (n <= 0)
          return n;
        auto begin = std::max(s.position, s.patchOffset),
             end = std::min(s.position + n, s.patchOffset + s.patch.size());
        if (begin < end)
          memcpy(static_cast<char *>(buffer) + begin - s.position,
                 s.patch.constData() + begin - s.patchOffset,
                 size_t(end - begin));
        s.position += n;
        return n;
      })),
      state);
  g_signal_connect(source, "seek",
                   G_CALLBACK((+[](VipsSourceCustom *, gint64 offset,
                                   int whence, void *p) -> gint64 {
                     auto &s = *static_cast<Input *>(p);
                     std::lock_guard guard(s.mutex);
                     if (s.stopped())
                       return -1;
                     qint64 base = whence == SEEK_SET   ? 0
                                   : whence == SEEK_CUR ? s.position
                                   : whence == SEEK_END ? s.size
                                                        : -1;
                     if (base < 0 || offset < -base || offset > s.size - base)
                       return -1;
                     s.position = base + offset;
                     return s.position;
                   })),
                   state);
  value = VIPS_SOURCE(source);
}
RasterSource::~RasterSource() {
  if (value)
    g_object_unref(value);
}
