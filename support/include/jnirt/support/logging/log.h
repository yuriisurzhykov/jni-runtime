// The file provides different levels of logging, a log interface and string format options.
//
// Created by Yurii Surzhykov on 9/28/26.

#ifndef JNIRT_SUPPORT_LOGGING_LOG_H
#define JNIRT_SUPPORT_LOGGING_LOG_H
#pragma once
#include <cstdint>

namespace support::logging {
    enum class Level : uint8_t { Verbose, Debug, Info, Warn, Error };

    void setLevel(Level level) noexcept;

    bool enabled(Level level) noexcept;

    void write(Level level, const char *tag, const char *fmt, ...) noexcept
    __attribute__((format(printf, 3, 4)));
} // namespace support::logging
#endif //JNIRT_SUPPORT_LOGGING_LOG_H
