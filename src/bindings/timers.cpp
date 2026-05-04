#include "context.hpp"
#include "quickjs.h"

#include <cstdint>

static JSValue js_setTimeout(JSContext* ctx, JSValueConst this_val,
                              int argc, JSValueConst* argv) {
    if (argc < 2 || !JS_IsFunction(ctx, argv[0]))
        return JS_NewInt64(ctx, -1);
    int64_t ms = 0;
    JS_ToInt64(ctx, &ms, argv[1]);
    auto* sctx = SandboxContext::from(ctx);
    return JS_NewInt64(ctx, sctx->scheduleTimer(argv[0], ms, false));
}

static JSValue js_setInterval(JSContext* ctx, JSValueConst this_val,
                               int argc, JSValueConst* argv) {
    if (argc < 2 || !JS_IsFunction(ctx, argv[0]))
        return JS_NewInt64(ctx, -1);
    int64_t ms = 0;
    JS_ToInt64(ctx, &ms, argv[1]);
    auto* sctx = SandboxContext::from(ctx);
    return JS_NewInt64(ctx, sctx->scheduleTimer(argv[0], ms, true));
}

static JSValue js_clearTimeout(JSContext* ctx, JSValueConst this_val,
                                int argc, JSValueConst* argv) {
    if (argc < 1) return JS_UNDEFINED;
    int64_t id = 0;
    JS_ToInt64(ctx, &id, argv[0]);
    SandboxContext::from(ctx)->cancelTimer(id);
    return JS_UNDEFINED;
}

static JSValue js_clearInterval(JSContext* ctx, JSValueConst this_val,
                                 int argc, JSValueConst* argv) {
    return js_clearTimeout(ctx, this_val, argc, argv);
}

void registerTimers(SandboxContext* sctx) {
    JSContext* ctx = sctx->js_ctx();
    JSValue global = JS_GetGlobalObject(ctx);

    JS_SetPropertyStr(ctx, global, "setTimeout",
        JS_NewCFunction(ctx, js_setTimeout, "setTimeout", 2));
    JS_SetPropertyStr(ctx, global, "setInterval",
        JS_NewCFunction(ctx, js_setInterval, "setInterval", 2));
    JS_SetPropertyStr(ctx, global, "clearTimeout",
        JS_NewCFunction(ctx, js_clearTimeout, "clearTimeout", 1));
    JS_SetPropertyStr(ctx, global, "clearInterval",
        JS_NewCFunction(ctx, js_clearInterval, "clearInterval", 1));

    JS_FreeValue(ctx, global);
}
