#pragma once

#include <chrono>
#include <memory>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/asio.hpp>

#include "quickjs.h"

#include "qjsb/cancelable.hpp"

struct ContextState {
  // --- Timer Heap ---
  //
  // Classic timer heap: a min-heap of (expiry, id) pairs plus an
  // unordered_map for entry storage. A single asio::steady_timer tracks
  // the nearest deadline.

  struct TimerEntry {
    int64_t id;
    JSValue func;
    std::chrono::steady_clock::time_point expiry;
    bool repeating;
    int64_t interval_ms;
    bool cancelled = false;
  };

  using TimePoint = std::chrono::steady_clock::time_point;

  // Min-heap of (expiry, id) — only lightweight data, no JSValues
  struct HeapEntry {
    TimePoint expiry;
    int64_t id;
    bool operator<(const HeapEntry &other) const { return expiry > other.expiry; }
  };

  std::priority_queue<HeapEntry> timer_heap;
  std::unordered_map<int64_t, TimerEntry> timer_entries;
  std::unique_ptr<boost::asio::steady_timer> next_timer;
  int64_t next_timer_id = 1;

  int64_t allocTimerId() { return next_timer_id++; }

  // --- Rejected Promises ---
  struct RejectedPromise {
    JSValue promise;
    JSValue reason;
  };
  std::vector<RejectedPromise> pending_rejections;

  // --- I/O Resources (sockets, etc.) ---
  //
  // Holds raw pointers to I/O resource structs (SocketState, etc.).
  // These are NOT owned by ContextState — they are owned by the JSClassDef
  // finalizer + deferred free pattern (finalized_/closed_ handshake).
  // ContextState just holds the registry so ~SandboxContext can cancel
  // all active I/O on shutdown.

  std::unordered_set<qjsb::IOResource*> active_resources;
};
