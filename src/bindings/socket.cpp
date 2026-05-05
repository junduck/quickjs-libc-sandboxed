#include "io_resource.hpp"

#include <boost/asio.hpp>
#include <cstdlib>
#include <cstring>

JSClassID SocketState::class_id = 0;

static JSValue js_socket_constructor(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
  auto *sctx = SandboxContext::from(ctx);
  auto *state = new SocketState(ctx, sctx->io_ctx());
  JS_SetOpaque(this_val, state);
  sctx->registerResource(state);
  return this_val;
}

static JSValue js_socket_constructor2(JSContext *ctx, JSValueConst new_target,
                                        int argc, JSValueConst *argv, int magic) {
  JSValue proto = JS_GetClassProto(ctx, SocketState::class_id);
  if (JS_IsException(proto))
    return proto;
  JSValue obj = JS_NewObjectProtoClass(ctx, proto, SocketState::class_id);
  JS_FreeValue(ctx, proto);
  if (JS_IsException(obj))
    return obj;
  auto *sctx = SandboxContext::from(ctx);
  auto *state = new SocketState(ctx, sctx->io_ctx());
  JS_SetOpaque(obj, state);
  sctx->registerResource(state);
  return obj;
}

static JSValue js_socket_on(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv) {
  if (argc < 2)
    return JS_ThrowTypeError(ctx, "on(event, handler) requires 2 arguments");

  auto *state = static_cast<SocketState *>(JS_GetOpaque(this_val, SocketState::class_id));
  if (!state)
    return JS_ThrowTypeError(ctx, "not a Socket");

  const char *event = JS_ToCString(ctx, argv[0]);
  if (!event)
    return JS_EXCEPTION;

  JSValue *slot = nullptr;
  if (strcmp(event, "data") == 0) {
    slot = &state->cb_data;
  } else if (strcmp(event, "error") == 0) {
    slot = &state->cb_error;
  } else if (strcmp(event, "close") == 0) {
    slot = &state->cb_close;
  } else if (strcmp(event, "connect") == 0) {
    slot = &state->cb_connect;
  }

  JS_FreeCString(ctx, event);

  if (!slot)
    return JS_ThrowTypeError(ctx, "unknown event");

  if (!JS_IsUndefined(*slot)) {
    JS_FreeValue(ctx, *slot);
  }
  *slot = JS_DupValue(ctx, argv[1]);

  if (!JS_IsUndefined(state->cb_data) && !state->reading_) {
    state->reading_ = true;
    state->startRead();
  }

  return JS_UNDEFINED;
}

static JSValue js_socket_close(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv) {
  auto *state = static_cast<SocketState *>(JS_GetOpaque(this_val, SocketState::class_id));
  if (!state)
    return JS_ThrowTypeError(ctx, "not a Socket");

  state->unpin();
  state->cancel();

  return JS_UNDEFINED;
}

static JSValue js_socket_write(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv) {
  auto *state = static_cast<SocketState *>(JS_GetOpaque(this_val, SocketState::class_id));
  if (!state)
    return JS_ThrowTypeError(ctx, "not a Socket");

  if (argc < 1)
    return JS_ThrowTypeError(ctx, "write(data) requires 1 argument");

  size_t len = 0;
  uint8_t *data = nullptr;

  if (JS_IsArray(ctx, argv[0])) {
    data = JS_GetArrayBuffer(ctx, &len, argv[0]);
    if (!data && len == 0)
      return JS_ThrowTypeError(ctx, "write() requires ArrayBuffer or string");
  } else if (JS_IsString(argv[0])) {
    size_t slen = 0;
    const char *s = JS_ToCStringLen(ctx, &slen, argv[0]);
    if (!s)
      return JS_EXCEPTION;
    data = reinterpret_cast<uint8_t *>(const_cast<char *>(s));
    len = slen;
  } else {
    return JS_ThrowTypeError(ctx, "write() requires ArrayBuffer or string");
  }

  JSValue this_dup = JS_DupValue(ctx, this_val);
  state->pin(this_val);
  boost::asio::async_write(
      state->sock, boost::asio::buffer(data, len),
      [ctx, state, this_dup](const boost::system::error_code &ec, size_t n) {
        JS_FreeValue(ctx, this_dup);
        state->unpin();
        if (ec) {
          if (!JS_IsUndefined(state->cb_error)) {
            JSValue err = JS_NewString(ctx, ec.message().c_str());
            JSValue r = JS_Call(ctx, state->cb_error, JS_UNDEFINED, 1, &err);
            JS_FreeValue(ctx, err);
            JS_FreeValue(ctx, r);
          }
        }
      });

  return JS_NewInt64(ctx, static_cast<int64_t>(len));
}

