// NVIDIA nvJPEG is linked dynamically, never redistributed in our bundles.
// Its vendor notices are preserved in NVIDIA-NVJPEG-NOTICE.txt.
#include "nvjpeg_abi.h"
#include "nvjpeg_resize.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cuda_runtime.h>
#include <memory>
#include <mutex>
#include <nvjpeg.h>
#include <stdexcept>
#include <string>
#include <unordered_map>
namespace {
constexpr size_t MiB = 1024 * 1024;
struct Failure : std::runtime_error {
  int code;
  Failure(int c, const std::string &s) : runtime_error(s), code(c) {}
};
void cu(cudaError_t result, const char *where) {
  if (result != cudaSuccess)
    throw Failure(3, std::string(where) + ": " + cudaGetErrorString(result));
}
void nv(nvjpegStatus_t result, const char *where) {
  if (result != NVJPEG_STATUS_SUCCESS)
    throw Failure(result == NVJPEG_STATUS_JPEG_NOT_SUPPORTED ? 2 : 3,
                  std::string(where) + ": nvJPEG status " +
                      std::to_string(result));
}
void text(char *dest, size_t cap, const char *s) {
  if (dest && cap)
    std::snprintf(dest, cap, "%s", s);
}
struct Budget {
  std::mutex mutex;
  int deviceIndex = 0;
  size_t limit = 0, used = 0, pinned = 0;
  std::unordered_map<void *, size_t> device, host;
  int alloc(void **p, size_t n, bool pin) noexcept {
    try {
      std::lock_guard lock(mutex);
      *p = nullptr;
      if (cudaSetDevice(deviceIndex) != cudaSuccess)
        return 1;
      auto &amount = pin ? pinned : used;
      const auto ceiling = pin ? 512 * MiB : limit;
      if (!n || n > ceiling || amount > ceiling - n)
        return 1;
      auto result =
          pin ? cudaHostAlloc(p, n, cudaHostAllocDefault) : cudaMalloc(p, n);
      if (result != cudaSuccess) {
        *p = nullptr;
        return 1;
      }
      try {
        (pin ? host : device).emplace(*p, n);
      } catch (...) {
        if (pin)
          cudaFreeHost(*p);
        else
          cudaFree(*p);
        *p = nullptr;
        return 1;
      }
      amount += n;
      return 0;
    } catch (...) {
      return 1;
    }
  }
  int free(void *p, bool pin) noexcept {
    try {
      if (!p)
        return 0;
      std::lock_guard lock(mutex);
      if (cudaSetDevice(deviceIndex) != cudaSuccess)
        return 1;
      auto &map = pin ? host : device;
      auto it = map.find(p);
      if (it == map.end())
        return 1;
      auto result = pin ? cudaFreeHost(p) : cudaFree(p);
      if (result != cudaSuccess)
        return 1;
      (pin ? pinned : used) -= it->second;
      map.erase(it);
      return 0;
    } catch (...) {
      return 1;
    }
  }
};
struct Buffer {
  Budget *budget = nullptr;
  void *data = nullptr;
  size_t capacity = 0;
  bool pinned = false;
  Buffer(Budget &b, bool pin = false) : budget(&b), pinned(pin) {}
  ~Buffer() { budget->free(data, pinned); }
  void reserve(size_t n) {
    if (n <= capacity)
      return;
    // Reuse buffers; round up modestly without doubling hundreds of MB.
    n = (n + MiB - 1) / MiB * MiB;
    budget->free(data, pinned);
    data = nullptr;
    capacity = 0;
    if (budget->alloc(&data, n, pinned))
      throw Failure(3, "nvJPEG: memory budget/allocation failed");
    capacity = n;
  }
};
struct Session {
  cudaStream_t stream = nullptr;
  cudaEvent_t done = nullptr;
  nvjpegHandle_t handle = nullptr;
  nvjpegJpegDecoder_t decoder = nullptr;
  nvjpegJpegState_t state = nullptr;
  nvjpegJpegStream_t jpeg = nullptr;
  nvjpegDecodeParams_t params = nullptr;
  nvjpegBufferPinned_t pinned = nullptr;
  nvjpegBufferDevice_t device = nullptr;
  nvjpegDevAllocatorV2_t da{};
  nvjpegPinnedAllocatorV2_t pa{};
  Buffer rgb, temp, xw, yw, xs, ys, result, hostResult;
  explicit Session(Budget &b)
      : rgb(b), temp(b), xw(b), yw(b), xs(b), ys(b), result(b),
        hostResult(b, true) {}
  void init(Budget &budget) {
    int least = 0, greatest = 0;
    cu(cudaDeviceGetStreamPriorityRange(&least, &greatest),
       "cudaDeviceGetStreamPriorityRange");
    cu(cudaStreamCreateWithPriority(&stream, cudaStreamNonBlocking, least),
       "cudaStreamCreate");
    cu(cudaEventCreateWithFlags(&done,
                                cudaEventDisableTiming | cudaEventBlockingSync),
       "cudaEventCreate");
    da = {+[](void *p, void **d, size_t n, cudaStream_t) {
            return static_cast<Budget *>(p)->alloc(d, n, false);
          },
          +[](void *p, void *d, size_t, cudaStream_t) {
            return static_cast<Budget *>(p)->free(d, false);
          },
          &budget};
    pa = {+[](void *p, void **d, size_t n, cudaStream_t) {
            return static_cast<Budget *>(p)->alloc(d, n, true);
          },
          +[](void *p, void *d, size_t, cudaStream_t) {
            return static_cast<Budget *>(p)->free(d, true);
          },
          &budget};
    unsigned flags = NVJPEG_FLAGS_BITSTREAM_STRICT;
#ifdef NVJPEG_FLAGS_REDUCED_MEMORY_DECODE
    flags |= NVJPEG_FLAGS_REDUCED_MEMORY_DECODE;
#endif
#ifdef NVJPEG_FLAGS_UPSAMPLING_WITH_INTERPOLATION
    flags |= NVJPEG_FLAGS_UPSAMPLING_WITH_INTERPOLATION;
#endif
    nv(nvjpegCreateExV2(NVJPEG_BACKEND_GPU_HYBRID, &da, &pa, flags, &handle),
       "nvjpegCreateExV2");
    nv(nvjpegDecoderCreate(handle, NVJPEG_BACKEND_GPU_HYBRID, &decoder),
       "nvjpegDecoderCreate");
    nv(nvjpegDecoderStateCreate(handle, decoder, &state),
       "nvjpegDecoderStateCreate");
    nv(nvjpegJpegStreamCreate(handle, &jpeg), "nvjpegJpegStreamCreate");
    nv(nvjpegDecodeParamsCreate(handle, &params), "nvjpegDecodeParamsCreate");
    nv(nvjpegDecodeParamsSetOutputFormat(params, NVJPEG_OUTPUT_RGBI),
       "nvjpegDecodeParamsSetOutputFormat");
    nv(nvjpegBufferPinnedCreateV2(handle, &pa, &pinned),
       "nvjpegBufferPinnedCreateV2");
    nv(nvjpegBufferDeviceCreateV2(handle, &da, &device),
       "nvjpegBufferDeviceCreateV2");
    nv(nvjpegStateAttachPinnedBuffer(state, pinned),
       "nvjpegStateAttachPinnedBuffer");
    nv(nvjpegStateAttachDeviceBuffer(state, device),
       "nvjpegStateAttachDeviceBuffer");
  }
  ~Session() {
    if (stream)
      cudaStreamSynchronize(stream);
    if (state)
      nvjpegJpegStateDestroy(state);
    if (pinned)
      nvjpegBufferPinnedDestroy(pinned);
    if (device)
      nvjpegBufferDeviceDestroy(device);
    if (params)
      nvjpegDecodeParamsDestroy(params);
    if (jpeg)
      nvjpegJpegStreamDestroy(jpeg);
    if (decoder)
      nvjpegDecoderDestroy(decoder);
    if (handle)
      nvjpegDestroy(handle);
    if (done)
      cudaEventDestroy(done);
    if (stream)
      cudaStreamDestroy(stream);
  }
};
struct Context {
  int device = 0;
  Budget budget;
  std::mutex mutex;
  std::condition_variable changed;
  std::array<bool, 2> used{false, false};
  // Budget outlives both sessions and all allocator callbacks.
  std::array<std::unique_ptr<Session>, 2> sessions;
  ~Context() {
    cudaSetDevice(device);
    sessions = {};
  }
};
struct Lease {
  Context &context;
  size_t index;
  Lease(Context &c, int (*cancel)(void *), void *arg) : context(c) {
    std::unique_lock lock(c.mutex);
    for (;;) {
      if (cancel && cancel(arg))
        throw Failure(4, "nvJPEG: cancelled while waiting");
      for (index = 0; index < c.used.size(); ++index)
        if (!c.used[index]) {
          c.used[index] = true;
          return;
        }
      c.changed.wait_for(lock, std::chrono::milliseconds(10));
    }
  }
  ~Lease() {
    std::lock_guard lock(context.mutex);
    context.used[index] = false;
    context.changed.notify_one();
  }
};
void *create(char *message, size_t cap) {
  try {
    int count = 0;
    cu(cudaGetDeviceCount(&count), "cudaGetDeviceCount");
    if (!count)
      throw Failure(1, "nvJPEG: no CUDA device");
    int choice = 0;
    if (const char *value = std::getenv("PRISM_CUDA_DEVICE")) {
      char *end = nullptr;
      long parsed = std::strtol(value, &end, 10);
      if (!*value || !end || *end || parsed < 0 || parsed >= count)
        throw Failure(1, "PRISM_CUDA_DEVICE: invalid index");
      choice = int(parsed);
    }
    cudaDeviceProp props{};
    cu(cudaGetDeviceProperties(&props, choice), "cudaGetDeviceProperties");
    if (props.major * 10 + props.minor < 75)
      throw Failure(1, "nvJPEG module requires CUDA capability >=7.5");
    cu(cudaSetDevice(choice), "cudaSetDevice");
    size_t free = 0, total = 0;
    cu(cudaMemGetInfo(&free, &total), "cudaMemGetInfo");
    if (free < 512 * MiB)
      throw Failure(1, "nvJPEG: insufficient free VRAM");
    auto context = std::make_unique<Context>();
    context->device = choice;
    context->budget.deviceIndex = choice;
    context->budget.limit = std::min<size_t>(3ULL * 1024 * MiB, free / 3 * 2);
    std::string description = "nvJPEG GPU_HYBRID + CUDA Lanczos3: ";
    description += props.name;
    text(message, cap, description.c_str());
    return context.release();
  } catch (const std::exception &e) {
    text(message, cap, e.what());
    return nullptr;
  } catch (...) {
    text(message, cap, "nvJPEG: initialization failed");
    return nullptr;
  }
}
void destroy(void *p) { delete static_cast<Context *>(p); }
int decode(void *value, const unsigned char *bytes, size_t length, int iw,
           int ih, unsigned char *out, int ow, int oh, int (*cancel)(void *),
           void *arg, char *error, size_t cap) {
  if (!value || !bytes || length < 2 || length > 512 * MiB || !out || iw < 1 ||
      ih < 1 || iw > 65535 || ih > 65535 || uint64_t(iw) * ih > 250000000 ||
      ow < 1 || oh < 1 || ow > 512 || oh > 512 || ow > iw || oh > ih) {
    text(error, cap, "nvJPEG: invalid input bounds");
    return 2;
  }
  auto &context = *static_cast<Context *>(value);
  try {
    Lease lease(context, cancel, arg);
    cu(cudaSetDevice(context.device), "cudaSetDevice");
    auto &slot = context.sessions[lease.index];
    try {
      if (!slot) {
        slot = std::make_unique<Session>(context.budget);
        slot->init(context.budget);
      }
      auto &s = *slot;
      auto stopped = [&] {
        if (cancel && cancel(arg))
          throw Failure(4, "nvJPEG: cancelled");
      };
      stopped();
      nv(nvjpegJpegStreamParse(s.handle, bytes, length, 0, 0, s.jpeg),
         "nvjpegJpegStreamParse");
      unsigned w = 0, h = 0;
      nv(nvjpegJpegStreamGetFrameDimensions(s.jpeg, &w, &h),
         "nvjpegJpegStreamGetFrameDimensions");
      if (w != unsigned(iw) || h != unsigned(ih))
        throw Failure(2, "nvJPEG: metadata dimension mismatch");
      unsigned components = 0;
      nv(nvjpegJpegStreamGetComponentsNum(s.jpeg, &components),
         "nvjpegJpegStreamGetComponentsNum");
      if (components != 1 && components != 3)
        throw Failure(
            2, "nvJPEG: CMYK/extra channels use color-managed CPU fallback");
      int supported = -1;
      nv(nvjpegDecoderJpegSupported(s.decoder, s.jpeg, s.params, &supported),
         "nvjpegDecoderJpegSupported");
      if (supported)
        throw Failure(2, "nvJPEG GPU_HYBRID: JPEG encoding not supported");
      const size_t pitch = (size_t(iw) * 3 + 127) / 128 * 128;
      const int xt = int(std::ceil(6. * double(iw) / ow)) + 3,
                yt = int(std::ceil(6. * double(ih) / oh)) + 3;
      s.rgb.reserve(pitch * ih);
      s.temp.reserve(size_t(ow) * ih * sizeof(float4));
      s.xw.reserve(size_t(ow) * xt * sizeof(float));
      s.yw.reserve(size_t(oh) * yt * sizeof(float));
      s.xs.reserve(size_t(ow) * sizeof(int));
      s.ys.reserve(size_t(oh) * sizeof(int));
      s.result.reserve(size_t(ow) * oh * 3);
      s.hostResult.reserve(size_t(ow) * oh * 3);
      stopped();
      nv(nvjpegDecodeJpegHost(s.handle, s.decoder, s.state, s.params, s.jpeg),
         "nvjpegDecodeJpegHost");
      stopped();
      nv(nvjpegDecodeJpegTransferToDevice(s.handle, s.decoder, s.state, s.jpeg,
                                          s.stream),
         "nvjpegDecodeJpegTransferToDevice");
      nvjpegImage_t destination{};
      destination.channel[0] = static_cast<unsigned char *>(s.rgb.data);
      destination.pitch[0] = pitch;
      nv(nvjpegDecodeJpegDevice(s.handle, s.decoder, s.state, &destination,
                                s.stream),
         "nvjpegDecodeJpegDevice");
      cu(prismResizeRgb(
             destination.channel[0], pitch, iw, ih,
             static_cast<float4 *>(s.temp.data),
             static_cast<float *>(s.xw.data), static_cast<float *>(s.yw.data),
             static_cast<int *>(s.xs.data), static_cast<int *>(s.ys.data),
             static_cast<unsigned char *>(s.result.data), ow, oh, xt, yt,
             s.stream),
         "CUDA Lanczos3");
      cu(cudaMemcpyAsync(s.hostResult.data, s.result.data, size_t(ow) * oh * 3,
                         cudaMemcpyDeviceToHost, s.stream),
         "cudaMemcpyAsync thumbnail");
      cu(cudaEventRecord(s.done, s.stream), "cudaEventRecord");
      cu(cudaEventSynchronize(s.done), "cudaEventSynchronize");
      stopped();
      std::memcpy(out, s.hostResult.data, size_t(ow) * oh * 3);
      return 0;
    } catch (...) {
      slot.reset();
      throw;
    } // reset failed decoder only after draining its stream
  } catch (const Failure &e) {
    text(error, cap, e.what());
    return e.code;
  } catch (const std::exception &e) {
    text(error, cap, e.what());
    return 3;
  } catch (...) {
    text(error, cap, "nvJPEG: unexpected decode failure");
    return 3;
  }
}
const PrismNvjpegApi api{1, &create, &destroy, &decode};
} // namespace
extern "C" __attribute__((visibility("default"))) const PrismNvjpegApi *
prism_nvjpeg_api() {
  return &api;
}
