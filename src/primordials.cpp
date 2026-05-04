#include "primordials.hpp"
#include "quickjs.h"

void Primordials::cache(JSContext *ctx) {
  JSValue global = JS_GetGlobalObject(ctx);

  Object = JS_GetPropertyStr(ctx, global, "Object");
  Array = JS_GetPropertyStr(ctx, global, "Array");
  Map = JS_GetPropertyStr(ctx, global, "Map");
  Set = JS_GetPropertyStr(ctx, global, "Set");
  Promise = JS_GetPropertyStr(ctx, global, "Promise");
  Error = JS_GetPropertyStr(ctx, global, "Error");
  TypeError = JS_GetPropertyStr(ctx, global, "TypeError");
  RangeError = JS_GetPropertyStr(ctx, global, "RangeError");

  Object_prototype = JS_GetPropertyStr(ctx, Object, "prototype");
  Array_prototype = JS_GetPropertyStr(ctx, Array, "prototype");
  Function_prototype = JS_GetPropertyStr(ctx, JS_GetPropertyStr(ctx, global, "Function"), "prototype");
  String_prototype = JS_GetPropertyStr(ctx, JS_GetPropertyStr(ctx, global, "String"), "prototype");
  Map_prototype = JS_GetPropertyStr(ctx, Map, "prototype");
  Set_prototype = JS_GetPropertyStr(ctx, Set, "prototype");
  Promise_prototype = JS_GetPropertyStr(ctx, Promise, "prototype");

  JS_FreeValue(ctx, global);
}

void Primordials::free(JSContext *ctx) {
  JS_FreeValue(ctx, Object);
  JS_FreeValue(ctx, Array);
  JS_FreeValue(ctx, Map);
  JS_FreeValue(ctx, Set);
  JS_FreeValue(ctx, Promise);
  JS_FreeValue(ctx, Error);
  JS_FreeValue(ctx, TypeError);
  JS_FreeValue(ctx, RangeError);
  JS_FreeValue(ctx, Object_prototype);
  JS_FreeValue(ctx, Array_prototype);
  JS_FreeValue(ctx, Map_prototype);
  JS_FreeValue(ctx, Set_prototype);
  JS_FreeValue(ctx, Promise_prototype);
  JS_FreeValue(ctx, Function_prototype);
  JS_FreeValue(ctx, String_prototype);
}
