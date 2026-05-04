#pragma once

#include <expected>

#include "quickjs.h"

struct JsPromise {
  JSValue promise;
  JSValue resolve_func;
  JSValue reject_func;

  static auto create(JSContext *ctx) -> std::expected<JsPromise, int>;

  void resolve(JSContext *ctx, JSValue value);
  void reject(JSContext *ctx, JSValue reason);

  JsPromise() : promise(JS_UNDEFINED), resolve_func(JS_UNDEFINED), reject_func(JS_UNDEFINED) {}

  ~JsPromise();

  JsPromise(const JsPromise &) = delete;
  JsPromise &operator=(const JsPromise &) = delete;

  JsPromise(JsPromise &&other) noexcept : promise(other.promise), resolve_func(other.resolve_func), reject_func(other.reject_func), rt_(other.rt_) {
    other.promise = JS_UNDEFINED;
    other.resolve_func = JS_UNDEFINED;
    other.reject_func = JS_UNDEFINED;
  }

  JsPromise &operator=(JsPromise &&other) noexcept {
    if (this != &other) {
      promise = other.promise;
      resolve_func = other.resolve_func;
      reject_func = other.reject_func;
      rt_ = other.rt_;
      other.promise = JS_UNDEFINED;
      other.resolve_func = JS_UNDEFINED;
      other.reject_func = JS_UNDEFINED;
    }
    return *this;
  }

private:
  JSRuntime *rt_ = nullptr;

  JsPromise(JSValue p, JSValue res, JSValue rej, JSRuntime *rt) : promise(p), resolve_func(res), reject_func(rej), rt_(rt) {}
};
