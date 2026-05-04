#pragma once

#include <stdexcept>

#include "quickjs.h"

namespace qjsb {
struct JsError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

enum class PromiseState : int {
  NotAPromise = -1, // JS_PromiseState returns -1 to indicate error when not a promise
  Pending = JS_PROMISE_PENDING,
  Fulfilled = JS_PROMISE_FULFILLED,
  Rejected = JS_PROMISE_REJECTED,
};

// Lightweight wrapper around QuickJS's tri-state int returns:
//   < 0  → exception
//   0    → false / failure
//   > 0  → true  / success
class JsResult {
  int v_;

public:
  JsResult(int v) noexcept : v_(v) {}
  int raw() const { return v_; }
  explicit operator bool() const { return v_ > 0; }
  bool isException() const { return v_ < 0; }
};
} // namespace qjsb
