#ifndef ONEFLOW_CORE_COMPILER_OF2XLA_MEMORY_MEMORY_POOL_H_
#define ONEFLOW_CORE_COMPILER_OF2XLA_MEMORY_MEMORY_POOL_H_

#include <mutex>
#include <functional>
#include "glog/logging.h"

#include "tensorflow/stream_executor/stream.h"
#include "tensorflow/stream_executor/platform.h"

namespace oneflow {
namespace mola {

namespace se = tensorflow::se;

class DeviceMemoryPool {
 public:
  static DeviceMemoryPool *NewMemoryPool(const se::Platform *platform,
                                         se::Stream *stream,
                                         int device_ordinal);

  int device_ordinal() const { return device_ordinal_; }
  size_t capacity() const { return capacity_; }

  void Reserve(size_t size);
  void Release();

  void IncreaseRef() const;
  void DecreaseRef() const;

  virtual void *AllocateRaw(size_t offset, size_t size) {
    CHECK_LE(offset + size, capacity_);
    return reinterpret_cast<void *>(mem_buffer_ + offset);
  }

  typedef std::function<DeviceMemoryPool *(se::Stream *, int)> MemPoolFactory;

 protected:
  DeviceMemoryPool() = default;
  explicit DeviceMemoryPool(se::Stream *stream, int device_ordinal)
      : mem_buffer_(nullptr), capacity_(0), stream_(stream),
        device_ordinal_(device_ordinal) {}

  virtual ~DeviceMemoryPool() {}

  virtual void ReserveImpl(size_t size) = 0;
  virtual void ReleaseImpl() = 0;

  template <typename Derived>
  friend class MemoryPoolRegistarr;

  static void RegisterFactory(const se::Platform::Id &platform_id,
                              MemPoolFactory factory);

  mutable std::mutex mutex_;

  mutable std::condition_variable cond_;

  uint8_t *mem_buffer_;

  size_t capacity_;

  se::Stream *stream_;

  int device_ordinal_;

  mutable volatile uint64_t reference_count_ = 0;

  // Limited size for buffer size. If limit is not requared, then set it -1
  int64_t limited_memory_size_ = -1;
};

template <typename Derived>
class MemoryPoolRegistarr {
 public:
  MemoryPoolRegistarr(const se::Platform::Id &platform_id) {
    auto factory = [](se::Stream *stream, int device_ordinal) -> Derived* {
      return new Derived(stream, device_ordinal);
    };
    DeviceMemoryPool::RegisterFactory(platform_id, factory);
  }
};

#define REGISTER_XLA_MEMORY_POOL(PlatformId, Derived)            \
  static MemoryPoolRegistarr<Derived>                            \
      _device_memory_pool_##Derived##_ __attribute__((unused)) = \
      MemoryPoolRegistarr<Derived>(PlatformId)

}  // namespace mola
}  // namespace oneflow

#endif  // ONEFLOW_CORE_COMPILER_OF2XLA_MEMORY_MEMORY_POOL_H_
