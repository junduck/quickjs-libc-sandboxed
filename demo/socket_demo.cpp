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
        const s = new Socket();

        s.on("connect", () => {
            print("connected! writing...");
            s.write("GET / HTTP/1.1\r\nHost: httpbin.org\r\nConnection: close\r\n\r\n");
        });

        s.on("data", (buf) => {
            print("recv: " + buf.byteLength + " bytes");
        });

        s.on("error", (err) => {
            print("error: " + err);
        });

        s.on("close", () => {
            print("closed");
        });

        print("connecting to 54.225.113.51:80...");
        s.connect("54.225.113.51", 80);
        print("connect call returned");

        setTimeout(() => {
            print("timeout - closing socket");
            s.close();
        }, 5000);
    )JS";

    JSValue result = JS_Eval(ctx->js_ctx(), code, std::strlen(code), "<socket-demo>", JS_EVAL_TYPE_GLOBAL);
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

    printf("event loop exited — shutting down\n");
    runtime.shutdown();
    return 0;
}
