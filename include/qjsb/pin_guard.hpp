#pragma once

#include <cassert>

#include "quickjs.h"

namespace qjsb {

class PinGuard {
public:
    PinGuard() = default;
    PinGuard(const PinGuard &) = delete;
    PinGuard &operator=(const PinGuard &) = delete;

    ~PinGuard() = default;

    void pin(JSContext *ctx, JSValue val) {
        if (count_ == 0) {
            ctx_ = ctx;
            obj_ = JS_DupValue(ctx, val);
        }
        count_++;
    }

    void unpin() {
        assert(count_ > 0);
        count_--;
        if (count_ == 0 && ctx_) {
            JS_FreeValue(ctx_, obj_);
            obj_ = JS_UNDEFINED;
        }
    }

    void forceRelease(JSRuntime *rt) {
        if (count_ > 0) {
            JS_FreeValueRT(rt, obj_);
            obj_ = JS_UNDEFINED;
            count_ = 0;
            ctx_ = nullptr;
        }
    }

    bool isPinned() const { return count_ > 0; }
    int count() const { return count_; }
    JSValue pinnedObject() const { return obj_; }

private:
    JSContext *ctx_ = nullptr;
    JSValue obj_ = JS_UNDEFINED;
    int count_ = 0;
};

} // namespace qjsb
