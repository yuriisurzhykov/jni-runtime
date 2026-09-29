// A core implementation of the logging mechanism for core C++ modules.
//
// Created by Yurii Surzhykov on 9/28/26.

#include <jnirt/support/logging/log.h>
#include "support/logging/log_sink.h"

#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace support::logging {
    namespace {
        constexpr size_t kBufferSize = 1024;
        std::atomic<Level> gLevel{Level::Debug};
    }

    void setLevel(Level level) noexcept {
        gLevel.store(level, std::memory_order_relaxed);
    }

    bool enabled(Level level) noexcept {
        return level >= gLevel.load(std::memory_order_relaxed);
    }

    void write(Level level, const char *tag, const char *fmt, ...) noexcept {
        if (!enabled(level)) return;

        char buf[kBufferSize];
        va_list args;
        va_start(args, fmt);
        // vsnprintf cuts the string itself and guarantee the EOF char
        int n = vsnprintf(buf, sizeof buf, fmt, args);
        va_end(args);

        if (n < 0) {
            sinkWrite(level, tag, "<log format error>");
            return;
        }
        sinkWrite(level, tag, buf);
    }
} // namespace support::logging
