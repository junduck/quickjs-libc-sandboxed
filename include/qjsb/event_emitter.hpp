#pragma once

#include <cassert>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "quickjs.h"

namespace qjsb {

class EventEmitter {
public:
    struct Slot {
        std::string_view name;
    };

    EventEmitter(JSContext *ctx, std::initializer_list<Slot> slots) : ctx_(ctx) {
        slots_.reserve(slots.size());
        for (auto &s : slots) {
            name_to_index_[s.name] = slots_.size();
            slots_.push_back(JS_UNDEFINED);
        }
    }

    EventEmitter(const EventEmitter &) = delete;
    EventEmitter &operator=(const EventEmitter &) = delete;

    ~EventEmitter() = default;

    std::expected<void, int> on(std::string_view event, JSValue callback) {
        auto it = name_to_index_.find(event);
        if (it == name_to_index_.end())
            return std::unexpected(-1);
        auto &slot = slots_[it->second];
        if (!JS_IsUndefined(slot))
            JS_FreeValue(ctx_, slot);
        slot = JS_DupValue(ctx_, callback);
        return {};
    }

    void off(std::string_view event) {
        auto it = name_to_index_.find(event);
        if (it == name_to_index_.end())
            return;
        auto &slot = slots_[it->second];
        if (!JS_IsUndefined(slot)) {
            JS_FreeValue(ctx_, slot);
            slot = JS_UNDEFINED;
        }
    }

    JSValue emit(std::string_view event, std::span<JSValueConst> args) {
        auto it = name_to_index_.find(event);
        if (it == name_to_index_.end())
            return JS_UNDEFINED;
        auto &slot = slots_[it->second];
        if (JS_IsUndefined(slot))
            return JS_UNDEFINED;
        auto argv = const_cast<JSValueConst *>(args.data());
        return JS_Call(ctx_, slot, JS_UNDEFINED, static_cast<int>(args.size()), argv);
    }

    JSValue emit0(std::string_view event) {
        return emit(event, {});
    }

    JSValue emit1(std::string_view event, JSValueConst arg) {
        return emit(event, {&arg, 1});
    }

    void mark(JSRuntime *rt, JS_MarkFunc *mf) const {
        for (auto &slot : slots_) {
            if (!JS_IsUndefined(slot))
                JS_MarkValue(rt, slot, mf);
        }
    }

    void finalize(JSRuntime *rt) {
        for (auto &slot : slots_) {
            if (!JS_IsUndefined(slot)) {
                JS_FreeValueRT(rt, slot);
                slot = JS_UNDEFINED;
            }
        }
    }

    bool hasListener(std::string_view event) const {
        auto it = name_to_index_.find(event);
        if (it == name_to_index_.end())
            return false;
        return !JS_IsUndefined(slots_[it->second]);
    }

private:
    JSContext *ctx_;
    std::vector<JSValue> slots_;
    std::unordered_map<std::string_view, size_t> name_to_index_;
};

} // namespace qjsb
