// Programmer errors and broken invariants. See ADR-0002.
//
//   JNIRT_REQUIRE(cond, fmt, ...)  bad argument from the caller    -> throws jnirt::IllegalArgument
//   JNIRT_CHECK(cond, fmt, ...)    call in a wrong state           -> throws jnirt::IllegalState
//   JNIRT_ASSERT(cond, fmt, ...)   our own invariant, debug only   -> crash (removed in release)
//   JNIRT_FATAL(fmt, ...)          "this must never happen"        -> crash, always
//
// REQUIRE/CHECK are the same in debug and release. entry() turns them into IllegalArgumentException /
// IllegalStateException. With no entry() above them (pure C++ callers, our own threads) nobody catches them,
// so the process terminates and the tombstone shows the message.
//
// The message is printf-style, checked by the compiler, and its arguments are evaluated only on failure.
// A message is mandatory. Do not use REQUIRE/CHECK in destructors or noexcept functions: the exception can not
// leave them, so it becomes a crash.
//
// Created by Yurii Surzhykov on 9/28/26.

#ifndef JNIRT_SUPPORT_ASSERT_H
#define JNIRT_SUPPORT_ASSERT_H

#pragma once
#include <jnirt/support/compiler.h>
#include <jnirt/support/error.h>

namespace jnirt::detail {
    // Not for direct use: go through the macros below.
    [[noreturn]] void throwIllegalArgument(const char *file, int line, const char *fmt, ...) JNIRT_PRINTF(3, 4);

    [[noreturn]] void throwIllegalState(const char *file, int line, const char *fmt, ...) JNIRT_PRINTF(3, 4);

    // Logs, puts the text into the tombstone and aborts. `expr` may be null.
    [[noreturn]] void fail(const char *file, int line, const char *expr, const char *fmt, ...) JNIRT_PRINTF(4, 5);
}

#define JNIRT_REQUIRE(cond, ...)                                                          \
    do {                                                                                  \
        if (JNIRT_UNLIKELY(!(cond)))                                                      \
            ::jnirt::detail::throwIllegalArgument(__FILE__, __LINE__, __VA_ARGS__);       \
    } while (0)

#define JNIRT_CHECK(cond, ...)                                                            \
    do {                                                                                  \
        if (JNIRT_UNLIKELY(!(cond)))                                                      \
            ::jnirt::detail::throwIllegalState(__FILE__, __LINE__, __VA_ARGS__);          \
    } while (0)

#define JNIRT_FATAL(...) ::jnirt::detail::fail(__FILE__, __LINE__, nullptr, __VA_ARGS__)

#ifndef NDEBUG
#define JNIRT_ASSERT(cond, ...)                                                           \
    do {                                                                                  \
        if (JNIRT_UNLIKELY(!(cond)))                                                      \
            ::jnirt::detail::fail(__FILE__, __LINE__, #cond, __VA_ARGS__);                \
    } while (0)
#else
// Release: nothing runs, but the code is still compiled, so a typo in an assert can not hide until debug.
#define JNIRT_ASSERT(cond, ...)                                                           \
    do {                                                                                  \
        if (false) {                                                                      \
            if (!(cond)) ::jnirt::detail::fail(__FILE__, __LINE__, #cond, __VA_ARGS__);   \
        }                                                                                 \
    } while (0)
#endif

#endif //JNIRT_SUPPORT_ASSERT_H
