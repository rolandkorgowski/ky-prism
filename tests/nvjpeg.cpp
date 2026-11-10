#include "nvjpeg_backend.h"
#include "pixel_simd.h"
#include "thumbnail_cache.h"
#include <QColorSpace>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtConcurrent>
#include <cmath>
#include <iostream>
static void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  const auto args = app.arguments();
  const bool required = args.contains("--require-nvjpeg");
  const bool cpuOnly = args.contains("--cpu");
  const bool compare = cpuOnly || !args.contains("--no-reference") ||
                       args.contains("--reference");
  const bool synthetic = argc < 2 || args.at(1).startsWith("--");
  if (VIPS_INIT(argv[0]))
    return 2;
  vips_concurrency_set(2);
  vips_cache_set_max(0);
  int result = 0;
  QJsonObject report{{"cpu_simd", PixelSimd::backend()},
                     {"synthetic", synthetic},
                     {"thumbnail_disk_cache_used", false},
                     {"os_cache_dropped", false}};
  report["cpu_reference_before_gpu"] = compare && !cpuOnly;
  try {
    QTemporaryDir tmp;
    QString directory = synthetic ? tmp.path() : args.at(1);
    if (synthetic) {
      auto image = vips::VImage::black(2003, 1337,
                                       vips::VImage::option()->set("bands", 3))
                       .new_from_image({200, 70, 25});
      const auto profile = QColorSpace(QColorSpace::SRgb).iccProfile();
      vips_image_set_blob_copy(image.get_image(), VIPS_META_ICC_NAME,
                               profile.constData(), profile.size());
      image.jpegsave((directory + "/flat.jpg").toUtf8().constData());
      image = image.copy();
      image.set(VIPS_META_ORIENTATION, 6);
      image.jpegsave((directory + "/rotated.jpg").toUtf8().constData());
      auto xyz = vips::VImage::xyz(2053, 1379);
      auto r = xyz[0] % 256, g = xyz[1] % 256, b = (xyz[0] + xyz[1]) % 256;
      auto gradient = r.bandjoin(g)
                          .bandjoin(b)
                          .cast(VIPS_FORMAT_UCHAR)
                          .copy(vips::VImage::option()->set(
                              "interpretation", VIPS_INTERPRETATION_sRGB));
      gradient.jpegsave((directory + "/gradient.jpg").toUtf8().constData());
      gradient.jpegsave((directory + "/progressive.jpg").toUtf8().constData(),
                        vips::VImage::option()->set("interlace", true));
    }
    const auto files = QDir(directory).entryInfoList(
        {"*.jpg", "*.jpeg", "*.JPG", "*.JPEG"}, QDir::Files, QDir::Name);
    check(!files.isEmpty(), "No JPEG files in directory");
    QElapsedTimer initialization;
    initialization.start();
    if (!cpuOnly && !NvjpegBackend::available()) {
      report["initialization_ms"] = initialization.elapsed();
      report["diagnostic"] = NvjpegBackend::diagnostic();
      report["gpu_decodes"] = 0;
      report["success"] = false;
      report["skipped_gpu"] = !required;
      std::cout << QJsonDocument(report).toJson().constData();
      vips_shutdown();
      return required ? 1 : 77;
    }
    report["initialization_ms"] = initialization.elapsed();
    QJsonArray entries;
    auto token = std::make_shared<std::atomic_bool>(false);
    double cpuTotal = 0, gpuTotal = 0;
    int gpuCount = 0;
    for (const auto &file : files) {
      QJsonObject row{{"file", file.fileName()},
                      {"bytes", double(file.size())}};
      qputenv("PRISM_JPEG_BACKEND", "cpu");
      QElapsedTimer timer;
      timer.start();
      QImage reference;
      if (compare) {
        reference = decodeThumbnail(file.absoluteFilePath(), 512, token);
        const auto cpuMs = timer.elapsed();
        cpuTotal += cpuMs;
        row["cpu_ms"] = cpuMs;
      }
      if (!cpuOnly) {
        qputenv("PRISM_JPEG_BACKEND", "nvjpeg");
        const auto before = NvjpegBackend::decoded();
        timer.restart();
        try {
          auto gpu = decodeThumbnail(file.absoluteFilePath(), 512, token);
          const auto elapsed = timer.elapsed();
          check(NvjpegBackend::decoded() == before + 1,
                "GPU test silently used CPU");
          row["nvjpeg_ms"] = elapsed;
          if (compare) {
            check(gpu.size() == reference.size(),
                  "nvJPEG geometry/EXIF mismatch");
            double sum = 0, squared = 0;
            for (int y = 0; y < gpu.height(); ++y)
              for (int x = 0; x < gpu.width(); ++x) {
                const auto a = gpu.pixelColor(x, y),
                           b = reference.pixelColor(x, y);
                for (auto d : {a.red() - b.red(), a.green() - b.green(),
                               a.blue() - b.blue()}) {
                  sum += std::abs(d);
                  squared += d * d;
                }
              }
            const auto samples = double(gpu.width()) * gpu.height() * 3;
            const auto mae = sum / samples, rmse = std::sqrt(squared / samples);
            row["nvjpeg_ms"] = elapsed;
            row["mae"] = mae;
            row["rmse"] = rmse;
            if (synthetic && file.fileName().contains("flat"))
              check(mae < 3, "nvJPEG flat color mismatch");
            if (synthetic)
              check(mae < 5 && rmse < 12, "nvJPEG quality regression");
          }
          gpuTotal += elapsed;
          ++gpuCount;
        } catch (const std::exception &e) {
          row["nvjpeg_error"] = QString::fromUtf8(e.what());
          if (NvjpegBackend::decoded() != before)
            throw;
          if (synthetic &&
              (!file.fileName().contains("progressive") ||
               !QString::fromUtf8(e.what()).contains("not supported")))
            throw;
          if (synthetic) {
            qputenv("PRISM_JPEG_BACKEND", "auto");
            check(decodeThumbnail(file.absoluteFilePath(), 512, token).size() ==
                      reference.size(),
                  "unsupported progressive fallback failed");
            row["cpu_fallback_verified"] = true;
          }
          if (required && !synthetic)
            result = 1;
        }
      }
      entries.append(row);
    }
    report["entries"] = entries;
    report["jpeg_count"] = files.size();
    report["cpu_total_ms"] = cpuTotal;
    report["nvjpeg_total_ms"] = gpuTotal;
    report["gpu_decodes"] = gpuCount;
    report["diagnostic"] = NvjpegBackend::diagnostic();
    if (required && gpuCount == 0)
      result = 1;
    if (synthetic && !cpuOnly) {
      qputenv("PRISM_JPEG_BACKEND", "nvjpeg");
      QThreadPool pool;
      pool.setMaxThreadCount(4);
      const auto before = NvjpegBackend::decoded();
      for (int round = 0; round < 3; ++round) {
        QList<QFuture<QImage>> futures;
        for (int i = 0; i < 8; ++i) {
          const auto name = directory + (i % 2 ? "/gradient.jpg" : "/flat.jpg");
          futures.append(QtConcurrent::run(&pool, [name, token] {
            return decodeThumbnail(name, 512, token);
          }));
        }
        for (auto &future : futures)
          check(!future.result().isNull(), "parallel nvJPEG decode failed");
      }
      check(NvjpegBackend::decoded() == before + 24,
            "parallel GPU work silently used CPU");
      report["parallel_gpu_jobs"] = 24;
      // Cancellation must propagate before touching a GPU or source pixels.
      token->store(true);
      bool cancelled = false;
      try {
        decodeThumbnail(files.first().absoluteFilePath(), 512, token);
      } catch (const std::exception &) {
        cancelled = true;
      }
      check(cancelled, "nvJPEG cancellation ignored");
      token->store(false);
    }
  } catch (const std::exception &e) {
    report["error"] = QString::fromUtf8(e.what());
    result = 1;
  }
  report["success"] = result == 0;
  std::cout
      << QJsonDocument(report).toJson(QJsonDocument::Indented).constData();
  vips_shutdown();
  return result;
}
