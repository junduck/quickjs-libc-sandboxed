#include "context.hpp"

#include <algorithm>
#include <cstdio>

static void dumpJsError(JSContext *ctx) {
  JSValue exc = JS_GetException(ctx);
  if (!JS_IsUndefined(exc)) {
    const char *str = JS_ToCString(ctx, exc);
    if (str) {
      fprintf(stderr, "Unhandled JS error: %s\n", str);
      JS_FreeCString(ctx, str);
    }
    JS_FreeValue(ctx, exc);
  }
}

SandboxContext *SandboxContext::from(JSContext *ctx) { return static_cast<SandboxContext *>(JS_GetContextOpaque(ctx)); }

SandboxContext::SandboxContext(JSContext *ctx, Config cfg)
    : js_ctx_(ctx), work_guard_(boost::asio::make_work_guard(io_ctx_)), config_(std::move(cfg)), stopping_(false) {
  JS_SetContextOpaque(ctx, this);
  JS_SetHostPromiseRejectionTracker(JS_GetRuntime(ctx), promiseRejectionTracker, this);
  primordials_.cache(ctx);
}

SandboxContext::~SandboxContext() {
  if (js_ctx_) {
    JS_SetHostPromiseRejectionTracker(JS_GetRuntime(js_ctx_), nullptr, nullptr);
    primordials_.free(js_ctx_);
    JS_FreeContext(js_ctx_);
  }
}

void SandboxContext::drainMicrotasks() {
  JSRuntime *rt = JS_GetRuntime(js_ctx_);
  for (;;) {
    int err = JS_ExecutePendingJob(rt, nullptr);
    if (err <= 0) {
      if (err < 0) {
        dumpJsError(js_ctx_);
      }
      break;
    }
  }
}

auto SandboxContext::checkUnhandledRejections() -> bool {
  if (state_.pending_rejections.empty())
    return false;

  for (auto &rp : state_.pending_rejections) {
    dumpJsError(js_ctx_);
    JS_FreeValue(js_ctx_, rp.promise);
    JS_FreeValue(js_ctx_, rp.reason);
  }
  state_.pending_rejections.clear();
  return true;
}

void SandboxContext::promiseRejectionTracker(JSContext *ctx, JSValue promise, JSValue reason, JS_BOOL is_handled, void *opaque) {
  auto *self = static_cast<SandboxContext *>(opaque);
  if (is_handled) {
    auto &v = self->state_.pending_rejections;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](const ContextState::RejectedPromise &rp) { return JS_VALUE_GET_PTR(rp.promise) == JS_VALUE_GET_PTR(promise); }),
            v.end());
  } else {
    self->state_.pending_rejections.push_back({JS_DupValue(ctx, promise), JS_DupValue(ctx, reason)});
  }
}

void SandboxContext::run() {
  for (;;) {
    drainMicrotasks();

    if (checkUnhandledRejections())
      break;

    auto n = io_ctx_.run_one();
    if (n == 0)
      break;
  }
}

void SandboxContext::stop() {
  stopping_.store(true);
  boost::asio::post(io_ctx_, []() {});
  work_guard_.reset();
}

void SandboxContext::join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}
