#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

#include "quickjs.h"

#include "qjsb/cancelable.hpp"
#include "qjsb/event_emitter.hpp"
#include "qjsb/pin_guard.hpp"
#include "qjsb/transport/tcp.hpp"

class SandboxContext;

namespace qjsb {

namespace modules {

class TcpSocket final : public qjsb::IOResource {
public:
    static constexpr std::string_view class_name = "Socket";
    static JSClassID class_id;

    static void jsFinalizer(JSRuntime *rt, JSValue val);
    static void jsGcMark(JSRuntime *rt, JSValue val, JS_MarkFunc *mf);

    TcpSocket(JSContext *ctx, boost::asio::io_context &io);
    ~TcpSocket() override;

    std::expected<void, int> connect(std::string_view host, uint16_t port);
    std::expected<void, int> write(std::vector<uint8_t> data);
    void close();
    std::expected<void, int> on(std::string_view event, JSValue callback);

    void setThisVal(JSValue val) { this_val_ = val; }
    JSValue thisVal() const { return this_val_; }

    void registerWith(SandboxContext *sctx);
    void unregisterFrom(SandboxContext *sctx);

    void cancel() noexcept override;
    void forceCleanup(JSContext *ctx) override;

    bool isFinalized() const { return finalized_; }
    bool isClosed() const { return closed_; }

private:
    JSContext *ctx_;
    SandboxContext *sctx_ = nullptr;
    JSValue this_val_ = JS_UNDEFINED;
    PinGuard pin_;
    EventEmitter events_;
    transport::TcpTransport transport_;

    bool finalized_ = false;
    bool closed_ = false;

    void handleConnect();
    void handleData(std::span<const uint8_t> data);
    void handleError(std::string_view error);
    void handleClose();

    void maybeDestroy();
};

class SocketModule {
public:
    static void registerWith(SandboxContext *sctx);
};

} // namespace modules
} // namespace qjsb
