#include <gtest/gtest.h>

#include "value.hpp"

using namespace qjsb;

static JSValue fn_ignore(JSContext* ctx, JSValueConst this_val,
                          int argc, JSValueConst* argv) {
    return JS_UNDEFINED;
}

TEST(CallMinimal, Test1_NewCFunction) {
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);

    JSValue f = JS_NewCFunction(ctx, fn_ignore, "f", 0);
    EXPECT_FALSE(JS_IsException(f));
    JS_FreeValue(ctx, f);

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}

TEST(CallMinimal, Test2_RawCall) {
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);

    JSValue f = JS_NewCFunction(ctx, fn_ignore, "f", 0);
    JSValue r = JS_Call(ctx, f, JS_UNDEFINED, 0, nullptr);
    EXPECT_FALSE(JS_IsException(r));
    JS_FreeValue(ctx, r);
    JS_FreeValue(ctx, f);

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}

TEST(CallMinimal, Test3_ValueWrap) {
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);

    {
        Value f(ctx, JS_NewCFunction(ctx, fn_ignore, "f", 0));
        EXPECT_TRUE(f.isFunction());
    }

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}

TEST(CallMinimal, Test4_ValueCall) {
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);

    {
        Value f(ctx, JS_NewCFunction(ctx, fn_ignore, "f", 0));
        Value r = f.call();
        EXPECT_TRUE(r.isUndefined());
    }

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}
