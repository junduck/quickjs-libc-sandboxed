#pragma once

#include <array>
#include <boost/asio.hpp>
#include <cstring>
#include <cstdio>

#include "context.hpp"
#include "quickjs.h"

struct IOResource {
  virtual ~IOResource() = default;
  virtual void cancel() noexcept = 0;
};

struct SocketState final : IOResource {
  static JSClassID class_id;

  JSContext *ctx = nullptr;
  JSValue obj = JS_UNDEFINED;
  bool finalized_ = false;
  bool closed_ = false;
  bool reading_ = false;
  bool connected_ = false;

  boost::asio::ip::tcp::socket sock;

  JSValue cb_data = JS_UNDEFINED;
  JSValue cb_error = JS_UNDEFINED;
  JSValue cb_close = JS_UNDEFINED;
  JSValue cb_connect = JS_UNDEFINED;

  std::array<uint8_t, 4096> read_buf_;

  SocketState(JSContext *c, boost::asio::io_context &io) : ctx(c), sock(io) {}

  ~SocketState() override {
    boost::system::error_code ec;
    if (sock.is_open()) {
      sock.close(ec);
    }
  }

  void open() {
    if (!sock.is_open()) {
      sock.open(boost::asio::ip::tcp::v4());
    }
  }

  void pin(JSValue this_val) {
    if (JS_IsUndefined(obj)) {
      obj = JS_DupValue(ctx, this_val);
    }
  }

  void unpin() {
    if (!JS_IsUndefined(obj)) {
      JS_FreeValue(ctx, obj);
      obj = JS_UNDEFINED;
    }
  }

  void cancel() noexcept override {
    boost::system::error_code ec;
    sock.cancel(ec);
  }

  void onClosed() {
    closed_ = true;
    if (finalized_) {
      destroy();
    }
  }

  void destroy() {
    if (SandboxContext::from(ctx))
      SandboxContext::from(ctx)->unregisterResource(this);
    delete this;
  }

  static void jsGcMark(JSRuntime *rt, JSValue val, JS_MarkFunc *mf) {
    auto *s = static_cast<SocketState *>(JS_GetOpaque(val, class_id));
    if (!s)
      return;
    JS_MarkValue(rt, s->cb_data, mf);
    JS_MarkValue(rt, s->cb_error, mf);
    JS_MarkValue(rt, s->cb_close, mf);
    JS_MarkValue(rt, s->cb_connect, mf);
  }

  static void jsFinalizer(JSRuntime *rt, JSValue val) {
    auto *s = static_cast<SocketState *>(JS_GetOpaque(val, class_id));
    if (!s)
      return;

    JS_FreeValueRT(rt, s->obj);
    s->obj = JS_UNDEFINED;
    JS_FreeValueRT(rt, s->cb_data);
    s->cb_data = JS_UNDEFINED;
    JS_FreeValueRT(rt, s->cb_error);
    s->cb_error = JS_UNDEFINED;
    JS_FreeValueRT(rt, s->cb_close);
    s->cb_close = JS_UNDEFINED;
    JS_FreeValueRT(rt, s->cb_connect);
    s->cb_connect = JS_UNDEFINED;

    s->finalized_ = true;

    if (s->closed_) {
      s->destroy();
    } else {
      s->cancel();
    }
  }

  void startRead();
  void doRead();
};

void registerSocket(SandboxContext *sctx);
