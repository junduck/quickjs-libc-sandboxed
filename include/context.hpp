#pragma once

#include <atomic>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>

#include "context_state.hpp"
#include "primordials.hpp"

struct IOResource;

struct JsError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

class SandboxContext {
public:
  struct Config {
    std::vector<std::string> argv;
    std::unordered_map<std::string, std::string> env;
  };

  static SandboxContext *from(JSContext *ctx);

  void setupGlobals();
  void run();
  void stop();
  void join();

  // --- Timer API (called by JS bindings) ---
  int64_t scheduleTimer(JSValue func, int64_t ms, bool repeating);
  void cancelTimer(int64_t id);

  // --- I/O Resource registry ---
  void registerResource(IOResource *r);
  void unregisterResource(IOResource *r);

  // --- Error helpers ---
  void assertOk(JSValue v);
  [[noreturn]] void throwJsError();

  JSContext *js_ctx() const { return js_ctx_; }
  boost::asio::io_context &io_ctx() { return io_ctx_; }
  const Config &config() const { return config_; }
  Primordials &primordials() { return primordials_; }

  SandboxContext(JSContext *ctx, Config cfg);
  ~SandboxContext();

private:
  void drainMicrotasks();
  void checkUnhandledRejections();
  void processExpiredTimers();
  void armNextTimer();
  void reportFatal(const JsError &e);

  static std::string jsValueToString(JSContext *ctx, JSValue v);

  static void promiseRejectionTracker(JSContext *ctx, JSValueConst promise, JSValueConst reason, JS_BOOL is_handled, void *opaque);

  JSContext *js_ctx_ = nullptr;
  boost::asio::io_context io_ctx_;
  Config config_;
  ContextState state_;
  Primordials primordials_;
  std::thread thread_;
  std::atomic<bool> stopping_{false};
};
