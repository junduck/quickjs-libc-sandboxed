#include "promise.hpp"

#include <expected>

auto JsPromise::create(JSContext *ctx) -> std::expected<JsPromise, int> {
  JSValue resolving_funcs[2] = {JS_UNDEFINED, JS_UNDEFINED};
  JSValue p = JS_NewPromiseCapability(ctx, resolving_funcs);
  if (JS_IsException(p)) {
    return std::unexpected(-1);
  }
  return JsPromise(p, resolving_funcs[0], resolving_funcs[1], JS_GetRuntime(ctx));
}

void JsPromise::resolve(JSContext *ctx, JSValue value) {
  if (JS_IsUndefined(resolve_func))
    return;
  JSValue ret = JS_Call(ctx, resolve_func, JS_UNDEFINED, 1, &value);
  if (JS_IsException(ret)) {
    JSValue exc = JS_GetException(ctx);
    JS_FreeValue(ctx, ret);
    JS_FreeValue(ctx, exc);
  }
  JS_FreeValue(ctx, ret);
}

void JsPromise::reject(JSContext *ctx, JSValue reason) {
  if (JS_IsUndefined(reject_func))
    return;
  JSValue ret = JS_Call(ctx, reject_func, JS_UNDEFINED, 1, &reason);
  if (JS_IsException(ret)) {
    JSValue exc = JS_GetException(ctx);
    JS_FreeValue(ctx, ret);
    JS_FreeValue(ctx, exc);
  }
  JS_FreeValue(ctx, ret);
}

JsPromise::~JsPromise() {
  if (rt_) {
    JS_FreeValueRT(rt_, promise);
    JS_FreeValueRT(rt_, resolve_func);
    JS_FreeValueRT(rt_, reject_func);
  }
}
