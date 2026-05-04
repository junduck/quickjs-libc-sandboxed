#pragma once

#include <cstddef>
#include <cstring>
#include <utility>

namespace qjsb {

// Classic SBO type eraser. Small objects live inline in the byte buffer;
// larger objects fall back to heap allocation. Stores a type-erased
// destructor function pointer for cleanup.

class OpaqueStorage {
  static constexpr size_t kInlineSize = 48;

  alignas(std::max_align_t) std::byte inline_buf_[kInlineSize]{};
  void *heap_ptr_ = nullptr;
  void (*destroy_)(void *) = nullptr;

  bool is_inline() const { return heap_ptr_ == nullptr; }
  void *data_ptr() { return is_inline() ? static_cast<void *>(&inline_buf_) : heap_ptr_; }

public:
  template <typename T, typename... Args>
  T *emplace(Args &&...args) {
    if constexpr (sizeof(T) <= kInlineSize && alignof(T) <= alignof(std::max_align_t)) {
      T *ptr = ::new (&inline_buf_) T(std::forward<Args>(args)...);
      destroy_ = [](void *p) { static_cast<T *>(p)->~T(); };
      return ptr;
    } else {
      auto *ptr = new T(std::forward<Args>(args)...);
      heap_ptr_ = ptr;
      destroy_ = [](void *p) { delete static_cast<T *>(p); };
      return ptr;
    }
  }

  template <typename T>
  T *get() {
    return static_cast<T *>(data_ptr());
  }

  template <typename T>
  const T *get() const {
    return static_cast<const T *>(const_cast<OpaqueStorage *>(this)->data_ptr());
  }

  void *raw() { return destroy_ ? data_ptr() : nullptr; }
  const void *raw() const { return const_cast<OpaqueStorage *>(this)->raw(); }
  bool hasValue() const { return destroy_ != nullptr; }

  ~OpaqueStorage() {
    if (destroy_)
      destroy_(data_ptr());
  }

  OpaqueStorage(const OpaqueStorage &) = delete;
  OpaqueStorage &operator=(const OpaqueStorage &) = delete;

  OpaqueStorage() = default;
  OpaqueStorage(OpaqueStorage &&o) noexcept : heap_ptr_(o.heap_ptr_), destroy_(std::exchange(o.destroy_, nullptr)) {
    if (is_inline()) {
      std::memcpy(inline_buf_, o.inline_buf_, sizeof(inline_buf_));
    }
    o.heap_ptr_ = nullptr;
  }

  OpaqueStorage &operator=(OpaqueStorage &&) = delete;
};

} // namespace qjsb
