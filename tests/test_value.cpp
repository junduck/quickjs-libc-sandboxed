#include <gtest/gtest.h>

#include <expected>
#include <string>
#include <vector>

#include "atom.hpp"
#include "common.hpp"
#include "cstring.hpp"
#include "value.hpp"

using namespace qjsb;

struct ValueTest : ::testing::Test {
    JSRuntime* rt = nullptr;
    JSContext* ctx = nullptr;

    void SetUp() override {
        rt = JS_NewRuntime();
        ASSERT_NE(rt, nullptr);
        ctx = JS_NewContext(rt);
        ASSERT_NE(ctx, nullptr);
    }

    void TearDown() override {
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
    }
};

// ── JsResult ───────────────────────────────────────────────────────

TEST(JsResultTest, TriState) {
    EXPECT_FALSE(static_cast<bool>(JsResult(-1)));
    EXPECT_TRUE(JsResult(-1).isException());
    EXPECT_FALSE(static_cast<bool>(JsResult(0)));
    EXPECT_FALSE(JsResult(0).isException());
    EXPECT_TRUE(static_cast<bool>(JsResult(1)));
    EXPECT_FALSE(JsResult(1).isException());
    EXPECT_EQ(JsResult(42).raw(), 42);
}

// ── CString ────────────────────────────────────────────────────────

TEST_F(ValueTest, CStringFromValue) {
    Value s(ctx, JS_NewString(ctx, "hello"));
    auto cs = s.toString();
    ASSERT_TRUE(cs.has_value());
    EXPECT_TRUE(cs->str() == "hello");
    EXPECT_TRUE(std::string_view(*cs) == "hello");
    EXPECT_EQ(cs->size(), 5u);
    EXPECT_FALSE(cs->empty());
}

TEST_F(ValueTest, CStringFromEmptyValue) {
    Value s(ctx, JS_NewString(ctx, ""));
    auto cs = s.toString();
    ASSERT_TRUE(cs.has_value());
    EXPECT_TRUE(cs->str() == "");
    EXPECT_TRUE(cs->empty());
}

// ── Atom ───────────────────────────────────────────────────────────

TEST_F(ValueTest, ToAtomFromString) {
    Value s(ctx, JS_NewString(ctx, "test"));
    auto a = s.toAtom();
    ASSERT_TRUE(a.has_value());
    EXPECT_TRUE(*a);
    EXPECT_TRUE(a->to_cstring().str() == "test");
}

TEST_F(ValueTest, ToAtomOnNonString) {
    Value n(ctx, JS_NewInt32(ctx, 42));
    auto a = n.toAtom();
    ASSERT_TRUE(a.has_value());
}

TEST(AtomTest, DefaultConstructedIsFalse) {
    Atom a;
    EXPECT_FALSE(a);
}

TEST_F(ValueTest, AtomComparison) {
    Atom a1(ctx, "foo");
    Atom a2(ctx, "foo");  // same string → same atom (QuickJS atomizes)
    Atom a3(ctx, "bar");

    EXPECT_TRUE(a1);
    EXPECT_TRUE(a2);

    // Same string → same atom index
    EXPECT_EQ(static_cast<JSAtom>(a1), static_cast<JSAtom>(a2));
    // Different string → different atom index
    EXPECT_NE(static_cast<JSAtom>(a1), static_cast<JSAtom>(a3));

    Value v1(ctx, JS_NewString(ctx, "foo"));
    auto ta1 = v1.toAtom();
    ASSERT_TRUE(ta1.has_value());
    EXPECT_EQ(static_cast<JSAtom>(*ta1), static_cast<JSAtom>(a1));
}

// ── Value type checks ──────────────────────────────────────────────

TEST_F(ValueTest, Undefined) {
    Value v(ctx, JS_UNDEFINED);
    EXPECT_TRUE(v.isUndefined());
    EXPECT_FALSE(v.isNull());
    EXPECT_FALSE(v.isObject());
}

TEST_F(ValueTest, Null) {
    Value v(ctx, JS_NULL);
    EXPECT_TRUE(v.isNull());
    EXPECT_FALSE(v.isUndefined());
}

TEST_F(ValueTest, BoolChecks) {
    Value t(ctx, JS_TRUE);
    EXPECT_TRUE(t.isBool());
    EXPECT_FALSE(t.isNumber());
    auto b = t.toBool();
    ASSERT_TRUE(b.has_value());
    EXPECT_TRUE(*b);

    Value f(ctx, JS_FALSE);
    auto bf = f.toBool();
    ASSERT_TRUE(bf.has_value());
    EXPECT_FALSE(*bf);
}

