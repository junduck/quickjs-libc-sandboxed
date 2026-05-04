#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>

#include "context_state.hpp"
#include "primordials.hpp"

class SandboxContext {
public:
  struct Config {
    std::vector<std::string> argv;
    std::unordered_map<std::string, std::string> env;
  };

  static SandboxContext *from(JSContext *ctx);

  void run();
  void stop();
  void join();

  JSContext *js_ctx() const { return js_ctx_; }
  boost::asio::io_context &io_ctx() { return io_ctx_; }
  const Config &config() const { return config_; }
  ContextState &state() { return state_; }
  Primordials &primordials() { return primordials_; }

  SandboxContext(JSContext *ctx, Config cfg);
  ~SandboxContext();

private:
  void drainMicrotasks();
  auto checkUnhandledRejections() -> bool;

  static void promiseRejectionTracker(JSContext *ctx, JSValue promise, JSValue reason, JS_BOOL is_handled, void *opaque);

  JSContext *js_ctx_ = nullptr;
  boost::asio::io_context io_ctx_;
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
  Config config_;
  ContextState state_;
  Primordials primordials_;
  std::thread thread_;
  std::atomic<bool> stopping_{false};
};
