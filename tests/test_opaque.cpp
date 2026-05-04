#include <gtest/gtest.h>

#include <atomic>

#include "opaque.hpp"
#include "value.hpp"

using namespace qjsb;

struct LeakCounter {
    static std::atomic<int> alive;
    int id;

    LeakCounter(int i) : id(i) { alive.fetch_add(1); }
    ~LeakCounter() { alive.fetch_sub(1); }
    LeakCounter(const LeakCounter&) = delete;
    LeakCounter(LeakCounter&&) = delete;
};
std::atomic<int> LeakCounter::alive{0};

// ── Helper: create a bare JSContext + Value for isolated tests ──

struct OpaqueTest : ::testing::Test {
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

    Value newObject() {
        return Value(ctx, JS_NewObject(ctx));
    }
};

// ── Tests ──────────────────────────────────────────────────────────

TEST_F(OpaqueTest, SurvivesWhileReferencesExist) {
    ASSERT_EQ(LeakCounter::alive.load(), 0);

    Value obj = newObject();
    obj.setOpaque<LeakCounter>(42);
    EXPECT_EQ(LeakCounter::alive.load(), 1);
    EXPECT_EQ(obj.getOpaque<LeakCounter>()->id, 42);

    {
        Value copy = obj;
        EXPECT_EQ(LeakCounter::alive.load(), 1);
    }
    EXPECT_EQ(LeakCounter::alive.load(), 1);
}

TEST_F(OpaqueTest, FreedAfterLastReference) {
    ASSERT_EQ(LeakCounter::alive.load(), 0);
    {
        Value obj = newObject();
        obj.setOpaque<LeakCounter>(99);
        EXPECT_EQ(LeakCounter::alive.load(), 1);
    }
    EXPECT_EQ(LeakCounter::alive.load(), 0);
}

TEST_F(OpaqueTest, MoveTransfersOwnership) {
    ASSERT_EQ(LeakCounter::alive.load(), 0);
    {
        Value obj = newObject();
        obj.setOpaque<LeakCounter>(7);
        EXPECT_EQ(LeakCounter::alive.load(), 1);

        Value moved = std::move(obj);
        EXPECT_EQ(moved.getOpaque<LeakCounter>()->id, 7);
        EXPECT_EQ(LeakCounter::alive.load(), 1);
    }
    EXPECT_EQ(LeakCounter::alive.load(), 0);
}

TEST_F(OpaqueTest, MultipleCopiesShareOpaque) {
    ASSERT_EQ(LeakCounter::alive.load(), 0);

    Value a = newObject();
    a.setOpaque<LeakCounter>(1);
    EXPECT_EQ(LeakCounter::alive.load(), 1);

    Value b = a;
    Value c = a;
    Value d = b;
    EXPECT_EQ(LeakCounter::alive.load(), 1);
    EXPECT_EQ(d.getOpaque<LeakCounter>()->id, 1);

    a = Value{};
    b = Value{};
    EXPECT_EQ(LeakCounter::alive.load(), 1);

    c = Value{};
    EXPECT_EQ(LeakCounter::alive.load(), 1);

    d = Value{};
    EXPECT_EQ(LeakCounter::alive.load(), 0);
}

TEST_F(OpaqueTest, NoOpaqueDoesNotCrash) {
    Value obj = newObject();
    EXPECT_EQ(obj.getOpaque<LeakCounter>(), nullptr);
    {
        Value copy = obj;
    }
    SUCCEED();
}

TEST_F(OpaqueTest, ReleaseBypassesOpaqueCleanup) {
    ASSERT_EQ(LeakCounter::alive.load(), 0);
    {
        Value obj = newObject();
        obj.setOpaque<LeakCounter>(1);
        EXPECT_EQ(LeakCounter::alive.load(), 1);

        JSValue raw = obj.release();  // ctx_ = nullptr — destructor no-ops
        EXPECT_EQ(LeakCounter::alive.load(), 1);

        JS_FreeValue(ctx, raw);  // raw free — won't clean opaque
        EXPECT_EQ(LeakCounter::alive.load(), 1);  // leaked (by design)
    }
    // opaque leaked because release() bypassed the RAII cleanup path.
    // Reset to pass the scope-end assertion.
    LeakCounter::alive.store(0);
}
