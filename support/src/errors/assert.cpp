// The failure paths behind JNIRT_REQUIRE / JNIRT_CHECK / JNIRT_ASSERT / JNIRT_FATAL.
//
// Created by Yurii Surzhykov on 9/30/26.

#include <jnirt/support/assert.h>
#include <jnirt/support/logging/log.h>

#include "abort_hook.h"
#include "format.h"

#include <cstdarg>
#include <cstdio>

#include "jnirt/support/error.h"

namespace jnirt::detail {
    namespace {
        constexpr size_t kBufferSize = 1024;
    }

    void throwIllegalArgument(const char *file, int line, const char *fmt, ...) {
        va_list args;
        va_start(args, fmt);
        std::string message = vformat(fmt, args);
        va_end(args);
        throw IllegalArgument(message, file, line);
    }

    void throwIllegalState(const char *file, int line, const char *fmt, ...) {
        va_list args;
        va_start(args, fmt);
        std::string message = vformat(fmt, args);
        va_end(args);
        throw IllegalState(message, file, line);
    }

    void fail(const char *file, int line, const char *expr, const char *fmt, ...) {
        // Guards against a failure inside the failure path. The flag is per thread: a second thread that
        // fails at the same moment still gets its own message out.
        thread_local bool failing = false;

        // A fixed stack buffer, no heap: after memory corruption or OOM the heap can not be trusted.
        char message[kBufferSize];
        int used = expr
                       ? std::snprintf(message, sizeof message, "%s:%d: assertion `%s` failed: ", file, line, expr)
                       : std::snprintf(message, sizeof message, "%s:%d: ", file, line);
        if (used < 0 || static_cast<size_t>(used) >= sizeof message) used = 0;

        va_list args;
        va_start(args, fmt);
        std::vsnprintf(message + used, sizeof message - static_cast<size_t>(used), fmt, args);
        va_end(args);

        if (!failing) {
            failing = true;
            logging::write(logging::Level::Error, "jnirt", "%s", message);
            setAbortMessage(message);
        }
        std::abort();
    }
}
