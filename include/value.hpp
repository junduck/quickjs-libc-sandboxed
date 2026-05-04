#pragma once

#include <expected>
#include <ranges>

#include <boost/container/small_vector.hpp>

#include "atom.hpp"
#include "common.hpp"
#include "cstring.hpp"
#include "opaque.hpp"

namespace qjsb {

class Value {
  JSContext *ctx_ = nullptr;
  JSValue val_ = JS_UNDEFINED;
  bool opaque_set_ = false;

  void freeOpaqueIfLastRef() noexcept {
    if (!opaque_set_ || !JS_VALUE_HAS_REF_COUNT(val_) || !::JS_IsObject(val_))
      return;
    auto *p = (JSRefCountHeader *)JS_VALUE_GET_PTR(val_);
    if (p->ref_count != 1)
      return;

    JSClassID cid;
    void *opaque = ::JS_GetAnyOpaque(val_, &cid);
    if (opaque)
      delete static_cast<OpaqueStorage *>(opaque);
  }

public:
  // --- RAII ---

  Value() = default;

  Value(JSContext *ctx, JSValue val) noexcept : ctx_(ctx), val_(val) {}

  Value(Value const &o) noexcept : ctx_(o.ctx_), val_(o.ctx_ ? JS_DupValue(o.ctx_, o.val_) : JS_UNDEFINED), opaque_set_(o.opaque_set_) {}

  Value(Value &&o) noexcept : ctx_(o.ctx_), val_(o.val_), opaque_set_(o.opaque_set_) {
    o.ctx_ = nullptr;
    o.val_ = JS_UNDEFINED;
    o.opaque_set_ = false;
  }

  Value &operator=(Value o) noexcept {
    swap(*this, o);
    return *this;
  }

  ~Value() noexcept {
    if (ctx_) {
      freeOpaqueIfLastRef();
      JS_FreeValue(ctx_, val_);
    }
  }

  friend void swap(Value &lhs, Value &rhs) noexcept {
    using std::swap;
    swap(lhs.ctx_, rhs.ctx_);
    swap(lhs.val_, rhs.val_);
    swap(lhs.opaque_set_, rhs.opaque_set_);
  }

  // --- Raw handles ---

  // Enforce v.borrow() on JSValueConst interface
  JSValueConst borrow() const { return val_; }

  // Enforce v.release() on JSValue interface
  JSValue release() {
    opaque_set_ = false;
    auto tmp = val_;
    ctx_ = nullptr;
    val_ = JS_UNDEFINED;
    return tmp;
  }

  JSContext *ctx() const { return ctx_; }

  template <std::ranges::input_range R>
    requires std::same_as<std::remove_const_t<std::ranges::range_value_t<R>>, Value>
  static auto make_argv(R &&args) {
    boost::container::small_vector<JSValue, 4> argv;
    if constexpr (std::ranges::sized_range<R>) {
      argv.reserve(std::ranges::size(args));
    }
    for (auto const &a : args)
      argv.push_back(a.borrow());
    return argv;
  }

  // --- Exception ---

  void assertOk() const {
    if (!isException())
      return;
    auto ex = ::JS_GetException(ctx_);
    char const *st = ::JS_ToCString(ctx_, ex);
    std::string msg = st ? st : "JS error";

    ::JS_FreeCString(ctx_, st);
    ::JS_FreeValue(ctx_, ex);

    throw JsError(std::move(msg));
  }

  // --- Type checks ---

  bool isNumber() const { return ::JS_IsNumber(val_); }
  bool isBigInt() const { return ::JS_IsBigInt(ctx_, val_); }
  bool isBool() const { return ::JS_IsBool(val_); }
  bool isNull() const { return ::JS_IsNull(val_); }
  bool isUndefined() const { return ::JS_IsUndefined(val_); }
  bool isException() const { return ::JS_IsException(val_); }
  bool isUninitialized() const { return ::JS_IsUninitialized(val_); }
  bool isString() const { return ::JS_IsString(val_); }
  bool isSymbol() const { return ::JS_IsSymbol(val_); }
  bool isObject() const { return ::JS_IsObject(val_); }
  bool isError() const { return ::JS_IsError(ctx_, val_); }
  bool isFunction() const { return ::JS_IsFunction(ctx_, val_); }
  bool isConstructor() const { return ::JS_IsConstructor(ctx_, val_); }

  // --- Type Conversion ---

  std::expected<bool, int> toBool() const {
    JsResult r = JS_ToBool(ctx_, val_);
    if (r.isException())
      return std::unexpected(-1);
    return static_cast<bool>(r);
  }

  std::expected<int32_t, int> toInt32() const {
    int32_t v = 0;
    if (JS_ToInt32(ctx_, &v, val_))
      return std::unexpected(-1);
    return v;
  }