TEST_F(ValueTest, NumberChecks) {
    Value n(ctx, JS_NewInt32(ctx, 42));
    EXPECT_TRUE(n.isNumber());
    auto i32 = n.toInt32();
    ASSERT_TRUE(i32.has_value());
    EXPECT_EQ(*i32, 42);
    auto i64 = n.toInt64();
    ASSERT_TRUE(i64.has_value());
    EXPECT_EQ(*i64, 42);
}

TEST_F(ValueTest, FloatConversion) {
    Value n(ctx, JS_NewFloat64(ctx, 3.14));
    auto f = n.toFloat64();
    ASSERT_TRUE(f.has_value());
    EXPECT_NEAR(*f, 3.14, 0.001);
}

TEST_F(ValueTest, StringCheck) {
    Value s(ctx, JS_NewString(ctx, "abc"));
    EXPECT_TRUE(s.isString());
    EXPECT_FALSE(s.isNumber());
}

TEST_F(ValueTest, ObjectCheck) {
    Value obj(ctx, JS_NewObject(ctx));
    EXPECT_TRUE(obj.isObject());
    EXPECT_FALSE(obj.isFunction());
    EXPECT_EQ(obj.getClassID(), 1u); // JS_CLASS_OBJECT
}

TEST_F(ValueTest, TypeErrorIsException) {
    Value err(ctx, JS_ThrowTypeError(ctx, "test error"));
    EXPECT_TRUE(err.isException());
    EXPECT_THROW(err.assertOk(), JsError);
}

// ── Type conversions ──────────────────────────────────────────────

TEST_F(ValueTest, ToBoolOnNonBool) {
    Value n(ctx, JS_NewInt32(ctx, 1));
    auto b = n.toBool();
    ASSERT_TRUE(b.has_value());
    EXPECT_TRUE(*b);

    Value z(ctx, JS_NewInt32(ctx, 0));
    auto bz = z.toBool();
    ASSERT_TRUE(bz.has_value());
    EXPECT_FALSE(*bz);
}

TEST_F(ValueTest, ToInt32CoercesString) {
    Value s(ctx, JS_NewString(ctx, "abc"));
    auto n = s.toInt32();
    // JS_ToInt32 coerces non-numeric strings to 0 (no exception)
    ASSERT_TRUE(n.has_value());
    EXPECT_EQ(*n, 0);
}

TEST_F(ValueTest, ToIndex) {
    Value n(ctx, JS_NewInt32(ctx, 42));
    auto idx = n.toIndex();
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 42u);
}

TEST_F(ValueTest, ToIndexNegativeFails) {
    Value n(ctx, JS_NewInt32(ctx, -1));
    EXPECT_FALSE(n.toIndex().has_value());
}

// ── Comparison ─────────────────────────────────────────────────────

TEST_F(ValueTest, StrictEq) {
    Value a(ctx, JS_NewInt32(ctx, 42));
    Value b(ctx, JS_NewInt32(ctx, 42));
    Value c(ctx, JS_NewInt32(ctx, 99));
    EXPECT_TRUE(a.isStrictEq(b));
    EXPECT_FALSE(a.isStrictEq(c));
    EXPECT_FALSE(a.isStrictEq(Value(ctx, JS_NewString(ctx, "42"))));
}

TEST_F(ValueTest, SameValue) {
    Value a(ctx, JS_NewFloat64(ctx, 0.0));
    Value b(ctx, JS_NewFloat64(ctx, -0.0));
    EXPECT_FALSE(a.isSameValue(b));
}

// ── Property get / set ─────────────────────────────────────────────

TEST_F(ValueTest, GetSetPropertyStr) {
    Value obj(ctx, JS_NewObject(ctx));
    auto r = obj.setProperty("x", Value(ctx, JS_NewInt32(ctx, 10)));
    EXPECT_FALSE(r.isException());

    Value x = obj.getProperty("x");
    EXPECT_TRUE(x.isNumber());
    auto n = x.toInt32();
    ASSERT_TRUE(n.has_value());
    EXPECT_EQ(*n, 10);

    EXPECT_TRUE(static_cast<bool>(obj.hasProperty("x")));
    EXPECT_FALSE(static_cast<bool>(obj.hasProperty("y")));
}

