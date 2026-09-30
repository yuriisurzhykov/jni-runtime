// Small compiler-portability macros shared by the public support headers.
//
// Created by Yurii Surzhykov on 9/30/26.

#ifndef JNIRT_SUPPORT_COMPILER_H
#define JNIRT_SUPPORT_COMPILER_H

// Lets the compiler check printf-style arguments. Put it BEFORE the declaration:
//   JNIRT_PRINTF(2, 3) void f(int x, const char *fmt, ...);
// For member functions, `this` is argument 1.
#if defined(__GNUC__) || defined(__clang__)
#define JNIRT_PRINTF(fmtIndex, argsIndex) __attribute__((format(printf, fmtIndex, argsIndex)))
#define JNIRT_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define JNIRT_PRINTF(fmtIndex, argsIndex)
#define JNIRT_UNLIKELY(x) (x)
#endif

#endif //JNIRT_SUPPORT_COMPILER_H
