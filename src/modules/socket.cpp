#include "qjsb/modules/socket.hpp"

#include "context.hpp"
#include "quickjs.h"

#include <cstring>

namespace qjsb::modules {

JSClassID TcpSocket::class_id = 0;

TcpSocket::TcpSocket(JSContext *ctx, boost::asio::io_context &io)
    : ctx_(ctx),
      events_(ctx,
              {{"connect"}, {"data"}, {"error"}, {"close"}}),
      transport_(io) {
    transport_.startRead(
        [this](std::span<const uint8_t> data) { handleData(data); },
        [this](std::string_view error) { handleError(error); },
        [this]() { handleClose(); });
}

TcpSocket::~TcpSocket() = default;

std::expected<void, int> TcpSocket::connect(std::string_view host, uint16_t port) {
    pin_.pin(ctx_, this_val_);
    transport_.connect(
        host, port,
        [this]() { handleConnect(); },
        [this](std::string_view error) { handleError(error); });
    return {};
}

std::expected<void, int> TcpSocket::write(std::vector<uint8_t> data) {
    pin_.pin(ctx_, this_val_);
    transport_.write(
        std::move(data),
        [this]() { pin_.unpin(); },
        [this](std::string_view error) { handleError(error); });
    return {};
}

void TcpSocket::close() {
    pin_.forceRelease(JS_GetRuntime(ctx_));
    transport_.cancel();
    transport_.close();
    closed_ = true;
    maybeDestroy();
}

std::expected<void, int> TcpSocket::on(std::string_view event, JSValue callback) {
    auto result = events_.on(event, callback);

    if (event == "data" && events_.hasListener("data")) {
        if (transport_.isConnected()) {
            pin_.pin(ctx_, this_val_);
        }
    }

    return result;
}

void TcpSocket::registerWith(SandboxContext *sctx) {
    sctx_ = sctx;
    sctx_->registerResource(this);
}

void TcpSocket::unregisterFrom(SandboxContext *sctx) {
    if (sctx_) {
        sctx_->unregisterResource(this);
        sctx_ = nullptr;
    }
}

void TcpSocket::cancel() noexcept {
    transport_.cancel();
}

void TcpSocket::forceCleanup(JSContext *ctx) {
    if (!finalized_) {
        events_.finalize(JS_GetRuntime(ctx));
        pin_.forceRelease(JS_GetRuntime(ctx));
        if (!JS_IsUndefined(this_val_)) {
            JS_SetOpaque(this_val_, nullptr);
        }
    }
}

void TcpSocket::handleConnect() {
    JSValue r = events_.emit0("connect");
    JS_FreeValue(ctx_, r);
}

void TcpSocket::handleData(std::span<const uint8_t> data) {
    if (finalized_)
        return;
    JSValue buf = JS_NewArrayBufferCopy(ctx_, data.data(), data.size());
    JSValue r = events_.emit1("data", buf);
    JS_FreeValue(ctx_, r);
    JS_FreeValue(ctx_, buf);
}

void TcpSocket::handleError(std::string_view error) {
    if (finalized_)
        return;
    JSValue err_val = JS_NewStringLen(ctx_, error.data(), error.size());
    JSValue r = events_.emit1("error", err_val);
    JS_FreeValue(ctx_, r);
    JS_FreeValue(ctx_, err_val);
}

void TcpSocket::handleClose() {
    closed_ = true;
    if (pin_.isPinned()) {
        pin_.unpin();
    }
    if (!finalized_) {
        JSValue r = events_.emit0("close");
        JS_FreeValue(ctx_, r);
    }
    maybeDestroy();
}

void TcpSocket::maybeDestroy() {
    if (finalized_ && closed_) {
        unregisterFrom(sctx_);
        delete this;
    }
}

void TcpSocket::jsFinalizer(JSRuntime *rt, JSValue val) {
    auto *self = static_cast<TcpSocket *>(JS_GetOpaque(val, class_id));
    if (!self)
        return;

    self->events_.finalize(rt);
    self->pin_.forceRelease(rt);

    self->finalized_ = true;

    if (self->closed_) {
        self->maybeDestroy();
    } else {
        self->transport_.cancel();
    }
}

void TcpSocket::jsGcMark(JSRuntime *rt, JSValue val, JS_MarkFunc *mf) {
    auto *self = static_cast<TcpSocket *>(JS_GetOpaque(val, class_id));
    if (!self)
        return;
    self->events_.mark(rt, mf);
}

// ─── JS Binding Functions ─────────────────────────────────────

static JSValue js_socket_constructor(JSContext *ctx, JSValueConst new_target,
                                     int argc, JSValueConst *argv, int magic) {
    JSValue proto = JS_GetClassProto(ctx, TcpSocket::class_id);
    if (JS_IsException(proto))
        return proto;

    JSValue obj = JS_NewObjectProtoClass(ctx, proto, TcpSocket::class_id);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(obj))
        return obj;