TEST_F(ValueTest, GetSetPropertyUint32) {
    Value obj(ctx, JS_NewObject(ctx));
    obj.setProperty(0u, Value(ctx, JS_NewString(ctx, "first")));
    obj.setProperty(1u, Value(ctx, JS_NewString(ctx, "second")));

    Value v0 = obj.getProperty(0u);
    EXPECT_TRUE(v0.toString()->str() == "first");
    Value v1 = obj.getProperty(1u);
    EXPECT_TRUE(v1.toString()->str() == "second");
}

TEST_F(ValueTest, DeleteProperty) {
    Value obj(ctx, JS_NewObject(ctx));
    obj.setProperty("x", Value(ctx, JS_NewInt32(ctx, 1)));
    EXPECT_TRUE(static_cast<bool>(obj.hasProperty("x")));
    obj.deleteProperty(Atom(ctx, "x"), 0);
    EXPECT_FALSE(static_cast<bool>(obj.hasProperty("x")));
}

// ── Prototype ─────────────────────────────────────────────────────

TEST_F(ValueTest, SetAndGetPrototype) {
    Value proto(ctx, JS_NewObject(ctx));
    proto.setProperty("inherited", Value(ctx, JS_NewInt32(ctx, 123)));
    Value obj(ctx, JS_NewObject(ctx));
    obj.setPrototype(proto);
    Value v = obj.getProperty("inherited");
    EXPECT_TRUE(v.isNumber());
}

// ── Function calls ────────────────────────────────────────────────

static JSValue fn_double(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NewInt32(ctx, 0);
    int32_t n = 0;
    JS_ToInt32(ctx, &n, argv[0]);
    return JS_NewInt32(ctx, n * 2);
}

static JSValue fn_sum(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    int32_t total = 0;
    for (int i = 0; i < argc; i++) {
        int32_t n = 0;
        JS_ToInt32(ctx, &n, argv[i]);
        total += n;
    }
    return JS_NewInt32(ctx, total);
}

static JSValue fn_identity(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc == 0) return JS_UNDEFINED;
    return JS_DupValue(ctx, argv[0]);
}

TEST_F(ValueTest, CallZeroArgs) {
    Value fn(ctx, JS_NewCFunction(ctx, fn_identity, "identity", 0));
    EXPECT_TRUE(fn.call().isUndefined());
}

TEST_F(ValueTest, CallOneArg) {
    Value fn(ctx, JS_NewCFunction(ctx, fn_double, "double", 1));
    Value arg(ctx, JS_NewInt32(ctx, 21));
    Value result = fn.call(arg);
    EXPECT_TRUE(result.isNumber());
    EXPECT_EQ(*result.toInt32(), 42);
}

TEST_F(ValueTest, CallMethodWithThis) {
    Value obj(ctx, JS_NewObject(ctx));
    Value fn(ctx, JS_NewCFunction(ctx, fn_identity, "identity", 1));
    obj.setProperty("id", fn);

    Value method = obj.getProperty("id");
    Value arg(ctx, JS_NewString(ctx, "test"));
    Value result = method.callMethod(obj, arg);
    EXPECT_TRUE(result.isString());
    EXPECT_TRUE(result.toString()->str() == "test");
}

TEST_F(ValueTest, CallWithRange) {
    Value fn(ctx, JS_NewCFunction(ctx, fn_sum, "sum", 0));
    std::vector<Value> args;
    args.emplace_back(ctx, JS_NewInt32(ctx, 1));
    args.emplace_back(ctx, JS_NewInt32(ctx, 2));
    args.emplace_back(ctx, JS_NewInt32(ctx, 3));
    EXPECT_EQ(*fn.call(args).toInt32(), 6);
}

TEST_F(ValueTest, CallWithSpan) {
    Value fn(ctx, JS_NewCFunction(ctx, fn_double, "double", 1));
    Value args[] = {Value(ctx, JS_NewInt32(ctx, 50))};
    EXPECT_EQ(*fn.call(std::span(args)).toInt32(), 100);
}

TEST_F(ValueTest, InvokeMethod) {
    Value obj(ctx, JS_NewObject(ctx));
    Value fn(ctx, JS_NewCFunction(ctx, fn_double, "double", 1));
    obj.setProperty("doubleIt", fn);

    Atom method(ctx, "doubleIt");
    Value arg(ctx, JS_NewInt32(ctx, 5));
    Value result = obj.invoke(method, arg);
    EXPECT_EQ(*result.toInt32(), 10);
}