  std::expected<int64_t, int> toInt64() const {
    int64_t v = 0;
    if (JS_ToInt64(ctx_, &v, val_))
      return std::unexpected(-1);
    return v;
  }

  std::expected<double, int> toFloat64() const {
    double v = 0;
    if (JS_ToFloat64(ctx_, &v, val_))
      return std::unexpected(-1);
    return v;
  }

  std::expected<uint64_t, int> toIndex() const {
    uint64_t v = 0;
    if (JS_ToIndex(ctx_, &v, val_))
      return std::unexpected(-1);
    return v;
  }

  std::expected<CString, int> toString() const {
    size_t len;
    char const *ptr = ::JS_ToCStringLen(ctx_, &len, val_);
    if (ptr == nullptr && len == 0) {
      return std::unexpected(-1);
    }
    return CString{ctx_, ptr, len};
  }

  std::expected<Atom, int> toAtom() const {
    auto raw = ::JS_ValueToAtom(ctx_, val_);
    if (raw == JS_ATOM_NULL) {
      return std::unexpected<int>(-1);
    }
    return Atom{ctx_, raw};
  }

  // --- Comparison ---

  bool isStrictEq(Value const &other) const { return ::JS_StrictEq(ctx_, val_, other.borrow()); }
  bool isSameValue(Value const &other) const { return ::JS_SameValue(ctx_, val_, other.borrow()); }
  bool isSameValueZero(Value const &other) const { return ::JS_SameValueZero(ctx_, val_, other.borrow()); }

  // --- Property Get ---

  Value getProperty(Atom const &prop) const { return Value{ctx_, JS_GetProperty(ctx_, val_, prop)}; }
  Value getProperty(uint32_t idx) const { return Value{ctx_, JS_GetPropertyUint32(ctx_, val_, idx)}; }
  Value getProperty(std::string_view prop) const {
    Atom a{ctx_, prop};
    if (!a)
      return {ctx_, JS_EXCEPTION};
    return Value{ctx_, JS_GetProperty(ctx_, val_, a)};
  }

  // --- Property Set (passed by val: v is duplicated, or explictly std::move to transfer ownership) ---

  JsResult setProperty(Atom const &prop, Value v) { return JS_SetProperty(ctx_, val_, prop, v.release()); }
  JsResult setProperty(uint32_t idx, Value v) { return JS_SetPropertyUint32(ctx_, val_, idx, v.release()); }
  JsResult setProperty(int64_t idx, Value v) { return JS_SetPropertyInt64(ctx_, val_, idx, v.release()); }
  JsResult setProperty(std::string_view prop, Value v) {
    Atom a{ctx_, prop};
    if (!a)
      return -1;
    return JS_SetProperty(ctx_, val_, a, v.release());
  }

  // --- Property Query ---

  JsResult hasProperty(Atom const &prop) const { return JS_HasProperty(ctx_, val_, prop); }
  JsResult hasProperty(std::string_view prop) const {
    Atom a{ctx_, prop};
    if (!a)
      return -1;
    return JS_HasProperty(ctx_, val_, a);
  }
  JsResult deleteProperty(Atom const &prop, int flags) { return JS_DeleteProperty(ctx_, val_, prop, flags); }

  // --- Define Property ---

  JsResult defineProperty(Atom const &prop, Value const &v, Value const &getter, Value const &setter, int flags) {
    return JS_DefineProperty(ctx_, val_, prop, v.borrow(), getter.borrow(), setter.borrow(), flags);
  }
  JsResult definePropertyValue(Atom const &prop, Value v, int flags) { return JS_DefinePropertyValue(ctx_, val_, prop, v.release(), flags); }
  JsResult definePropertyValue(uint32_t idx, Value v, int flags) { return JS_DefinePropertyValueUint32(ctx_, val_, idx, v.release(), flags); }
  JsResult definePropertyValue(std::string_view prop, Value v, int flags) {
    Atom a{ctx_, prop};
    if (!a)
      return -1;
    return JS_DefinePropertyValue(ctx_, val_, a, v.release(), flags);
  }
  JsResult definePropertyGetSet(Atom const &prop, Value getter, Value setter, int flags) {
    return JS_DefinePropertyGetSet(ctx_, val_, prop, getter.release(), setter.release(), flags);
  }

  // --- Prototype ---

  JsResult setPrototype(Value const &proto) { return JS_SetPrototype(ctx_, val_, proto.borrow()); }
  Value getPrototype() const { return Value{ctx_, JS_GetPrototype(ctx_, val_)}; }

  // --- Object Ext ---

  JsResult isExtensible() const { return JS_IsExtensible(ctx_, val_); }
  JsResult preventExtensions() { return JS_PreventExtensions(ctx_, val_); }

  // --- Function / Call ---

  // call with explicit this, 0 args
  Value callMethod(Value const &this_obj) const { return Value{ctx_, JS_Call(ctx_, val_, this_obj.borrow(), 0, nullptr)}; }

