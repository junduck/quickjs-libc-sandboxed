#include "context.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

using namespace std::chrono;

void registerTimers(SandboxContext* sctx);
void registerPrint(SandboxContext* sctx);

// ─── Standard error path ───────────────────────────────────────────

void SandboxContext::reportError() {
    JSValue exc = JS_GetException(js_ctx_);
    const char* str = JS_ToCString(js_ctx_, exc);
    if (str) {
        fprintf(stderr, "Unhandled JS error: %s\n", str);
        JS_FreeCString(js_ctx_, str);
    }
    JS_FreeValue(js_ctx_, exc);
}

// ─── Construction / destruction ────────────────────────────────────

SandboxContext *SandboxContext::from(JSContext *ctx) {
    return static_cast<SandboxContext *>(JS_GetContextOpaque(ctx));
}

SandboxContext::SandboxContext(JSContext *ctx, Config cfg)
    : js_ctx_(ctx), config_(std::move(cfg)), stopping_(false) {
  JS_SetContextOpaque(ctx, this);
  JS_SetHostPromiseRejectionTracker(JS_GetRuntime(ctx), promiseRejectionTracker, this);
  primordials_.cache(ctx);
}

SandboxContext::~SandboxContext() {
  if (js_ctx_) {
    JS_SetHostPromiseRejectionTracker(JS_GetRuntime(js_ctx_), nullptr, nullptr);
    primordials_.free(js_ctx_);

    state_.next_timer.reset();

    for (auto& [id, entry] : state_.timer_entries) {
      JS_FreeValue(js_ctx_, entry.func);
    }
    state_.timer_entries.clear();

    for (auto& rp : state_.pending_rejections) {
      JS_FreeValue(js_ctx_, rp.promise);
      JS_FreeValue(js_ctx_, rp.reason);
    }
    state_.pending_rejections.clear();

    JS_RunGC(JS_GetRuntime(js_ctx_));
    JS_FreeContext(js_ctx_);
  }
}

// ─── Timer API ─────────────────────────────────────────────────────
//
// Ownership:
//   SandboxContext owns the timer heap, entries, asio timer, and all
//   JSValue lifecycle. JS bindings are thin suppliers that call in.
//
// Data flow:
//   JS → scheduleTimer → pushes to heap → armNextTimer (single asio timer)
//   asio fires → processExpiredTimers → JS_Call each → repeat or cleanup
//
// Error path:
//   Any JS_Call exception → reportError (same path as drainMicrotasks)

int64_t SandboxContext::scheduleTimer(JSValue func, int64_t ms, bool repeating) {
    int64_t id = state_.allocTimerId();

    ContextState::TimerEntry entry;
    entry.id = id;
    entry.func = JS_DupValue(js_ctx_, func);
    entry.expiry = steady_clock::now() + milliseconds(ms);
    entry.repeating = repeating;
    entry.interval_ms = ms;

    state_.timer_entries[id] = std::move(entry);
    state_.timer_heap.push({entry.expiry, id});

    if (state_.timer_heap.top().id == id) {
        armNextTimer();
    }

    return id;
}

void SandboxContext::cancelTimer(int64_t id) {
    auto it = state_.timer_entries.find(id);
    if (it != state_.timer_entries.end()) {
        it->second.cancelled = true;
    }
}

void SandboxContext::armNextTimer() {
    // Skip cancelled / stale entries at the top
    while (!state_.timer_heap.empty()) {
        auto top = state_.timer_heap.top();
        auto it = state_.timer_entries.find(top.id);
        if (it == state_.timer_entries.end() || it->second.cancelled) {
            state_.timer_heap.pop();
        } else {
            break;
        }
    }

    if (state_.timer_heap.empty()) {
        state_.next_timer.reset();
        return;
    }

    auto top = state_.timer_heap.top();
    auto& entry = state_.timer_entries[top.id];

    if (!state_.next_timer) {
        state_.next_timer = std::make_unique<boost::asio::steady_timer>(io_ctx_);
    }

    state_.next_timer->expires_at(entry.expiry);
    state_.next_timer->async_wait([this](const boost::system::error_code& ec) {
        if (ec) return;
        processExpiredTimers();
        armNextTimer();
    });
}

void SandboxContext::processExpiredTimers() {
    auto now = steady_clock::now();

    while (!state_.timer_heap.empty()) {
        auto top = state_.timer_heap.top();
        auto entry_it = state_.timer_entries.find(top.id);
        if (entry_it == state_.timer_entries.end()) {
            state_.timer_heap.pop();
            continue;
        }

        auto& entry = entry_it->second;
        if (entry.expiry > now) break;

        state_.timer_heap.pop();

        if (entry.cancelled) {
            JS_FreeValue(js_ctx_, entry.func);
            state_.timer_entries.erase(entry_it);
            continue;
        }

        JSValue ret = JS_Call(js_ctx_, entry.func, JS_UNDEFINED, 0, nullptr);
        if (JS_IsException(ret)) {
            reportError();
        }
        JS_FreeValue(js_ctx_, ret);

        // Re-check — callback may have called clearTimeout / clearInterval
        if (entry.cancelled) {
            JS_FreeValue(js_ctx_, entry.func);
            state_.timer_entries.erase(entry_it);
            continue;
        }

        if (entry.repeating) {
            entry.expiry += milliseconds(entry.interval_ms);
            if (entry.expiry <= now) {
                entry.expiry = now + milliseconds(entry.interval_ms);
            }
            state_.timer_heap.push({entry.expiry, entry.id});
        } else {
            JS_FreeValue(js_ctx_, entry.func);
            state_.timer_entries.erase(entry_it);
        }
    }
}

// ─── Event loop ────────────────────────────────────────────────────

void SandboxContext::drainMicrotasks() {
  JSRuntime *rt = JS_GetRuntime(js_ctx_);
  for (;;) {
    int err = JS_ExecutePendingJob(rt, nullptr);
    if (err <= 0) {
      if (err < 0) {
        reportError();
      }
      break;
    }
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
  if (state_.next_timer) {
    state_.next_timer->cancel();
  }
  boost::asio::post(io_ctx_, []() {});
}

void SandboxContext::join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

// ─── Promise rejection tracker ─────────────────────────────────────

auto SandboxContext::checkUnhandledRejections() -> bool {
  if (state_.pending_rejections.empty())
    return false;

  for (auto &rp : state_.pending_rejections) {
    reportError();
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

// ─── Global registration ───────────────────────────────────────────

void SandboxContext::setupGlobals() {
  registerTimers(this);
  registerPrint(this);
}
