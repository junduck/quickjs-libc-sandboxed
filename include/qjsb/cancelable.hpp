#pragma once

#include "quickjs.h"

namespace qjsb {

struct IOResource {
    virtual ~IOResource() = default;
    virtual void cancel() noexcept = 0;
    virtual void forceCleanup(JSContext *ctx) = 0;
};

} // namespace qjsb
