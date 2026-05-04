#pragma once

#include <string>
#include <string_view>

#include "quickjs.h"

namespace qjsb {
class CString {
  JSContext *ctx_ = nullptr;
  char const *ptr_ = nullptr;
  size_t len_ = 0;

public:
  CString() = default;

  CString(JSContext *ctx, char const *ptr, size_t len) : ctx_(ctx), ptr_(ptr), len_(ptr ? len : 0) {}

  CString(CString &&o) noexcept : ctx_(o.ctx_), ptr_(o.ptr_), len_(o.len_) {
    o.ctx_ = nullptr;
    o.ptr_ = nullptr;
    o.len_ = 0;
  }

  CString &operator=(CString &&o) noexcept {
    if (this != &o) {
      if (ctx_ && ptr_)
        JS_FreeCString(ctx_, ptr_);
      ctx_ = o.ctx_;
      ptr_ = o.ptr_;
      len_ = o.len_;
      o.ctx_ = nullptr;
      o.ptr_ = nullptr;
      o.len_ = 0;
    }
    return *this;
  }

  CString(const CString &) = delete;
  CString &operator=(const CString &) = delete;

  ~CString() {
    if (ctx_ && ptr_)
      JS_FreeCString(ctx_, ptr_);
  }

  const char *data() const { return ptr_; }
  const char *c_str() const { return ptr_; }
  size_t size() const { return len_; }
  bool empty() const { return len_ == 0; }

  operator std::string_view() const { return ptr_ ? std::string_view{ptr_, len_} : std::string_view{}; }

  std::string str() const { return ptr_ ? std::string{ptr_, len_} : std::string{}; }

  explicit operator bool() const { return ptr_ != nullptr; }
};
} // namespace qjsb
