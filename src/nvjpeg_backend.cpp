#include "nvjpeg_backend.h"
#include "nvjpeg_abi.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <dlfcn.h>
#include <mutex>
#include <stdexcept>
#include <sys/stat.h>
namespace {
std::atomic<quint64> successes{0}, misses{0};
struct Loader {
  std::mutex mutex;
  bool tried = false;
  void *library = nullptr, *context = nullptr;
  const PrismNvjpegApi *api = nullptr;
  QString note = "nvJPEG: not initialized";
  void shutdown() {
    std::lock_guard lock(mutex);
    if (context)
      api->destroy(context);
    context = nullptr;
    if (library)
      dlclose(library);
    library = nullptr;
    api = nullptr;
    tried = true;
    note = "nvJPEG: stopped";
  }
  ~Loader() { shutdown(); }
  bool init() {
    std::lock_guard lock(mutex);
    if (tried)
      return context;
    tried = true;
    const auto dir = QCoreApplication::applicationDirPath();
    for (const auto &name :
         {dir + "/libprism_nvjpeg.so", dir + "/../lib/libprism_nvjpeg.so"}) {
      if (!QFileInfo(name).isFile())
        continue;
      QFile metadata(QFileInfo(name).dir().filePath("nvjpeg-build.json"));
      if (metadata.exists()) {
        if (!metadata.open(QIODevice::ReadOnly) ||
            !QJsonDocument::fromJson(metadata.read(4096))
                 .object()
                 .value("enabled")
                 .toBool()) {
          note = "nvJPEG disabled by build metadata";
          continue;
        }
      }
      library =
          dlopen(QFile::encodeName(name).constData(), RTLD_NOW | RTLD_LOCAL);
      if (library)
        break;
      note = QString::fromLocal8Bit(dlerror());
    }
    if (!library) {
      if (note == "nvJPEG: not initialized")
        note = "nvJPEG module not built";
      return false;
    }
    auto entry =
        reinterpret_cast<PrismNvjpegEntry>(dlsym(library, "prism_nvjpeg_api"));
    if (!entry || !(api = entry()) || api->version != 1 || !api->create ||
        !api->destroy || !api->decode) {
      note = "nvJPEG module ABI mismatch";
      return false;
    }
    char text[1024]{};
    context = api->create(text, sizeof(text));
    note = QString::fromUtf8(text);
    return context;
  }
  void error(const QString &text) {
    std::lock_guard lock(mutex);
    note = text.left(1024);
  }
};
Loader &loader() {
  static Loader value;
  return value;
}
void checkCancelled(const std::shared_ptr<std::atomic_bool> &token) {
  if (token->load())
    throw std::runtime_error("Anulowano miniaturę");
}
} // namespace
vips::VImage
NvjpegBackend::thumbnail(const QString &path, const vips::VImage &header,
                         int edge,
                         const std::shared_ptr<std::atomic_bool> &token) {
  const auto mode = qEnvironmentVariable("PRISM_JPEG_BACKEND", "auto");
  if (mode == "cpu")
    return {};
  if (mode != "auto" && mode != "nvjpeg")
    throw std::runtime_error("PRISM_JPEG_BACKEND: auto, cpu lub nvjpeg");
  try {
    checkCancelled(token);
    auto &module = loader();
    if (!module.init())
      throw std::runtime_error(diagnostic().toStdString());
    const int w = header.width(), h = header.height();
    if (header.bands() == 1 && header.get_typeof(VIPS_META_ICC_NAME))
      throw std::runtime_error(
          "nvJPEG: grayscale ICC uses color-managed CPU fallback");
    if (w < 1 || h < 1 || w > 65535 || h > 65535 ||
        quint64(w) * h > 250000000 ||
        (header.bands() != 1 && header.bands() != 3))
      throw std::runtime_error(
          "nvJPEG: unsupported dimensions/components; CPU fallback");
    const auto longest = std::max(w, h);
    const int ow =
        longest > edge ? std::max(1, qRound(double(w) * edge / longest)) : w;
    const int oh =
        longest > edge ? std::max(1, qRound(double(h) * edge / longest)) : h;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
      throw std::runtime_error("nvJPEG: cannot open JPEG");
    struct stat st{};
    if (fstat(file.handle(), &st) || !S_ISREG(st.st_mode) || st.st_size < 2 ||
        st.st_size > 512LL * 1024 * 1024)
      throw std::runtime_error("nvJPEG: invalid compressed source size");
    // Owned input avoids SIGBUS if an original is truncated while reading.
    QByteArray encoded(qsizetype(st.st_size), Qt::Uninitialized);
    qint64 offset = 0;
    while (offset < encoded.size()) {
      checkCancelled(token);
      const auto n =
          file.read(encoded.data() + offset,
                    std::min<qint64>(1024 * 1024, encoded.size() - offset));
      if (n <= 0)
        throw std::runtime_error("nvJPEG: incomplete JPEG input");
      offset += n;
    }
    QByteArray rgb(ow * oh * 3, Qt::Uninitialized);
    char error[1024]{};
    const auto status = module.api->decode(
        module.context,
        reinterpret_cast<const unsigned char *>(encoded.constData()),
        size_t(encoded.size()), w, h,
        reinterpret_cast<unsigned char *>(rgb.data()), ow, oh,
        +[](void *p) {
          return static_cast<std::atomic_bool *>(p)->load() ? 1 : 0;
        },
        token.get(), error, sizeof(error));
    checkCancelled(token);
    if (status)
      throw std::runtime_error(error);
    auto pixels = vips_image_new_from_memory_copy(
        rgb.constData(), size_t(rgb.size()), ow, oh, 3, VIPS_FORMAT_UCHAR);
    if (!pixels)
      throw vips::VError();
    auto image = vips::VImage(pixels).copy(vips::VImage::option()->set(
        "interpretation", VIPS_INTERPRETATION_sRGB));
    if (header.get_typeof(VIPS_META_ICC_NAME)) {
      const void *profile = nullptr;
      size_t n = 0;
      if (vips_image_get_blob(header.get_image(), VIPS_META_ICC_NAME, &profile,
                              &n) ||
          n > 4 * 1024 * 1024)
        throw std::runtime_error("nvJPEG: invalid ICC metadata");
      vips_image_set_blob_copy(image.get_image(), VIPS_META_ICC_NAME, profile,
                               n);
      image = image.icc_transform(
          "srgb",
          vips::VImage::option()->set("embedded", true)->set("depth", 8));
    }
    if (header.get_typeof(VIPS_META_ORIENTATION)) {
      const int orientation = header.get_int(VIPS_META_ORIENTATION);
      if (orientation < 1 || orientation > 8)
        throw std::runtime_error("nvJPEG: invalid EXIF orientation");
      image = image.copy();
      image.set(VIPS_META_ORIENTATION, orientation);
      image = image.autorot();
    }
    if (image.bands() != 3 || image.width() > 512 || image.height() > 512)
      throw std::runtime_error("nvJPEG: invalid color-managed output");
    ++successes;
    return image;
  } catch (const std::exception &e) {
    loader().error(QString::fromUtf8(e.what()));
    checkCancelled(token);
    if (mode == "nvjpeg")
      throw;
    ++misses;
    vips_error_clear();
    return {};
  }
}
QString NvjpegBackend::diagnostic() {
  auto &m = loader();
  std::unique_lock lock(m.mutex, std::try_to_lock);
  if (!lock.owns_lock())
    return "nvJPEG: initialization in background";
  return m.note;
}
quint64 NvjpegBackend::decoded() { return successes.load(); }
quint64 NvjpegBackend::fallbacks() { return misses.load(); }
bool NvjpegBackend::available() { return loader().init(); }
void NvjpegBackend::shutdown() { loader().shutdown(); }