static JSValue js_socket_connect(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv) {
  auto *state = static_cast<SocketState *>(JS_GetOpaque(this_val, SocketState::class_id));
  if (!state)
    return JS_ThrowTypeError(ctx, "not a Socket");

  if (argc < 2 || !JS_IsString(argv[0]) || !JS_IsNumber(argv[1]))
    return JS_ThrowTypeError(ctx, "connect(host, port) requires string and number");

  size_t host_len = 0;
  const char *host = JS_ToCStringLen(ctx, &host_len, argv[0]);
  if (!host)
    return JS_EXCEPTION;

  int64_t port = 0;
  JS_ToInt64(ctx, &port, argv[1]);

  state->open();
  state->pin(this_val);

  boost::asio::ip::tcp::endpoint ep(boost::asio::ip::make_address(host), static_cast<uint16_t>(port));
  JS_FreeCString(ctx, host);

  state->sock.async_connect(
      ep, [state](const boost::system::error_code &ec) {
        if (ec) {
          state->unpin();
          if (!JS_IsUndefined(state->cb_error)) {
            JSValue err = JS_NewString(state->ctx, ec.message().c_str());
            JSValue r = JS_Call(state->ctx, state->cb_error, JS_UNDEFINED, 1, &err);
            JS_FreeValue(state->ctx, err);
            JS_FreeValue(state->ctx, r);
          }
          state->onClosed();
          return;
        }

        state->connected_ = true;

        if (!JS_IsUndefined(state->cb_connect)) {
          JSValue r = JS_Call(state->ctx, state->cb_connect, JS_UNDEFINED, 0, nullptr);
          JS_FreeValue(state->ctx, r);
        }

        if (!JS_IsUndefined(state->cb_data) && state->reading_) {
          state->pin(state->obj);
          state->startRead();
        }
      });

  return JS_UNDEFINED;
}

void SocketState::startRead() {
  if (finalized_ || closed_)
    return;
  if (!connected_)
    return;
  doRead();
}

void SocketState::doRead() {
  if (finalized_ || closed_)
    return;

  sock.async_read_some(
      boost::asio::buffer(read_buf_),
      [this](const boost::system::error_code &ec, size_t n) {
        if (ec == boost::asio::error::operation_aborted) {
          reading_ = false;
          onClosed();
          return;
        }

        if (ec) {
          reading_ = false;
          if (!JS_IsUndefined(cb_error)) {
            JSValue err = JS_NewString(ctx, ec.message().c_str());
            JSValue r = JS_Call(ctx, cb_error, JS_UNDEFINED, 1, &err);
            JS_FreeValue(ctx, err);
            JS_FreeValue(ctx, r);
          }
          onClosed();
          return;
        }

        if (!JS_IsUndefined(cb_data) && !finalized_) {
          JSValue buf = JS_NewArrayBufferCopy(ctx, read_buf_.data(), n);
          if (!JS_IsException(buf)) {
            JSValue r = JS_Call(ctx, cb_data, JS_UNDEFINED, 1, &buf);
            JS_FreeValue(ctx, r);
            JS_FreeValue(ctx, buf);
          }
        }

        if (!finalized_ && !closed_) {
          doRead();
        }
      });
}

static const JSCFunctionListEntry socket_proto_funcs[] = {
    JS_CFUNC_DEF("on", 2, js_socket_on),
    JS_CFUNC_DEF("close", 0, js_socket_close),
    JS_CFUNC_DEF("write", 1, js_socket_write),
    JS_CFUNC_DEF("connect", 2, js_socket_connect),
};

void registerSocket(SandboxContext *sctx) {
  JSContext *ctx = sctx->js_ctx();
  JSRuntime *rt = JS_GetRuntime(ctx);

  SocketState::class_id = JS_NewClassID(&SocketState::class_id);

  JSClassDef cls = {};
  cls.class_name = "Socket";
  cls.finalizer = [](JSRuntime *rt, JSValue val) { SocketState::jsFinalizer(rt, val); };
  cls.gc_mark = [](JSRuntime *rt, JSValue val, JS_MarkFunc *mf) { SocketState::jsGcMark(rt, val, mf); };

  JS_NewClass(rt, SocketState::class_id, &cls);

  JSValue proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, proto, socket_proto_funcs,
                             sizeof(socket_proto_funcs) / sizeof(socket_proto_funcs[0]));
  JS_SetClassProto(ctx, SocketState::class_id, proto);

  JSValue global = JS_GetGlobalObject(ctx);
  JSValue ctor =
      JS_NewCFunctionMagic(ctx, js_socket_constructor2, "Socket", 0, JS_CFUNC_constructor_magic, 0);
  JS_SetConstructor(ctx, ctor, proto);
  JS_SetPropertyStr(ctx, global, "Socket", ctor);
  JS_FreeValue(ctx, global);
}
