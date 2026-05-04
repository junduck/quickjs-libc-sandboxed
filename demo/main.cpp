#include <cstdio>
#include <cstring>
#include <thread>

#include "context.hpp"
#include "runtime.hpp"

int main() {
    auto& runtime = SandboxRuntime::instance();
    if (!runtime.initialize({})) {
        fprintf(stderr, "Failed to initialize runtime\n");
        return 1;
    }

    auto ctx_res = runtime.createContext();
    if (!ctx_res) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }
    SandboxContext* ctx = *ctx_res;

    ctx->setupGlobals();

    const char* code = R"JS(
        let count = 0;

        print("demo start");

        setTimeout(() => {
            print("timer A — 10ms");
            count++;
        }, 10);

        const id = setInterval(() => {
            count++;
            print("interval tick — " + count);
            if (count >= 5) {
                clearInterval(id);
                print("interval cleared");
                setTimeout(() => {
                    print("final timer — event loop will exit after this");
                }, 20);
            }
        }, 15);

        print("code evaluated — entering event loop");
    )JS";

    JSValue result = JS_Eval(ctx->js_ctx(), code, std::strlen(code), "<demo>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        JSValue exc = JS_GetException(ctx->js_ctx());
        const char* str = JS_ToCString(ctx->js_ctx(), exc);
        if (str) {
            fprintf(stderr, "Eval error: %s\n", str);
            JS_FreeCString(ctx->js_ctx(), str);
        }
        JS_FreeValue(ctx->js_ctx(), exc);
        runtime.shutdown();
        return 1;
    }
    JS_FreeValue(ctx->js_ctx(), result);

    std::thread loop_thread([ctx]() { ctx->run(); });
    loop_thread.join();

    printf("event loop exited naturally — shutting down\n");

    runtime.shutdown();
    return 0;
}
