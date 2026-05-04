#pragma once

#include "cstring.hpp"

// TODO: document is not clear about atom's ownership

// It seems that: when passed as arg, it is by ref (no ownership transfer what so ever), it is simply just idx to
// rt->atom_array, ref counted and popped when rc == 0

// When returned from jsquick function, rc is increased (thus consumer owning the atom and should release)
// observation: __JS_FindAtom -> p->header.ref_count++; before return

namespace qjsb {
// JSAtom is just uint32_t but the usage is ref counted by qjs so we need RAII
class Atom {
  JSContext *ctx_ = nullptr;
  JSAtom atom_ = JS_ATOM_NULL;

public:
  Atom() = default;

  Atom(JSContext *ctx, JSAtom atom) noexcept : ctx_(ctx), atom_(atom) {}

  Atom(JSContext *ctx, std::string_view name) noexcept : ctx_(ctx) { atom_ = JS_NewAtomLen(ctx_, name.data(), name.size()); }

  Atom(Atom const &o) noexcept : ctx_(o.ctx_), atom_(o.ctx_ ? JS_DupAtom(o.ctx_, o.atom_) : JS_ATOM_NULL) {}

  Atom(Atom &&o) noexcept : ctx_(o.ctx_), atom_(o.atom_) {
    o.ctx_ = nullptr;
    o.atom_ = JS_ATOM_NULL;
  }

  Atom &operator=(Atom o) noexcept {
    swap(*this, o);
    return *this;
  }

  ~Atom() noexcept {
    if (ctx_)
      JS_FreeAtom(ctx_, atom_);
  }

  friend void swap(Atom &lhs, Atom &rhs) noexcept {
    using std::swap;
    swap(lhs.atom_, rhs.atom_);
    swap(lhs.ctx_, rhs.ctx_);
  }

  explicit operator bool() const { return atom_ != JS_ATOM_NULL; }

  CString to_cstring() const {
    size_t len = 0;
    const char *s = JS_AtomToCStringLen(ctx_, &len, atom_);
    return CString{ctx_, s, len};
  }

  operator JSAtom() const { return atom_; }
};
} // namespace qjsb
