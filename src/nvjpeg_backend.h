#pragma once
#include <QImage>
#include <QString>
#include <atomic>
#include <memory>
#include <vips/vips8>
namespace NvjpegBackend {
vips::VImage thumbnail(const QString &, const vips::VImage &, int,
                       const std::shared_ptr<std::atomic_bool> &);
QString diagnostic();
bool available();
// Terminal cleanup only: every decoder/thumbnail worker must be joined first.
void shutdown();
quint64 decoded();
quint64 fallbacks();
} // namespace NvjpegBackend
