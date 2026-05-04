#pragma once

#include <expected>
#include <memory>
#include <vector>

#include "context.hpp"

class SandboxRuntime {
public:
  struct Config {
    size_t memory_limit = 0;
    size_t gc_threshold = 256 * 1024;
    size_t max_stack_size = 0;
  };

  static SandboxRuntime &instance();

  SandboxRuntime(const SandboxRuntime &) = delete;
  SandboxRuntime &operator=(const SandboxRuntime &) = delete;

  auto initialize(Config cfg) -> std::expected<void, int>;
  void shutdown();

  auto createContext(SandboxContext::Config ctx_cfg = {}) -> std::expected<SandboxContext *, int>;

  JSRuntime *js_runtime() const { return rt_; }

private:
  SandboxRuntime() = default;
  ~SandboxRuntime();

  JSRuntime *rt_ = nullptr;
  Config config_;
  std::vector<std::unique_ptr<SandboxContext>> contexts_;
};