  // call with explicit this, 1 arg
  Value callMethod(Value const &this_obj, Value const &arg) const {
    return Value{ctx_, JS_Call(ctx_, val_, this_obj.borrow(), 1, &const_cast<Value &>(arg).val_)};
  }

  // call with explicit this, any range
  template <std::ranges::input_range R>
    requires std::same_as<std::remove_const_t<std::ranges::range_value_t<R>>, Value>
  Value callMethod(Value const &this_obj, R &&args) const {
    auto argv = make_argv(std::forward<R>(args));
    return Value{ctx_, JS_Call(ctx_, val_, this_obj.borrow(), static_cast<int>(argv.size()), argv.data())};
  }

  // call as free, 0 args
  Value call() const { return Value{ctx_, JS_Call(ctx_, val_, JS_UNDEFINED, 0, nullptr)}; }

  // call as free, 1 arg
  Value call(Value const &arg) const { return Value{ctx_, JS_Call(ctx_, val_, JS_UNDEFINED, 1, &const_cast<Value &>(arg).val_)}; }

  // call as free, any range
  template <std::ranges::input_range R>
    requires std::same_as<std::remove_const_t<std::ranges::range_value_t<R>>, Value>
  Value call(R &&args) const {
    auto argv = make_argv(std::forward<R>(args));
    return Value{ctx_, JS_Call(ctx_, val_, JS_UNDEFINED, static_cast<int>(argv.size()), argv.data())};
  }

  // invoke, 0 args
  Value invoke(Atom const &atom) const { return Value{ctx_, JS_Invoke(ctx_, val_, atom, 0, nullptr)}; }

  // invoke, 1 arg
  Value invoke(Atom const &atom, Value const &arg) const { return Value{ctx_, JS_Invoke(ctx_, val_, atom, 1, &const_cast<Value &>(arg).val_)}; }

  // invoke, any range
  template <std::ranges::input_range R>
    requires std::same_as<std::remove_const_t<std::ranges::range_value_t<R>>, Value>
  Value invoke(Atom const &atom, R &&args) const {
    auto argv = make_argv(std::forward<R>(args));
    return Value{ctx_, JS_Invoke(ctx_, val_, atom, static_cast<int>(argv.size()), argv.data())};
  }

  // constructor, 0 args
  Value callConstructor() const { return Value{ctx_, JS_CallConstructor(ctx_, val_, 0, nullptr)}; }

  // constructor, 1 arg
  Value callConstructor(Value const &arg) const { return Value{ctx_, JS_CallConstructor(ctx_, val_, 1, &const_cast<Value &>(arg).val_)}; }

  // constructor, any range
  template <std::ranges::input_range R>
    requires std::same_as<std::remove_const_t<std::ranges::range_value_t<R>>, Value>
  Value callConstructor(R &&args) const {
    auto argv = make_argv(std::forward<R>(args));
    return Value{ctx_, JS_CallConstructor(ctx_, val_, static_cast<int>(argv.size()), argv.data())};
  }

  // constructor with new.target, 1 arg
  Value callConstructor(Value const &new_target, Value const &arg) const {
    return Value{ctx_, JS_CallConstructor2(ctx_, val_, new_target.borrow(), 1, &const_cast<Value &>(arg).val_)};
  }

  // constructor with new.target, any range
  template <std::ranges::input_range R>
    requires std::same_as<std::remove_const_t<std::ranges::range_value_t<R>>, Value>
  Value callConstructor(Value const &new_target, R &&args) const {
    auto argv = make_argv(std::forward<R>(args));
    return Value{ctx_, JS_CallConstructor2(ctx_, val_, new_target.borrow(), static_cast<int>(argv.size()), argv.data())};
  }

  JsResult isInstanceOf(Value const &obj) const { return JS_IsInstanceOf(ctx_, val_, obj.borrow()); }

  // --- Promise ---

  PromiseState promiseState() const { return static_cast<PromiseState>(JS_PromiseState(ctx_, val_)); }
  Value promiseResult() const { return Value{ctx_, JS_PromiseResult(ctx_, val_)}; }

  // --- Opaque ---

  JSClassID getClassID() const { return ::JS_GetClassID(val_); }

  template <typename T, typename... Args>
  void setOpaque(Args &&...args) {
    auto *storage = new OpaqueStorage();
    storage->emplace<T>(std::forward<Args>(args)...);
    ::JS_SetOpaque(val_, storage);
    opaque_set_ = true;
  }

  template <typename T>
  T *getOpaque() const {
    if (!::JS_IsObject(val_))
      return nullptr;
    JSClassID cid;
    auto *storage = static_cast<OpaqueStorage *>(::JS_GetAnyOpaque(val_, &cid));
    return storage ? storage->get<T>() : nullptr;
  }
};
} // namespace qjsb
