#include "runtime.hpp"

#include <expected>

SandboxRuntime &SandboxRuntime::instance() {
  static SandboxRuntime inst;
  return inst;
}

auto SandboxRuntime::initialize(Config cfg) -> std::expected<void, int> {
  if (rt_)
    return std::unexpected(-1);

  config_ = cfg;
  rt_ = JS_NewRuntime();

  if (!rt_)
    return std::unexpected(-1);

  if (config_.memory_limit > 0) {
    JS_SetMemoryLimit(rt_, config_.memory_limit);
  }
  if (config_.max_stack_size > 0) {
    JS_SetMaxStackSize(rt_, config_.max_stack_size);
  }

  JS_SetRuntimeOpaque(rt_, this);

  return {};
}

void SandboxRuntime::shutdown() {
  contexts_.clear();

  if (rt_) {
    JS_RunGC(rt_);
    JS_FreeRuntime(rt_);
    rt_ = nullptr;
  }
}

auto SandboxRuntime::createContext(SandboxContext::Config ctx_cfg) -> std::expected<SandboxContext *, int> {
  if (!rt_)
    return std::unexpected(-1);

  JSContext *js_ctx = JS_NewContext(rt_);
  if (!js_ctx)
    return std::unexpected(-1);

  auto ctx = std::make_unique<SandboxContext>(js_ctx, std::move(ctx_cfg));
  auto *ptr = ctx.get();
  contexts_.push_back(std::move(ctx));
  return ptr;
}

SandboxRuntime::~SandboxRuntime() { shutdown(); }