TEST_F(ValueTest, CallConstructor) {
    JSValue global = JS_GetGlobalObject(ctx);
    Value Number(ctx, JS_GetPropertyStr(ctx, global, "Number"));
    JS_FreeValue(ctx, global);
    Value result = Number.callConstructor();
    EXPECT_TRUE(result.isObject());
    EXPECT_EQ(*result.toInt32(), 0);
}

TEST_F(ValueTest, IsInstanceOf) {
    JSValue global = JS_GetGlobalObject(ctx);
    Value Array(ctx, JS_GetPropertyStr(ctx, global, "Array"));
    Value Object(ctx, JS_GetPropertyStr(ctx, global, "Object"));
    JS_FreeValue(ctx, global);

    Value arr(ctx, JS_NewArray(ctx));
    Value obj(ctx, JS_NewObject(ctx));

    EXPECT_TRUE(static_cast<bool>(arr.isInstanceOf(Array)));
    EXPECT_TRUE(static_cast<bool>(obj.isInstanceOf(Object)));
}

// ── Promise ────────────────────────────────────────────────────────

TEST_F(ValueTest, PromiseState) {
    const char* code = "(async function f() { return 42; })()";
    Value p(ctx, JS_Eval(ctx, code, std::strlen(code), "<test>", JS_EVAL_TYPE_GLOBAL));
    ASSERT_FALSE(p.isException());
    if (p.isObject()) {
        auto state = p.promiseState();
        EXPECT_NE(state, PromiseState::NotAPromise);
    }
}

// ── RAII ───────────────────────────────────────────────────────────

TEST_F(ValueTest, CopySharesReference) {
    Value a(ctx, JS_NewObject(ctx));
    Value b = a;
    b.setProperty("x", Value(ctx, JS_NewInt32(ctx, 1)));
    EXPECT_TRUE(static_cast<bool>(a.hasProperty("x")));
    EXPECT_TRUE(static_cast<bool>(b.hasProperty("x")));
}

TEST_F(ValueTest, MoveDetachesSource) {
    Value a(ctx, JS_NewObject(ctx));
    Value b = std::move(a);
    EXPECT_TRUE(b.isObject());
    EXPECT_TRUE(a.isUndefined());
}

TEST_F(ValueTest, Swap) {
    Value a(ctx, JS_NewString(ctx, "a"));
    Value b(ctx, JS_NewString(ctx, "b"));
    using std::swap;
    swap(a, b);
    EXPECT_TRUE(a.toString()->str() == "b");
    EXPECT_TRUE(b.toString()->str() == "a");
}

TEST_F(ValueTest, ReleaseTransfersOwnership) {
    Value a(ctx, JS_NewString(ctx, "owned"));
    JSValue raw = a.release();
    EXPECT_TRUE(a.isUndefined());
    Value b(ctx, raw);
    EXPECT_TRUE(b.isString());
    EXPECT_TRUE(b.toString()->str() == "owned");
}

// ── make_argv ──────────────────────────────────────────────────────

TEST_F(ValueTest, MakeArgvFromVector) {
    std::vector<Value> args;
    args.emplace_back(ctx, JS_NewInt32(ctx, 1));
    EXPECT_EQ(Value::make_argv(args).size(), 1u);
}

TEST_F(ValueTest, MakeArgvFromSpan) {
    Value args[] = {Value(ctx, JS_NewInt32(ctx, 1)), Value(ctx, JS_NewInt32(ctx, 2))};
    EXPECT_EQ(Value::make_argv(std::span(args)).size(), 2u);
}

TEST_F(ValueTest, MakeArgvFromConstSpan) {
    Value raw[] = {Value(ctx, JS_NewInt32(ctx, 1)), Value(ctx, JS_NewInt32(ctx, 2))};
    EXPECT_EQ(Value::make_argv(std::span<const Value>(raw)).size(), 2u);
}

// ── Extensible ────────────────────────────────────────────────────

TEST_F(ValueTest, ExtensibleByDefault) {
    Value obj(ctx, JS_NewObject(ctx));
    EXPECT_TRUE(static_cast<bool>(obj.isExtensible()));
}

TEST_F(ValueTest, PreventExtensions) {
    Value obj(ctx, JS_NewObject(ctx));
    EXPECT_TRUE(static_cast<bool>(obj.preventExtensions()));
    EXPECT_FALSE(static_cast<bool>(obj.isExtensible()));
}