    auto *sctx = SandboxContext::from(ctx);
    auto *sock = new TcpSocket(ctx, sctx->io_ctx());
    sock->setThisVal(obj);
    JS_SetOpaque(obj, sock);
    sock->registerWith(sctx);

    return obj;
}

static JSValue js_socket_on(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "on(event, handler) requires 2 arguments");

    auto *sock = static_cast<TcpSocket *>(JS_GetOpaque(this_val, TcpSocket::class_id));
    if (!sock)
        return JS_ThrowTypeError(ctx, "not a Socket");

    const char *event = JS_ToCString(ctx, argv[0]);
    if (!event)
        return JS_EXCEPTION;

    auto result = sock->on(event, argv[1]);
    JS_FreeCString(ctx, event);

    if (!result)
        return JS_ThrowTypeError(ctx, "unknown event");
    return JS_UNDEFINED;
}

static JSValue js_socket_close(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv) {
    auto *sock = static_cast<TcpSocket *>(JS_GetOpaque(this_val, TcpSocket::class_id));
    if (!sock)
        return JS_ThrowTypeError(ctx, "not a Socket");
    sock->close();
    return JS_UNDEFINED;
}

static JSValue js_socket_write(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv) {
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "write(data) requires 1 argument");

    auto *sock = static_cast<TcpSocket *>(JS_GetOpaque(this_val, TcpSocket::class_id));
    if (!sock)
        return JS_ThrowTypeError(ctx, "not a Socket");

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
        std::vector<uint8_t> buf(s, s + slen);
        JS_FreeCString(ctx, s);
        auto result = sock->write(std::move(buf));
        if (!result)
            return JS_ThrowTypeError(ctx, "write failed");
        return JS_NewInt64(ctx, static_cast<int64_t>(len > 0 ? len : slen));
    } else {
        return JS_ThrowTypeError(ctx, "write() requires ArrayBuffer or string");
    }

    std::vector<uint8_t> buf(data, data + len);
    auto result = sock->write(std::move(buf));
    if (!result)
        return JS_ThrowTypeError(ctx, "write failed");
    return JS_NewInt64(ctx, static_cast<int64_t>(len));
}

static JSValue js_socket_connect(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
    if (argc < 2 || !JS_IsString(argv[0]) || !JS_IsNumber(argv[1]))
        return JS_ThrowTypeError(ctx, "connect(host, port) requires string and number");

    auto *sock = static_cast<TcpSocket *>(JS_GetOpaque(this_val, TcpSocket::class_id));
    if (!sock)
        return JS_ThrowTypeError(ctx, "not a Socket");

    size_t host_len = 0;
    const char *host = JS_ToCStringLen(ctx, &host_len, argv[0]);
    if (!host)
        return JS_EXCEPTION;

    int64_t port = 0;
    JS_ToInt64(ctx, &port, argv[1]);

    auto result = sock->connect(std::string_view(host, host_len), static_cast<uint16_t>(port));
    JS_FreeCString(ctx, host);

    if (!result)
        return JS_ThrowTypeError(ctx, "connect failed");
    return JS_UNDEFINED;
}

// ─── Module Registration ──────────────────────────────────────

static const JSCFunctionListEntry socket_proto_funcs[] = {
    JS_CFUNC_DEF("on", 2, js_socket_on),
    JS_CFUNC_DEF("close", 0, js_socket_close),
    JS_CFUNC_DEF("write", 1, js_socket_write),
    JS_CFUNC_DEF("connect", 2, js_socket_connect),
};

void SocketModule::registerWith(SandboxContext *sctx) {
    JSContext *ctx = sctx->js_ctx();
    JSRuntime *rt = JS_GetRuntime(ctx);

    TcpSocket::class_id = JS_NewClassID(&TcpSocket::class_id);

    JSClassDef cls = {};
    cls.class_name = "Socket";
    cls.finalizer = &TcpSocket::jsFinalizer;
    cls.gc_mark = &TcpSocket::jsGcMark;

    JS_NewClass(rt, TcpSocket::class_id, &cls);

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, socket_proto_funcs,
                               sizeof(socket_proto_funcs) / sizeof(socket_proto_funcs[0]));
    JS_SetClassProto(ctx, TcpSocket::class_id, proto);

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue ctor = JS_NewCFunctionMagic(ctx, js_socket_constructor, "Socket", 0,
                                        JS_CFUNC_constructor_magic, 0);
    JS_SetConstructor(ctx, ctor, proto);
    JS_SetPropertyStr(ctx, global, "Socket", ctor);
    JS_FreeValue(ctx, global);
}

} // namespace qjsb::modules
