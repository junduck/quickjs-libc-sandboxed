#pragma once

#include "quickjs.h"

struct Primordials {
  JSValue Object;
  JSValue Array;
  JSValue Map;
  JSValue Set;
  JSValue Promise;
  JSValue Error;
  JSValue TypeError;
  JSValue RangeError;

  JSValue Object_prototype;
  JSValue Array_prototype;
  JSValue Map_prototype;
  JSValue Set_prototype;
  JSValue Promise_prototype;
  JSValue Function_prototype;
  JSValue String_prototype;

  void cache(JSContext *ctx);
  void free(JSContext *ctx);
};
