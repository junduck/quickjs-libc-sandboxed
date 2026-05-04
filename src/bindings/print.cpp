#include "quickjs.h"

#include <cstdio>

#include "context.hpp"

static JSValue js_print(JSContext* ctx, JSValueConst this_val,
                         int argc, JSValueConst* argv) {
    for (int i = 0; i < argc; i++) {
        const char* str = JS_ToCString(ctx, argv[i]);
        if (str) {
            printf("%s%s", (i > 0 ? " " : ""), str);
            JS_FreeCString(ctx, str);
        }
    }
    printf("\n");
    fflush(stdout);
    return JS_UNDEFINED;
}

void registerPrint(SandboxContext* sctx) {
    JSContext* ctx = sctx->js_ctx();
    JSValue global = JS_GetGlobalObject(ctx);

    JS_SetPropertyStr(ctx, global, "print",
        JS_NewCFunction(ctx, js_print, "print", 1));

    JS_FreeValue(ctx, global);
}
