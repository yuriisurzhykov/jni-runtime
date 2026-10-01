// The file provides different levels of logging, a log interface and string format options.
//
// Created by Yurii Surzhykov on 9/28/26.

#ifndef JNIRT_SUPPORT_LOGGING_LOG_H
#define JNIRT_SUPPORT_LOGGING_LOG_H
#pragma once
#include <cstdint>

namespace jnirt::logging {
    enum class Level : uint8_t { Verbose, Debug, Info, Warn, Error };

    void setLevel(Level level) noexcept;

    bool enabled(Level level) noexcept;

    void write(Level level, const char *tag, const char *fmt, ...) noexcept
    __attribute__((format(printf, 3, 4)));

    // A forwarder sees every line that passed the level filter, formatted, before the platform sink (logcat).
    // It returns true if it took the line (the sink is then skipped), false to leave the line to the sink. It may be
    // called from any thread and must not throw. The runtime installs one that sends lines to Kotlin
    // (NativeLogBridge); nullptr removes it. Lines of JNIRT_ASSERT / JNIRT_FATAL never go through it.
    using Forwarder = bool (*)(Level level, const char *tag, const char *message) noexcept;

    void setForwarder(Forwarder forwarder) noexcept;
} // namespace jnirt::logging
#endif //JNIRT_SUPPORT_LOGGING_LOG_H
